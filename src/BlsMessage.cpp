/*
 * BlsMessage.cpp
 *
 *  Created on: 2014-10-31
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "BlsMessage.h"
#include <BlsLogger.h>
#include <utilities.h>
#include <map>
#include <stdlib.h>
//#include <node.h>
//#include <v8.h>
#include <nan.h>
#include <uv-private/ngx-queue.h>
#include <string>

using namespace v8;
using Nan::New;

#define MAX_CHUNK_TYPE 50
static message_worker_t TYPE_MESSAGE_WORKER_MAP[MAX_CHUNK_TYPE];

#define _TYPE_TO_WORKER(type, w) \
    do{             \
        temp_w.decode = msg_##w##_decode; \
        temp_w.available = true; \
        TYPE_MESSAGE_WORKER_MAP[type] = temp_w; \
    }while(0)

void init_type_message_map()
{
    message_worker_t temp_w = { NULL, false };
    for (int i = 0; i < MAX_CHUNK_TYPE; ++i)
    {
        TYPE_MESSAGE_WORKER_MAP[i] = temp_w;
    }

    /**
     * 不同类型的message对应的type id，如果对应的处理函数不存在，则忽略该消息
     */
    _TYPE_TO_WORKER(RTMP_MSG_SetChunkSize, set_chunk_size);
    //    _TYPE_TO_WORKER(2, abort_message);
    //    _TYPE_TO_WORKER(3, acknowledgement);
    _TYPE_TO_WORKER(RTMP_MSG_WindowAcknowledgementSize, window_acknowledgement_size);
    //    _TYPE_TO_WORKER(6, set_peer_bandwidth);
    _TYPE_TO_WORKER(RTMP_MSG_UserControlMessage, user_control_message);
    _TYPE_TO_WORKER(RTMP_MSG_AMF0CommandMessage, command_message_amf0);
    //    _TYPE_TO_WORKER(17, command_message_amf3);
    _TYPE_TO_WORKER(RTMP_MSG_AMF0DataMessage, msg_data_message_amf0);
    //    _TYPE_TO_WORKER(15, msg_data_message_amf3);
    //    _TYPE_TO_WORKER(19, share_object_message_amf0);
    //    _TYPE_TO_WORKER(16, share_object_message_amf3);
    _TYPE_TO_WORKER(RTMP_MSG_AudioMessage, audio);
    _TYPE_TO_WORKER(RTMP_MSG_VideoMessage, video);
    //    _TYPE_TO_WORKER(22, aggregate_message);
}

#undef _TYPE_TO_WORKER

void msg_decode(RtmpProtocol *protocol)
{
    message_worker_t worker;
    bls_message_t msg;

    //先接管protocol里的channel
    msg.protocol = protocol;
    msg.channel.header = protocol->current_channel->header;
    ngx_queue_add(&msg.channel.chain.queue, &protocol->current_channel->chain.queue);
    ngx_queue_init(&protocol->current_channel->chain.queue);

    if (protocol->current_channel->header.type >= MAX_CHUNK_TYPE)
    {
        CLIENT_WARNING(protocol->client, "this message type is too big");
        return;
    }

    worker = TYPE_MESSAGE_WORKER_MAP[protocol->current_channel->header.type];

    if (!worker.available)
    {
        CLIENT_WARNING(protocol->client, "ignore this message. type: 0x%2x",
                protocol->current_channel->header.type);
    }
    else
    {
        worker.decode(msg);
    }

    return;
}

void msg_command_message_amf0_decode(bls_message_t &msg)
{
    if (msg.channel.header.msg_len > msg.protocol->client->node_buf_len)
    {
        CLIENT_WARNING(msg.protocol->client,
                "this message len: %lu exceed the max buffer len. max: %lu",
                msg.channel.header.msg_len, msg.protocol->client->node_buf_len);

        msg.protocol->client->close();
        return;
    }

    ngx_queue_t *q = NULL;
    chunk_bucket_t *b = NULL;
    size_t buf_flag = 0;

    //拷贝chunk中的数据到buffer里
    ngx_queue_foreach(q, &msg.channel.chain.queue)
    {
        b = ngx_queue_data(q, chunk_bucket_t, queue);
#ifdef NODE_EXTEND
        memcpy(msg.protocol->client->node_read_buf + buf_flag,
                b->payload_start_p, b->payload_recv_len);
#endif
        buf_flag += b->payload_recv_len;
    }

//#ifdef NODE_EXTEND
    Nan::HandleScope scope;

    Handle<Value> argv[] =
    {
        Nan::New<Number>(msg.channel.header.stream_id),
        Nan::New<Boolean>(true),
        Nan::New<Number>(buf_flag)
    };

    //回调nodejs里的函数，在nodejs中处理AMF
    msg.protocol->client->node_on_msg_cb->Call(3, argv);
    //Nan::MakeCallback(Nan::GetCurrentContext()->Global(), msg.protocol->client->node_on_msg_cb, 3, argv);
    //msg.protocol->client->node_on_msg_cb->Call(v8::Context::GetCurrent()->Global(), 3, argv);
//#endif
}

