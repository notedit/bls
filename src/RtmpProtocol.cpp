/*
 * RtmpProtocol.cpp
 *
 *  Created on: 2014-10-27
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "RtmpProtocol.h"
#include <BlsMessage.h>
#include <utilities.h>
#include <BlsLogger.h>
#include <uv.h>
#include <time.h>
#include <assert.h>

size_t FORMAT_MESSAGE_HEADER_LEN[] = { 11, 7, 3, 0 };

/**
 * 当接收到新的chunk后的回调函数，新的chunk保存在current_chunk
 * @param p
 */
void on_new_chunk(RtmpProtocol *p)
{
    chunk_bucket_t *chunk = p->current_chunk;

    ngx_queue_insert_tail(&p->current_channel->chain.queue, &chunk->queue);

    if (chunk->state != CHUNK_STATE_END)
    {
        p->expect_chunk();
        return;
    }

    CLIENT_DEBUG(p->client, "get a whole chunk. fmt: 0x%2x cid: %u"
            " sid: %u ts: %u type: 0x%2x "
            "msg_len: %u chunk_len: %u",
            chunk->format, chunk->chunk_id,
            p->current_channel->header.stream_id,
            p->current_channel->header.timestamp, p->current_channel->header.type,
            p->current_channel->header.msg_len, p->current_chunk->payload_length);

    if (NULL != p->publish_source)
    {
        assert(chunk->source_stream_name == NULL);
        chunk->source_stream_name = new std::string(
                p->publish_source->source->stream_name);
    }

    //处理一个完整的message到来
    if (p->current_channel->header.msg_len == p->current_channel->header.msg_recv)
    {
        CLIENT_DEBUG(p->client, "get a whole message");
        p->current_channel->header.msg_recv = 0;
        msg_decode(p);
    }

    //开始接收下一个chunk
    p->expect_chunk();
}

/**
 * 收取chunk数据的状态机，收到一个完整的chunk后，调用protocol的on_new_chunk
 * @param buf 数据的缓冲区
 * @param data 指向protocol实例
 * @param recved 读取到的数据大小，-1表示读取失败（连接异常）
 */
void chunk_state_machine(uint8_t *buf, void *data, int recved);

RtmpProtocol::RtmpProtocol(RtmpClient *c)
{
    client = c;
    current_chunk = NULL;
    current_channel = NULL;
    window_size = 0;
    acked_size = 0;
    total_recved_size = 0;
    total_recved_video_msg = 0;
    total_recved_audio_msg = 0;

    //    uv_timer_init(uv_default_loop(), &ping_pong_timer);
    ping_pong_timer.data = (void *) this;
    pong_get = true;

    publish_source = NULL;
    play_consumer = NULL;
}

RtmpProtocol::~RtmpProtocol()
{
    rtmp_channel_t *temp_channel;

    CLIENT_NOTICE(client, "free protocol resource");

#ifdef NODE_EXTEND
    Nan::HandleScope scope;

    v8::Local<v8::Value> argv_res[] =
    {
        Nan::New(uv_err_name(-1)).ToLocalChecked(),
        Nan::New<v8::Number>(total_recved_size),
        Nan::New<v8::Number>(client->bls_socket->send_bytes)
    };
    client->node_on_close_cb->Call(3, argv_res);
#endif

    if (NULL != play_consumer)
    {
        free_consumer(play_consumer);
    }

    if (NULL != publish_source)
    {
        CLIENT_TRACE(client, "free publish source");
        collect_source(publish_source);
    }

    for (std::map<uint32_t, rtmp_channel_t*>::iterator it = channels.begin(); it
            != channels.end(); ++it)
    {
        temp_channel = (rtmp_channel_t *) it->second;
        bls_delete(temp_channel);
    }

    if (NULL != current_chunk)
        free_chunk_bucket(current_chunk);

    bls_delete(client);
}

void RtmpProtocol::start()
{
    set_window_ack_size(2500000);
    set_peer_bandwidth(2500000, 2);
    set_chunk_size();

    expect_chunk();
}

