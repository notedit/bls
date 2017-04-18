/*
 * BlsSource.cpp
 *
 *  Created on: 2014-11-11
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "BlsSource.h"
#include <utilities.h>
#include <map>
#include <string.h>
#include <BlsMessage.h>
#include <nan.h>
//#include <v8.h>

using namespace std;

ngx_queue_t g_source_pool;
std::map<std::string, source_bucket_t *> g_source_map;

BlsSource::BlsSource(uint32_t video, uint32_t audio)
{
    video_chunkid = video;
    audio_chunkid = audio;

    metadata_len = 0;
    abs_record_time = 0;
    time_index = 0;

    video_time = 0;
    audio_time = 0;

    is_publishing = false;

    ngx_queue_init(&consumer_queue);

    key_cache.source = (void *) this;
}

BlsSource::~BlsSource()
{

}

void BlsSource::add_consumer(bls_consumer_t *consumer)
{
    consumer->is_alive = true;
    ngx_queue_insert_tail(&consumer_queue, &consumer->queue);

    if (NULL != key_cache.gop && NULL != key_cache.metadata && NULL
            != key_cache.video_sh)
    {
        consumer->state = CONSUMER_WAIT_AUDIO_SH;

        ngx_queue_t *temp_q = ngx_queue_head(&key_cache.metadata->chain.queue);
        chunk_bucket_t *meta_chunk =
                ngx_queue_data(temp_q, chunk_bucket_t, queue);
        chunk_chain_t meta_data_chain;
        BlsSource *source = (BlsSource *) key_cache.source;

        //发送视频metadata
        CLIENT_TRACE(consumer->client, "wrap metadata and send."
                "msg_len: %u stream_id: %u",
                key_cache.metadata->header.msg_len, consumer->stream_id);
        wrap_chunk_header(consumer->metadata_chunk_header, 0,
                meta_chunk->chunk_id, 0, key_cache.metadata->header.msg_len,
                RTMP_MSG_AMF0DataMessage, consumer->stream_id);
        rtmp_write_chunk_header(consumer->client->bls_socket,
                consumer->metadata_chunk_header);

        CLIENT_TRACE(consumer->client, "send metadata with absTime: %.2f",
                key_cache.gop->header.timestamp + source->abs_record_time);

        source->update_abs_record_time(key_cache.gop->header.timestamp);
        encode_buf_to_chunk_chain(meta_data_chain,
                (uint8_t *) source->metadata_buf, source->metadata_len,
                consumer->stream_id, 0, 0, meta_chunk->chunk_id);

        write_chunk_chain_payload(consumer->client, meta_data_chain, false);

        //发送video sequence header
        CLIENT_TRACE(consumer->client, "wrap video sequence header and send."
                "msg_len: %u stream_id: %u",
                key_cache.video_sh->header.msg_len, consumer->stream_id);
        wrap_chunk_header(consumer->first_video_chunk_header, 0,
                consumer->video_chunk_id, 0,
                key_cache.video_sh->header.msg_len, RTMP_MSG_VideoMessage,
                consumer->stream_id);
        rtmp_write_chunk_header(consumer->client->bls_socket,
                consumer->first_video_chunk_header);
        write_chunk_chain_payload(consumer->client, key_cache.video_sh->chain,
                false);

        if (NULL != key_cache.audio_sh)
        {
            consumer->state = CONSUMER_RUN;

            //发送audio sequence header
            CLIENT_TRACE(consumer->client, "wrap audio sequence header and send."
                    "msg_len: %u stream_id: %u",
                    key_cache.audio_sh->header.msg_len, consumer->stream_id);

            wrap_chunk_header(consumer->first_audio_chunk_header, 0,
                    consumer->audio_chunk_id, 0,
                    key_cache.audio_sh->header.msg_len, RTMP_MSG_AudioMessage,
                    consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_audio_chunk_header);
            write_chunk_chain_payload(consumer->client,
                    key_cache.audio_sh->chain, false);

        }

        //发送gop
        CLIENT_TRACE(consumer->client, "send gop to client.");
        wrap_chunk_header(consumer->first_key_fram_chunk_header, 1,
                consumer->video_chunk_id, 0, key_cache.gop->header.msg_len,
                RTMP_MSG_VideoMessage, consumer->stream_id);
        rtmp_write_chunk_header(consumer->client->bls_socket,
                consumer->first_key_fram_chunk_header);
        write_chunk_chain_payload(consumer->client, key_cache.gop->chain, false);
    }
}

void BlsSource::on_av_chunk(rtmp_channel_t *channel, chunk_bucket_t *chunk)
{
    ngx_queue_t *q = NULL;

    uint32_t chunk_id;
    uint32_t timestamp = 0;

    if (channel->header.type == RTMP_MSG_AudioMessage)
    {
        chunk_id = audio_chunkid;
        timestamp = channel->header.timestamp > audio_time ?
                channel->header.timestamp - audio_time : 0;

        audio_time = channel->header.timestamp;
    }
    else
    {
        chunk_id = video_chunkid;
        timestamp = channel->header.timestamp > video_time ?
                channel->header.timestamp - video_time : 0;

        video_time = channel->header.timestamp;
    }

    //将chunk的id改成对应的chunkid
    if (chunk->format != RTMP_CHUNK_FMT_TYPE3)
    {
        wrap_chunk_header(chunk, 1, chunk_id, timestamp,
                channel->header.msg_len, channel->header.type);
    }
    else
    {
        wrap_chunk_header(chunk, 3, chunk_id);
    }

    //将chunk转发给客户端
    q = NULL;
    bls_consumer_t *c = NULL;
    ngx_queue_foreach(q, &consumer_queue)
    {
        c = ngx_queue_data(q, bls_consumer_t, queue);

        if (channel->header.type == RTMP_MSG_AudioMessage)
        {
            consumer_send_audio(c, chunk, &channel->header, &key_cache);
        }
        else
        {
            consumer_send_video(c, chunk, &channel->header, &key_cache);
        }
    }

    return;
}

void BlsSource::on_video_msg(rtmp_channel_t *channel)
{
    ngx_queue_t *q = ngx_queue_head(&channel->chain.queue);
    chunk_bucket_t *chunk = ngx_queue_data(q, chunk_bucket_t, queue);

    if (chunk_is_video_sh(chunk))
    {
        SYS_DEBUG("cache video sh for %s", stream_name.c_str());

        bls_delete(key_cache.video_sh);

        key_cache.video_sh = new rtmp_channel_t();
        key_cache.video_sh->header = channel->header;
        ngx_queue_add(&key_cache.video_sh->chain.queue, &channel->chain.queue);
        ngx_queue_init(&channel->chain.queue);
    }

    add_msg_to_gop(channel);

    return;
}

void BlsSource::on_audio_msg(rtmp_channel_t *channel)
{
    ngx_queue_t *q = ngx_queue_head(&channel->chain.queue);
    chunk_bucket_t *chunk = ngx_queue_data(q, chunk_bucket_t, queue);

    if (chunk_is_audio_sh(chunk))
    {
        SYS_DEBUG("cache audio sh for %s", stream_name.c_str());

        bls_delete(key_cache.audio_sh);

        key_cache.audio_sh = new rtmp_channel_t();
        key_cache.audio_sh->header = channel->header;
        ngx_queue_add(&key_cache.audio_sh->chain.queue, &channel->chain.queue);
        ngx_queue_init(&channel->chain.queue);
    }

    add_msg_to_gop(channel);

    return;
}

int BlsSource::copy_aac_sh_data(uint8_t *target)
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *chunk = NULL;
    size_t copyed_size = 0;

    if (!key_cache.audio_sh)
    {
        return -1;
    }

    ngx_queue_foreach(q, &key_cache.audio_sh->chain.queue)
    {
        chunk = ngx_queue_data(q, chunk_bucket_t, queue);

        memcpy(target + copyed_size, chunk->payload_start_p, chunk->payload_recv_len);
        copyed_size += chunk->payload_recv_len;
    }

    return copyed_size;
}

int BlsSource::copy_avc_sh_data(uint8_t *target)
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *chunk = NULL;
    size_t copyed_size = 0;

    if (!key_cache.video_sh)
    {
        return -1;
    }

    ngx_queue_foreach(q, &key_cache.video_sh->chain.queue)
    {
        chunk = ngx_queue_data(q, chunk_bucket_t, queue);

        memcpy(target + copyed_size, chunk->payload_start_p, chunk->payload_recv_len);
        copyed_size += chunk->payload_recv_len;
    }

    return copyed_size;
}

void BlsSource::add_msg_to_gop(rtmp_channel_t *channel)
{
    ngx_queue_t *q = ngx_queue_head(&channel->chain.queue);
    chunk_bucket_t *chunk = ngx_queue_data(q, chunk_bucket_t, queue);

    if (chunk_is_keyframe(chunk) && !chunk_is_video_sh(chunk))
    {
        clear_gop();

        key_cache.gop = new rtmp_channel_t();
        key_cache.gop->header = channel->header;
        ngx_queue_add(&key_cache.gop->chain.queue, &channel->chain.queue);
        ngx_queue_init(&channel->chain.queue);
    }
    else
    {
        if (NULL != key_cache.gop)
        {
            ngx_queue_add(&key_cache.gop->chain.queue, &channel->chain.queue);
            ngx_queue_init(&channel->chain.queue);

            key_cache.gop_counter++;

            //限制gop的大小
            if (key_cache.gop_counter > MAX_GOP_LEN)
            {
                clear_gop();
            }
        }
    }
}

void BlsSource::clear_gop()
{
    bls_delete(key_cache.gop);
    key_cache.gop_counter = 0;
}

void BlsSource::find_abs_time_in_metadata()
{
    int64_t temp_time;

    for (size_t i = 0; i < metadata_len - 13; i++)
    {
        if (!memcmp(metadata_buf + i, "absRecordTime", 13))
        {
            time_index = i + 14;
            temp_time = read_8bytes((uint8_t *) (metadata_buf + i + 14));
            memcpy(&abs_record_time, &temp_time, 8);

            NOTICE("get abs record time in meta data for %s. %.2f",
                    stream_name.c_str(), abs_record_time);

            break;
        }
    }

    if (time_index == 0)
    {
        WARNING("can not find abs record time in meta data for %s",
                stream_name.c_str());
    }
}

void BlsSource::on_metadata(rtmp_channel_t *channel, uint8_t *buffer)
{
    SYS_TRACE("cache metadata sh for %s", stream_name.c_str());

    if (NULL != key_cache.metadata)
    {
        bls_delete(key_cache.metadata);
    }

    //接管channel的内容，防止被回收
    key_cache.metadata = new rtmp_channel_t();
    key_cache.metadata->header = channel->header;
    ngx_queue_add(&key_cache.metadata->chain.queue, &channel->chain.queue);
    ngx_queue_init(&channel->chain.queue);

    if (NULL != buffer)
    {
        //处理metadata中的setDataFrame字段
        if (!memcmp(buffer + 3, "@setDataFrame", 13))
        {
            metadata_len = channel->header.msg_len - 16;
            memcpy((void *) metadata_buf, (void *) (buffer + 16), metadata_len);

            key_cache.metadata->header.msg_len = metadata_len;
        }
        else
        {
            metadata_len = channel->header.msg_len;
            memcpy((void *) metadata_buf, (void *) buffer, metadata_len);
        }

        //处理metadata里的absrecordtime
        find_abs_time_in_metadata();
    }
}

void BlsSource::update_abs_record_time(size_t time_add)
{
    double t = time_add + abs_record_time;
    int64_t tt = 0x00;
    uint8_t *p = (uint8_t *) (metadata_buf + time_index);

    if (time_index != 0)
    {
        memcpy(&tt, &t, 8);
        write_8bytes(p, tt);
    }
}

void BlsSource::clear_consumer()
{
    ngx_queue_t *q;
    bls_consumer_t *c;

    v8::Handle<v8::Value> argv[] = { Nan::New("play_stop_event").ToLocalChecked(),
            Nan::New(this->stream_name.c_str()).ToLocalChecked() };

    ngx_queue_foreach(q, &consumer_queue)
    {
        c = ngx_queue_data(q, bls_consumer_t, queue);

        c->is_alive = false;

        CLIENT_TRACE(c->client, "notify this client play stop");

        //notify client in nodejs stream unpublish
        
        Nan::HandleScope scope;
        c->client->node_on_msg_cb->Call(2, argv);
    }

    ngx_queue_init(&consumer_queue);

    bls_delete(key_cache.metadata);
    bls_delete(key_cache.video_sh);
    bls_delete(key_cache.audio_sh);
    bls_delete(key_cache.key_frame);
    clear_gop();

    abs_record_time = 0;
    video_time = 0;
    audio_time = 0;

    is_publishing = false;
}

bool BlsSource::consumer_is_empty()
{
    return ngx_queue_empty(&consumer_queue);
}

void change_channel_chunkid(rtmp_channel_t *channel, uint32_t new_id)
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *b = NULL;
    uint32_t time_value = 0;

    SYS_DEBUG("wrap channel chunkid to %u", new_id);

    ngx_queue_foreach(q, &(channel->chain.queue))
    {
        b = ngx_queue_data(q, chunk_bucket_t, queue);

        if (b->format == RTMP_CHUNK_FMT_TYPE0)
            time_value = channel->header.timestamp;
        else
            time_value = channel->header.delta_timestamp;

        wrap_chunk_header(b, b->format, new_id, time_value,
                channel->header.msg_len, channel->header.type,
                channel->header.stream_id);
    }

    return;
}

void init_source_pool()
{
    ngx_queue_init(&g_source_pool);

    uint32_t start_chunk_id = 64;
    source_bucket_t *s;

    for (int i = 0; i < DEFAULT_SOURCE_POOL_SIZE; i++)
    {
        s = new source_bucket_t();
//        s->source = new BlsSource(start_chunk_id, start_chunk_id + 1);
        s->source = new BlsSource(12, 13);
        ngx_queue_insert_tail(&g_source_pool, &s->queue);

        start_chunk_id += 2;
    }

    return;
}

source_bucket_t *apply_source()
{
    if (ngx_queue_empty(&g_source_pool))
    {
        assert(0);
        return NULL;
    }

    ngx_queue_t *q = ngx_queue_head(&g_source_pool);
    source_bucket_t *s = ngx_queue_data(q, source_bucket_t, queue);
    ngx_queue_remove(&s->queue);

    return s;
}

void collect_source(source_bucket_t *source)
{
    SYS_TRACE("collect publish source back to pool. stream_name: %s",
            source->source->stream_name.c_str());

    source->source->clear_consumer();

    g_source_map.erase(source->source->stream_name);
    ngx_queue_insert_head(&g_source_pool, &source->queue);

    return;
}

source_bucket_t *get_publish_source(std::string stream_name)
{
    std::map<string, source_bucket_t*>::iterator c;
    source_bucket_t *s = NULL;

    c = g_source_map.find(stream_name);
    if (c != g_source_map.end())
    {
        SYS_WARNING("hehe! find old source in g_map! stream: %s",
                stream_name.c_str());

        return NULL;
    }
    else
    {
        s = apply_source();

        if (NULL == s)
            return NULL;

        g_source_map.insert(pair<string, source_bucket_t*> (stream_name, s));
        s->source->stream_name = stream_name;
    }

    SYS_TRACE("get publish source from pool. stream_name: %s",
            stream_name.c_str());

    s->source->is_publishing = true;

    return s;
}

source_bucket_t *get_play_source(string stream_name)
{
    std::map<string, source_bucket_t*>::iterator c;

    c = g_source_map.find(stream_name);
    if (c != g_source_map.end())
    {
        SYS_TRACE("get play source from pool. stream_name: %s",
                stream_name.c_str());
        return c->second;
    }
    else
    {
        SYS_TRACE("there is no source for %s to play",
                stream_name.c_str());
        return NULL;
    }
}