void msg_window_acknowledgement_size_decode(bls_message_t &msg)
{
    ngx_queue_t *q = ngx_queue_head(&msg.channel.chain.queue);
    chunk_bucket_t *b = ngx_queue_data(q, chunk_bucket_t, queue);

    msg.protocol->window_size = read_4bytes(b->payload_start_p);

    CLIENT_DEBUG(msg.protocol->client, "peer set window size %u success",
            msg.protocol->window_size);
}

void msg_set_chunk_size_decode(bls_message_t &msg)
{
    uint32_t new_size;
    ngx_queue_t *q = ngx_queue_head(&msg.channel.chain.queue);
    chunk_bucket_t *b = ngx_queue_data(q, chunk_bucket_t, queue);

    new_size = read_4bytes(b->payload_start_p);

    if (new_size == 0 ||
            (new_size > DEFAULT_CHUNK_BUCKET_SIZE && new_size % DEFAULT_CHUNK_BUCKET_SIZE) ||
            (new_size < DEFAULT_CHUNK_BUCKET_SIZE && DEFAULT_CHUNK_BUCKET_SIZE % new_size))
    {
        CLIENT_NOTICE(msg.protocol->client,
                "recv set chunk size msg %u, close it!", new_size);
        msg.protocol->client->close();
    }
    else
    {
        CLIENT_NOTICE(msg.protocol->client,
                "recv set chunk size msg %u, do it!", new_size);
        msg.protocol->client->chunk_size = new_size;
    }

    return;
}

void msg_user_control_message_decode(bls_message_t &msg)
{
    ngx_queue_t *q = ngx_queue_head(&msg.channel.chain.queue);
    chunk_bucket_t *b = ngx_queue_data(q, chunk_bucket_t, queue);

    uint16_t event_type = read_2bytes(b->payload_start_p);

    CLIENT_TRACE(msg.protocol->client, "get user control msg. type: %u",event_type);
    switch (event_type)
    {
    case 6:
        CLIENT_TRACE(msg.protocol->client, "get ping request, return pong response");
        msg.protocol->send_pong_response(read_4bytes(b->payload_start_p + 2));
        break;

    case 7:
        {
            CLIENT_TRACE(msg.protocol->client, "get ping response, set pong_get to true");
            msg.protocol->pong_get = true;

#ifdef NODE_EXTEND
            Nan::HandleScope scope;

            Handle<Value> argv[] =
            {
                New("ping_pong_request").ToLocalChecked(),
                New<Number>(0),
                New<Number>(msg.protocol->total_recved_size)};

            //回调nodejs里的函数，在nodejs中处理AMF
            msg.protocol->client->node_on_msg_cb->Call(3, argv);
            //Nan::MakeCallback(Nan::GetCurrentContext()->Global(), msg.protocol->client->node_on_msg_cb, 3, argv);
            //msg.protocol->client->node_on_msg_cb->Call(v8::Context::GetCurrent()->Global(), 3, argv);
#endif
        }
        break;

    default:
        CLIENT_DEBUG(msg.protocol->client, "ignore this type of user control");
    }
}

/**
 * 向nodejs反馈音视频数据，将数据拷贝到client的avbuffer中
 * @param client 客户端实例
 * @param msg 完整音视频数据
 */
void throwup_av_info(RtmpClient *client, bls_message_t *msg)
{
    ngx_queue_t *q = ngx_queue_head(&msg->channel.chain.queue);
    chunk_bucket_t *head_chunk = ngx_queue_data(q, chunk_bucket_t, queue);
    chunk_bucket_t *chunk = NULL;
    const char *av_type = NULL;
    bool is_sh = false;
    bool is_key = false;

    if (msg->channel.header.type == RTMP_MSG_AudioMessage)
    {
        av_type = "audio";
        is_sh = chunk_is_audio_sh(head_chunk);
    }
    else if (msg->channel.header.type == RTMP_MSG_VideoMessage)
    {
        av_type = "video";
        is_sh = chunk_is_video_sh(head_chunk);
        is_key = chunk_is_keyframe(head_chunk);
    }
    else
    {
        return;
    }

    Nan::HandleScope scope;

    Handle<Value> argv[] =
    {
        New(av_type).ToLocalChecked(),           //av类型
        New<Boolean>(is_key),         //是否是关键帧
        New<Boolean>(is_sh),         //是否是avc sh
        New<Number>(msg->channel.header.timestamp),           //时间戳
        New<Number>(msg->channel.header.msg_len),             //数据大小
    };

    //拷贝视频数据
    size_t copyed_size = 0;
    ngx_queue_foreach(q, &msg->channel.chain.queue)
    {
        chunk = ngx_queue_data(q, chunk_bucket_t, queue);

        memcpy(client->node_av_buf + copyed_size, chunk->payload_start_p, chunk->payload_recv_len);
        copyed_size += chunk->payload_recv_len;
    }

    //回调nodejs里的函数，在nodejs中处理AMF
    client->node_on_av_cb->Call(5, argv);
    //Nan::MakeCallback(Nan::GetCurrentContext()->Global(), client->node_on_av_cb, 5, argv);
    //client->node_on_av_cb->Call(v8::Context::GetCurrent()->Global(), 5, argv);

    return;
}