void RtmpProtocol::expect_chunk()
{
    ngx_queue_t *q;
    chunk_bucket_t *b;

    current_chunk = alloc_chunk_bucket();

    current_chunk->payload_recv_len = 0;
    current_chunk->header_recv_len = 0;
    current_chunk->state = CHUNK_STATE_BEGIN;

    /*
     * 如果端设置了chunk size为default的整数倍，接收还没有收完的一个chunk
     */
    if (client->chunk_size > DEFAULT_CHUNK_BUCKET_SIZE && NULL
            != current_channel)
    {
        if (current_channel->header.msg_recv % client->chunk_size != 0 &&
                current_channel->header.msg_recv < current_channel->header.msg_len)
        {
            q = ngx_queue_last(&current_channel->chain.queue);
            b = ngx_queue_data(q, chunk_bucket_t, queue);

            wrap_chunk_header(current_chunk, RTMP_CHUNK_FMT_TYPE3, b->chunk_id);

            current_chunk->state = CHUNK_STATE_READ_PAYLOAD;

            client->read_n(current_chunk->payload_start_p,
                    expect_payload_len(current_channel, client), (void *) (this),
                    chunk_state_machine);

            return;
        }
    }

    chunk_state_machine(NULL, (void *) (this), 0);
}

void RtmpProtocol::send_ping_request()
{
    CLIENT_NOTICE(client, "send ping request to client");

    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 6, 4, 0);
    write_2bytes(b->payload_start_p, 6);
    write_4bytes(b->payload_start_p + 2, time(NULL));
    b->payload_length = 6;

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::send_pong_response(uint32_t t)
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 6, 4, 0);
    write_2bytes(b->payload_start_p, 7);
    write_4bytes(b->payload_start_p + 2, t);
    b->payload_length = 6;

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void ping_pong_cb(uv_timer_t* handle, int status)
{
    RtmpProtocol *p = (RtmpProtocol *) handle->data;

    if (!p->pong_get)
    {
        //如果没收到回复的pong包，关闭链接
        CLIENT_WARNING(p->client, "do not get pong response, close client");
        //bls_delete(p);
        p->client->close();
    }
    else
    {
        //发送ping包
        p->pong_get = false;
        p->send_ping_request();
    }
}

void RtmpProtocol::set_ping_pong_interval(int t)
{
    //    uv_timer_start(&ping_pong_timer, ping_pong_cb, 1, t*1000);
}

void RtmpProtocol::stop_ping_pong_timer()
{
    //    uv_timer_stop(&ping_pong_timer);
}

void RtmpProtocol::set_window_ack_size(size_t w)
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 4, 5, 0);
    write_4bytes(b->payload_start_p, w);
    b->payload_length = 4;

    CLIENT_TRACE(client, "set window ack size");

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::set_peer_bandwidth(size_t w, uint8_t type)
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 5, 6, 0);
    write_4bytes(b->payload_start_p, w);
    write_1bytes(b->payload_start_p + 4, type);
    b->payload_length = 5;

    CLIENT_TRACE(client, "set band width");

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::set_chunk_size()
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 4, 1, 0);
    write_4bytes(b->payload_start_p, DEFAULT_CHUNK_BUCKET_SIZE);
    b->payload_length = 4;

    CLIENT_TRACE(client, "set chunk size");

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::send_stream_eof()
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 6, 4, 0);
    write_2bytes(b->payload_start_p, 1);
    write_4bytes(b->payload_start_p + 2, 1);
    b->payload_length = 6;

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::send_stream_begin()
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 6, 4, 0);
    write_2bytes(b->payload_start_p, 0);
    write_4bytes(b->payload_start_p + 2, 1);
    b->payload_length = 6;

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::response_window_ack()
{
    chunk_bucket_t *b = alloc_chunk_bucket();

    wrap_chunk_header(b, 0, RTMP_CID_ProtocolControl, 0, 4, 3, 0);
    write_4bytes(b->payload_start_p, total_recved_size);
    b->payload_length = 4;

    rtmp_write_chunk(client->bls_socket, b);
    free_chunk_bucket(b);

    return;
}