void msg_video_decode(bls_message_t &msg)
{
    ngx_queue_t *q = ngx_queue_head(&msg.channel.chain.queue);
    chunk_bucket_t *head_chunk = ngx_queue_data(q, chunk_bucket_t, queue);
    chunk_bucket_t *chunk = NULL;

    msg.protocol->total_recved_video_msg ++;

    if (chunk_is_keyframe(head_chunk))
    {
        CLIENT_TRACE(msg.protocol->client,
                "get a video frame. is_sh: %s ts: %u size: %u"
                " total_size: %lu total_video_msg: %lu total_audio_msg: %lu",
                chunk_is_video_sh(head_chunk) ? "true" : "false",
                msg.channel.header.timestamp, msg.channel.header.msg_len,
                msg.protocol->total_recved_size, msg.protocol->total_recved_video_msg,
                msg.protocol->total_recved_audio_msg);
    }
    else
    {
        CLIENT_DEBUG(msg.protocol->client,
                "get a video frame. ts: %u size: %u",
                msg.channel.header.timestamp, msg.channel.header.msg_len);
    }

    if (NULL != msg.protocol->publish_source)
    {
        //向nodejs上抛视频数据用于websocket分发
        if (msg.protocol->client->enable_video_up)
        {
            throwup_av_info(msg.protocol->client, &msg);
        }

        ngx_queue_foreach(q, &msg.channel.chain.queue)
        {
            chunk = ngx_queue_data(q, chunk_bucket_t, queue);
            msg.protocol->publish_source->source->on_av_chunk(&msg.channel, chunk);
        }

        msg.protocol->publish_source->source->on_video_msg(&msg.channel);
    }

    return;
}

void msg_audio_decode(bls_message_t &msg)
{
    ngx_queue_t *q = ngx_queue_head(&msg.channel.chain.queue);
    chunk_bucket_t *chunk = ngx_queue_data(q, chunk_bucket_t, queue);

    msg.protocol->total_recved_audio_msg ++;

    if (chunk_is_audio_sh(chunk))
    {
        CLIENT_TRACE(msg.protocol->client,
                "get audio sequence header. ts: %u size: %u data: %x",
                msg.channel.header.timestamp, msg.channel.header.msg_len,
                *chunk->payload_start_p);
    }
    else
    {
        CLIENT_DEBUG(msg.protocol->client,
                "get audio frame. ts: %u size: %u data: %x",
                msg.channel.header.timestamp, msg.channel.header.msg_len,
                *chunk->payload_start_p);
    }

    if (NULL != msg.protocol->publish_source)
    {
        //向nodejs上抛音频数据用于websocket分发
        if (msg.protocol->client->enable_video_up)
        {
            throwup_av_info(msg.protocol->client, &msg);
        }

        ngx_queue_foreach(q, &msg.channel.chain.queue)
        {
            chunk = ngx_queue_data(q, chunk_bucket_t, queue);
            msg.protocol->publish_source->source->on_av_chunk(&msg.channel, chunk);
        }

        msg.protocol->publish_source->source->on_audio_msg(&msg.channel);
    }

    return;
}

void msg_msg_data_message_amf0_decode(bls_message_t &msg)
{
    RtmpClient *client = msg.protocol->client;

    CLIENT_NOTICE(msg.protocol->client, "get a metadata. ts: %u size: %u",
            msg.channel.header.timestamp, msg.channel.header.msg_len);

    //反馈给nodejs,同时拿到metadata的完整buffer
    msg_command_message_amf0_decode(msg);

    if (NULL != msg.protocol->publish_source &&
            msg.channel.header.msg_len < msg.protocol->client->node_buf_len)
    {
        msg.protocol->publish_source->source->on_metadata(&msg.channel,
                client->node_read_buf);
    }

    return;
}