void RtmpProtocol::process_window_ack(int recved)
{
    acked_size += recved;
    total_recved_size += recved;
    if (acked_size >= window_size && window_size != 0)
    {
        acked_size = 0;
        response_window_ack();
    }
}

void chunk_state_machine(uint8_t *buf, void *data, int recved)
{
    RtmpProtocol *protocol = (RtmpProtocol *) (data);
    RtmpClient *client = protocol->client;
    chunk_bucket_t *chunk = protocol->current_chunk;
    std::map<uint32_t, rtmp_channel_t*>::iterator c;
    rtmp_channel_t *p_channel;
    ngx_queue_t *last_q;
    chunk_bucket_t *last_b;

    //接收数据失败，断开连接
    if (recved < 0)
    {
        CLIENT_TRACE(protocol->client, "client leave. err: %s",
                uv_err_name(recved));
        //bls_delete(protocol);
        protocol->client->close();
        return;
    }

    //处理ack信息
    protocol->process_window_ack(recved);

    switch (protocol->current_chunk->state)
    {
    case CHUNK_STATE_BEGIN:
        {
            chunk->state = CHUNK_STATE_SIZE_BASIC_HEADER;
            chunk->header_recv_len = 0;
            chunk->payload_recv_len = 0;
            client->read_n(chunk->header_start_p, 1, data, chunk_state_machine);
            break;
        }

    case CHUNK_STATE_SIZE_BASIC_HEADER:
        {
            chunk->format = (*buf >> 6) & 0x03;
            chunk->chunk_id = *buf & 0x3f;
            chunk->header_recv_len = 1;

            if (chunk->chunk_id > 1)
            {
                chunk->state = CHUNK_STATE_READ_MESSAGE_HEADER;
                if (chunk->format != 3)
                {
                    client->read_n(chunk->header_start_p + recved,
                            FORMAT_MESSAGE_HEADER_LEN[chunk->format], data,
                            chunk_state_machine);
                    break;
                }
                else
                {
                    chunk_state_machine(NULL, data, 0);
                    break;
                }
            }

            chunk->state = CHUNK_STATE_READ_EXT_BASIC_HEADER;

            if (chunk->chunk_id == 0) 
            {
                client->read_n(chunk->header_start_p + recved, 1, data,
                        chunk_state_machine);
            }
            if (chunk->chunk_id == 1)
            {
                client->read_n(chunk->header_start_p + recved, 2, data,
                        chunk_state_machine);
            }
            break;
        }

    case CHUNK_STATE_READ_EXT_BASIC_HEADER:
        {
            chunk->header_recv_len += recved;
            chunk->chunk_id = 64;
            if (recved == 1)
            {
                chunk->chunk_id += *buf;
            }
            if (recved == 2)
            {
                chunk->chunk_id += *(buf++);
                chunk->chunk_id += *buf * 256;
            }

            chunk->state = CHUNK_STATE_READ_MESSAGE_HEADER;
            if (chunk->format != 3)
            {
                client->read_n(chunk->header_start_p + chunk->header_recv_len,
                        FORMAT_MESSAGE_HEADER_LEN[chunk->format], data,
                        chunk_state_machine);
            }
            else
            {
                chunk_state_machine(NULL, data, 0);
            }
            break;
        }

    case CHUNK_STATE_READ_MESSAGE_HEADER:
        {
            chunk->header_recv_len += recved;

            //查找channel信息，找到对应的header
            c = protocol->channels.find(chunk->chunk_id);
            uint8_t *temp_p;

            //构建一个新的header
            if (c == protocol->channels.end())
            {
                if (chunk->format > 1)
                {
                    CLIENT_WARNING(client,
                            "recv chunk error. first chunk(id: %lu) fmt should not be %2x",
                            chunk->chunk_id, chunk->format);
                    //bls_delete(protocol);
                    client->close();
                    return;
                }
                p_channel = new rtmp_channel_t();
                protocol->channels.insert(std::pair<uint32_t, rtmp_channel_t*>(
                        chunk->chunk_id, p_channel));
            }
            else
            {
                p_channel = c->second;
            }

            protocol->current_channel = p_channel;

            if (chunk->format == RTMP_CHUNK_FMT_TYPE3 &&
                    !ngx_queue_empty(&p_channel->chain.queue))
            {
                last_q = ngx_queue_last(&p_channel->chain.queue);
                last_b = ngx_queue_data(last_q, chunk_bucket_t, queue);

                if (last_b->state == CHUNK_STATE_MERGE_HEADER)
                {
                    ngx_queue_remove(last_q);

                    free_chunk_bucket(chunk);
                    protocol->current_chunk = last_b;

                    CLIENT_DEBUG(client,
                            "merge chunk with recv len %lu",
                            last_b->payload_recv_len);

                    last_b->state = CHUNK_STATE_READ_PAYLOAD;
                    client->read_n(last_b->payload_start_p
                            + last_b->payload_recv_len, expect_payload_len(
                            p_channel, client), data, chunk_state_machine);

                    break;
                }
            }

            if (p_channel->header.msg_recv != 0 && chunk->format != RTMP_CHUNK_FMT_TYPE3)
            {
                CLIENT_WARNING(client,
                        "recv chunk type error. "
                        "chunk_id: %lu msg_len: %u recv_len: %u fmt: %2x",
                        chunk->chunk_id, p_channel->header.msg_len,
                        p_channel->header.msg_recv, chunk->format);
                //bls_delete(protocol);
                client->close();
                return;
            }

            //读取header中的数据
            /**
             * chunk header的数据结构如下：
             * 0 fmt+chunkid
             * (00) ext chunkid
             * ————————————————————————type3
             * 000 timestamp
             * ————————————————————————type2
             * 000 message length
             * 0 message type
             * ————————————————————————type1
             * 0000 message stream id
             * ————————————————————————type0
             */
            p_channel->header.delta_timestamp = 0;
            switch (chunk->format)
            {
            case RTMP_CHUNK_FMT_TYPE0:
                p_channel->header.stream_id = *(uint32_t *) (buf + 7);
                p_channel->header.timestamp = 0;

            case RTMP_CHUNK_FMT_TYPE1:
                p_channel->header.type = *(buf + 6);

                temp_p = (uint8_t *) &p_channel->header.msg_len;
                temp_p[2] = *(buf + 3);
                temp_p[1] = *(buf + 4);
                temp_p[0] = *(buf + 5);
                temp_p[3] = 0;

            case RTMP_CHUNK_FMT_TYPE2:
                temp_p = (uint8_t *) &p_channel->header.delta_timestamp;
                temp_p[2] = *(buf + 0);
                temp_p[1] = *(buf + 1);
                temp_p[0] = *(buf + 2);
                temp_p[3] = 0;

            case RTMP_CHUNK_FMT_TYPE3:
                break;
            }

            if(p_channel->header.msg_len == 0)
            {
                CLIENT_WARNING(client,
                        "msg len can not be 0! close it!");
                //bls_delete(protocol);
                client->close();
                return;
            }

            if (p_channel->header.delta_timestamp
                    == RTMP_CHUNK_EXTENDED_TIMESTAMP)
            {
                chunk->state = CHUNK_STATE_READ_EXT_TIME;
                client->read_n(chunk->header_start_p + chunk->header_recv_len,
                        4, data, chunk_state_machine);
                break;
            }
            else
            {
                p_channel->header.timestamp
                        += p_channel->header.delta_timestamp;

                chunk->state = CHUNK_STATE_READ_PAYLOAD;
                client->read_n(chunk->payload_start_p, expect_payload_len(
                        p_channel, client), data, chunk_state_machine);
                break;
            }
        }
    case CHUNK_STATE_READ_EXT_TIME:
        {
            chunk->header_recv_len += recved;
            p_channel = protocol->current_channel;
            uint8_t *temp_p;

            temp_p = (uint8_t *) &p_channel->header.delta_timestamp;
            temp_p[3] = *(buf + 0);
            temp_p[2] = *(buf + 1);
            temp_p[1] = *(buf + 2);
            temp_p[0] = *(buf + 3);

            p_channel->header.timestamp += p_channel->header.delta_timestamp;

            //TODO: 处理时间戳溢出的情况——rtmp specify 1.0

            chunk->state = CHUNK_STATE_READ_PAYLOAD;
            client->read_n(chunk->payload_start_p, expect_payload_len(
                    p_channel, client), data, chunk_state_machine);
            break;
        }
    case CHUNK_STATE_READ_PAYLOAD:
        {
            p_channel = protocol->current_channel;

            chunk->payload_recv_len += recved;
            chunk->payload_length += recved;
            chunk->header_length = chunk->header_recv_len;

            p_channel->header.msg_recv += recved;

            if (p_channel->header.msg_recv == p_channel->header.msg_len
                    || DEFAULT_CHUNK_BUCKET_SIZE - chunk->payload_recv_len
                            < client->chunk_size)
            {
                chunk->state = CHUNK_STATE_END;
            }
            else
            {
                chunk->state = CHUNK_STATE_MERGE_HEADER;
            }

            on_new_chunk(protocol);
            break;
        }
    default:
        break;
    }

    return;
}

uint32_t expect_payload_len(rtmp_channel_t *c, RtmpClient *client)
{
    uint32_t expect_payload = c->header.msg_len - c->header.msg_recv;
    uint32_t temp_size = 0;

    if (expect_payload > client->chunk_size)
    {
        temp_size = client->chunk_size;
    }
    else
    {
        temp_size = expect_payload;
    }

    if (temp_size > DEFAULT_CHUNK_BUCKET_SIZE)
    {
        return DEFAULT_CHUNK_BUCKET_SIZE;
    }
    else
    {
        return temp_size;
    }
}

void wrap_chunk_header(chunk_bucket_t *b, uint8_t fmt, uint32_t chunk_id,
        uint32_t timestamp, uint32_t msg_len, uint8_t type_id, uint32_t msg_id)
{
    uint8_t *cur_p = b->header_start_p;
    bool need_ext_timestamp = false;
    uint8_t *temp_p;

    DEBUG_LOG("wrap chunk header. fmt: 0x%2x cid: %u ts: %u len: %u typeid: 0x%2x msgid: %u",
            fmt, chunk_id, timestamp, msg_len, type_id, msg_id);

    //写fmt
    *cur_p = fmt << 6;

    //写chunk_id
    if (chunk_id < 64)
    {
        *cur_p++ |= chunk_id & 0x3F;
    }
    else if (chunk_id < 320)
    {
        cur_p++;
        *cur_p++ = chunk_id - 64;
    }
    else if (chunk_id < 65599)
    {
        *cur_p++ |= 0x01;
        *(uint32_t *) cur_p = chunk_id - 64;
        cur_p += 2;
    }
    else
    {
        WARNING("chunk id is too big. id: %lu", chunk_id);
        return;
    }

    //根据fmt写不同类型的chunk头信息
    if (fmt < 3)
    {
        if (timestamp >= 16777215)
        {
            need_ext_timestamp = true;
            *(uint32_t *) cur_p = 0xFFFFFF;
            cur_p += 3;
        }
        else
        {
            temp_p = (uint8_t *) &timestamp;
            *cur_p++ = temp_p[2];
            *cur_p++ = temp_p[1];
            *cur_p++ = temp_p[0];
        }
    }
    if (fmt < 2)
    {
        temp_p = (uint8_t *) &msg_len;
        *cur_p++ = temp_p[2];
        *cur_p++ = temp_p[1];
        *cur_p++ = temp_p[0];

        *cur_p++ = type_id;
    }
    if (fmt < 1)
    {
        *(uint32_t *) cur_p = msg_id;
        cur_p += 4;
    }

    //写额外的扩展时间戳
    if (need_ext_timestamp)
    {
        temp_p = (uint8_t *) &timestamp;
        *cur_p++ = temp_p[3];
        *cur_p++ = temp_p[2];
        *cur_p++ = temp_p[1];
        *cur_p++ = temp_p[0];
    }

    b->header_length = cur_p - b->header_start_p;
    b->header_recv_len = b->header_length;
    b->chunk_id = chunk_id;
    b->format = fmt;

    return;
}

void encode_buf_to_chunk_chain(chunk_chain_t &chain, uint8_t *buf, size_t len,
        uint32_t sid, uint32_t ts, uint8_t type, uint32_t chunk_id)
{
    chunk_bucket_t *chunk;
    size_t remain_len = len;

    chunk = alloc_chunk_bucket();
    wrap_chunk_header(chunk, 0, chunk_id, ts, len, type, sid);

    ngx_queue_insert_head(&chain.queue, &chunk->queue);

    /**
     * 判断是否需要将msg拆分成多个chunk
     * 因为一个chunk的大小为固定的128字节，当msg的payload过大时，需要拆分
     */
    memcpy(chunk->payload_start_p, buf,
            bls_simple_min(remain_len, DEFAULT_CHUNK_BUCKET_SIZE));

    chunk->payload_length
            = bls_simple_min(remain_len, DEFAULT_CHUNK_BUCKET_SIZE);

    while (remain_len > DEFAULT_CHUNK_BUCKET_SIZE)
    {
        remain_len -= DEFAULT_CHUNK_BUCKET_SIZE;

        DEBUG_LOG("encode one more chunk for this message. remain_len: %u len: %u",
                remain_len, len);

        //一个message拆分成多个chunk，除了第一个chunk，后续的chunk的fmt都是3
        chunk = alloc_chunk_bucket();
        wrap_chunk_header(chunk, 3, chunk_id);
        memcpy(chunk->payload_start_p, buf + len - remain_len,
                bls_simple_min(remain_len, DEFAULT_CHUNK_BUCKET_SIZE));
        chunk->payload_length
                = bls_simple_min(remain_len, DEFAULT_CHUNK_BUCKET_SIZE);

        ngx_queue_insert_tail(&chain.queue, &chunk->queue);
    }

    return;
}

void write_chunk_chain(RtmpClient *client, chunk_chain_t &chain, bool auto_free)
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *b = NULL;

    CLIENT_TRACE(client, "write chunk chain to client. ");

    ngx_queue_foreach(q, &(chain.queue))
    {
        b = ngx_queue_data(q, chunk_bucket_t, queue);
        rtmp_write_chunk(client->bls_socket, b);
    }

    if (auto_free)
    {
        CLIENT_TRACE(client, "free chunk when finish send");
        ngx_queue_foreach(q, &(chain.queue))
        {
            if (NULL != b)
            {
                free_chunk_bucket(b);
            }
            b = ngx_queue_data(q, chunk_bucket_t, queue);
        }
        if (NULL != b)
        {
            free_chunk_bucket(b);
        }

        ngx_queue_init(&(chain.queue));
    }

    return;
}

void write_chunk_chain_payload(RtmpClient *client, chunk_chain_t &chain,
        bool auto_free)
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *b = NULL;
    bool is_first = true;

    CLIENT_TRACE(client, "write chunk chain payload to client. ");

    ngx_queue_foreach(q, &(chain.queue))
    {
        b = ngx_queue_data(q, chunk_bucket_t, queue);
        if (is_first)
        {
            is_first = false;
            rtmp_write_chunk_payload(client->bls_socket, b);
        }
        else
        {
            rtmp_write_chunk(client->bls_socket, b);
        }
    }

    if (auto_free)
    {
        CLIENT_TRACE(client, "free chunk when finish send");
        ngx_queue_foreach(q, &(chain.queue))
        {
            if (NULL != b)
            {
                free_chunk_bucket(b);
            }
            b = ngx_queue_data(q, chunk_bucket_t, queue);
        }
        if (NULL != b)
        {
            free_chunk_bucket(b);
        }

        ngx_queue_init(&(chain.queue));
    }

    return;
}
