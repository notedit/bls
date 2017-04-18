/*
 * BlsConsumer.cpp
 *
 *  Created on: 2014-11-9
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "BlsConsumer.h"
#include <BlsLogger.h>
#include <RtmpProtocol.h>
#include <utilities.h>
#include <BlsMessage.h>

bls_consumer_t* init_consumer(RtmpClient *client, std::string stream_name,
        uint32_t stream_id, uint32_t video_chunk_id, uint32_t audio_chunk_id)
{
    CLIENT_TRACE(client, "init consumer. stream_id: %u video_chunk_id: %u, audio_chunk_id: %u",
            stream_id, video_chunk_id, audio_chunk_id);

    bls_consumer_t *consumer = new bls_consumer_t();

    consumer->is_alive = false;

    consumer->client = client;
    consumer->stream_id = stream_id;
    consumer->audio_chunk_id = audio_chunk_id;
    consumer->video_chunk_id = video_chunk_id;
    consumer->state = CONSUMER_WAIT_METADATA;

    consumer->stream_name = new std::string(stream_name);

    consumer->first_video_chunk_header = alloc_chunk_bucket();
    consumer->first_audio_chunk_header = alloc_chunk_bucket();
    consumer->first_key_fram_chunk_header = alloc_chunk_bucket();
    consumer->metadata_chunk_header = alloc_chunk_bucket();
    consumer->first_audio2_chunk_header = alloc_chunk_bucket();

    consumer->chunk_in_buffer_num = 0;

    ngx_queue_init(&consumer->queue);

    return consumer;
}

void free_consumer(bls_consumer_t *consumer)
{
    CLIENT_TRACE(consumer->client, "free consumer");

    free_chunk_bucket(consumer->first_audio_chunk_header);
    free_chunk_bucket(consumer->first_video_chunk_header);
    free_chunk_bucket(consumer->first_key_fram_chunk_header);
    free_chunk_bucket(consumer->metadata_chunk_header);
    free_chunk_bucket(consumer->first_audio2_chunk_header);

    if (consumer->is_alive)
    {
        CLIENT_TRACE(consumer->client, "remove consumer from source");
        ngx_queue_remove(&consumer->queue);
    }
    bls_delete(consumer->stream_name);
    bls_delete(consumer);
    return;
}

void consumer_send_video(bls_consumer_t* consumer, chunk_bucket_t *chunk,
        chunk_header_t *chunk_header, av_buffer_t *buffer)
{
    CLIENT_DEBUG(consumer->client, "try to dispatch video chunk to client."
            " state: %d buffer_chunk: %u chunk_stream_name: %s source_stream_name: %s",
            consumer->state, consumer->chunk_in_buffer_num,
            chunk->source_stream_name->c_str(), consumer->stream_name->c_str());

    assert(*consumer->stream_name == *chunk->source_stream_name);

    switch (consumer->state)
    {
    //直接转发视频帧
    case CONSUMER_RUN:
    case CONSUMER_WAIT_AUDIO_SH:
        CLIENT_DEBUG(consumer->client, "dispatch video chunk to client. type: 0x%2x ts: %u",
                chunk_header->type, chunk_header->timestamp);

        if (chunk->format != RTMP_CHUNK_FMT_TYPE3
                && consumer->chunk_in_buffer_num > MAX_CHUNK_BUFFER_LEN)
        {
            consumer->state = CONSUMER_WAIT_VIDEO_SLOW_SPEED;

            CLIENT_WARNING(consumer->client, "this is a slow player. stop dispatch data. "
                    "state: %d", consumer->state);
            return;
        }

        if (chunk_is_keyframe(chunk))
        {
            CLIENT_TRACE(consumer->client, "dispatch key video frame to client. type: 0x%2x ts: %u len: %u",
                    chunk_header->type, chunk_header->timestamp, chunk_header->msg_len);
        }

        rtmp_write_chunk(consumer->client->bls_socket, chunk,
                consumer_write_chunk_cb, consumer->client->protocol);

        consumer->chunk_in_buffer_num++;
        return;

        //开始转发视频帧
    case CONSUMER_WAIT_METADATA:
        if (chunk_is_keyframe(chunk) && NULL != buffer->metadata && NULL != buffer->video_sh)
        {
            ngx_queue_t *temp_q =
                    ngx_queue_head(&buffer->metadata->chain.queue);
            chunk_bucket_t *meta_chunk =
                    ngx_queue_data(temp_q, chunk_bucket_t, queue);
            chunk_chain_t meta_data_chain;
            BlsSource *source = (BlsSource *) buffer->source;

            //发送视频metadata
            CLIENT_TRACE(consumer->client, "wrap metadata and send."
                    "msg_len: %u stream_id: %u",
                    buffer->metadata->header.msg_len, consumer->stream_id);
            wrap_chunk_header(consumer->metadata_chunk_header, 0,
                    meta_chunk->chunk_id, 0, buffer->metadata->header.msg_len,
                    RTMP_MSG_AMF0DataMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->metadata_chunk_header);

            CLIENT_TRACE(consumer->client, "send metadata with absTime: %.2f",
                    chunk_header->timestamp + source->abs_record_time);

            source->update_abs_record_time(chunk_header->timestamp);
            encode_buf_to_chunk_chain(meta_data_chain,
                    (uint8_t *) source->metadata_buf, source->metadata_len,
                    consumer->stream_id, 0, 0, meta_chunk->chunk_id);

            write_chunk_chain_payload(consumer->client, meta_data_chain, false);

            //发送video sequence header
            CLIENT_TRACE(consumer->client, "wrap video sequence header and send."
                    "msg_len: %u stream_id: %u",
                    buffer->video_sh->header.msg_len, consumer->stream_id);
            wrap_chunk_header(consumer->first_video_chunk_header, 0,
                    consumer->video_chunk_id, 0,
                    buffer->video_sh->header.msg_len, RTMP_MSG_VideoMessage,
                    consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_video_chunk_header);
            write_chunk_chain_payload(consumer->client,
                    buffer->video_sh->chain, false);

            //发送第一个关键帧chunk
            CLIENT_TRACE(consumer->client, "wrap first key frame header and send.");
            wrap_chunk_header(consumer->first_key_fram_chunk_header, 1,
                    consumer->video_chunk_id, 0, chunk_header->msg_len,
                    RTMP_MSG_VideoMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_key_fram_chunk_header);
            rtmp_write_chunk_payload(consumer->client->bls_socket, chunk);

            CLIENT_TRACE(consumer->client, "get first key fram. send "
                    "metadata/video sh/key frame chunk to client, "
                    "and set consumer state to wait_audio_sh");
            consumer->state = CONSUMER_WAIT_AUDIO_SH;
            return;
        }
        break;

    case CONSUMER_WAIT_VIDEO_SH:
        /**
         * 收到视频帧，将视频帧和缓存的视频sh发出去
         */
        if (NULL != buffer->video_sh && chunk_is_keyframe(chunk))
        {
            //发送video sequence header
            CLIENT_TRACE(consumer->client, "wrap video sequence header and send."
                    "msg_len: %u stream_id: %u",
                    buffer->video_sh->header.msg_len, consumer->stream_id);
            wrap_chunk_header(consumer->first_video_chunk_header, 0,
                    consumer->video_chunk_id, 0,
                    buffer->video_sh->header.msg_len, RTMP_MSG_VideoMessage,
                    consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_video_chunk_header);
            write_chunk_chain_payload(consumer->client,
                    buffer->video_sh->chain, false);

            //发送第一个关键帧chunk
            CLIENT_TRACE(consumer->client, "wrap first key frame header and send.");
            wrap_chunk_header(consumer->first_key_fram_chunk_header, 1,
                    consumer->video_chunk_id, 0, chunk_header->msg_len,
                    RTMP_MSG_VideoMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_key_fram_chunk_header);
            rtmp_write_chunk_payload(consumer->client->bls_socket, chunk);

            CLIENT_TRACE(consumer->client, "get first key fram. send "
                    "metadata/video sh/key frame chunk to client, "
                    "and set consumer state to wait_audio_sh");

            consumer->state = CONSUMER_RUN;
            return;
        }
        break;

    case CONSUMER_WAIT_VIDEO_SLOW_SPEED:

        if (consumer->chunk_in_buffer_num < MAX_CHUNK_BUFFER_LEN
                && chunk_is_keyframe(chunk))
        {
            CLIENT_NOTICE(consumer->client,
                    "client consume speed up, start dispatch data");

            consumer->state = CONSUMER_RUN;

            rtmp_write_chunk(consumer->client->bls_socket, chunk,
                    consumer_write_chunk_cb, consumer->client->protocol);

            consumer->chunk_in_buffer_num++;
            return;
        }
        break;

    default:
        break;
    }
}

void consumer_send_audio(bls_consumer_t* consumer, chunk_bucket_t *chunk,
        chunk_header_t *chunk_header, av_buffer_t *buffer)
{
    CLIENT_DEBUG(consumer->client, "try to dispatch video chunk to client."
            " state: %d buffer_chunk: %u chunk_stream_name: %s source_stream_name: %s",
            consumer->state, consumer->chunk_in_buffer_num,
            chunk->source_stream_name->c_str(), consumer->stream_name->c_str());

    assert(*consumer->stream_name == *chunk->source_stream_name);

    switch (consumer->state)
    {
    //直接转发音频帧
    case CONSUMER_RUN:
    case CONSUMER_WAIT_VIDEO_SH:

        if (chunk->format != RTMP_CHUNK_FMT_TYPE3
                && consumer->chunk_in_buffer_num > MAX_CHUNK_BUFFER_LEN)
        {
            consumer->state
                    = consumer->state == CONSUMER_WAIT_VIDEO_SH ? CONSUMER_WAIT_AUDIO_SLOW_SPEED
                            : CONSUMER_WAIT_VIDEO_SLOW_SPEED;

            CLIENT_WARNING(consumer->client, "this is a slow player. stop dispatch data. "
                    "state: %d", consumer->state);

            return;
        }

        rtmp_write_chunk(consumer->client->bls_socket, chunk,
                consumer_write_chunk_cb, consumer->client->protocol);

        consumer->chunk_in_buffer_num++;
        return;

        //开始转发音频帧
    case CONSUMER_WAIT_METADATA:
        /**
         * 当收到一个音频帧，但是之前并没有收到视频关键帧，则认为这个流中只有音频
         * 将状态设置为CONSUMER_WAIT_VIDEO_SH，忽略视频帧
         */
        if (NULL != buffer->metadata && NULL == buffer->video_sh
                && chunk->format != RTMP_CHUNK_FMT_TYPE3)
        {
            CLIENT_TRACE(consumer->client,
                    "get audio data without key frame, so there is no video");

            ngx_queue_t *temp_q =
                    ngx_queue_head(&buffer->metadata->chain.queue);
            chunk_bucket_t *meta_chunk =
                    ngx_queue_data(temp_q, chunk_bucket_t, queue);
            chunk_chain_t meta_data_chain;
            BlsSource *source = (BlsSource *) buffer->source;

            //发送视频metadata
            CLIENT_TRACE(consumer->client, "wrap metadata and send."
                    "msg_len: %u stream_id: %u",
                    buffer->metadata->header.msg_len, consumer->stream_id);
            wrap_chunk_header(consumer->metadata_chunk_header, 0,
                    meta_chunk->chunk_id, 0, buffer->metadata->header.msg_len,
                    RTMP_MSG_AMF0DataMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->metadata_chunk_header);

            CLIENT_TRACE(consumer->client, "send metadata with absTime: %.2f",
                    chunk_header->timestamp + source->abs_record_time);

            source->update_abs_record_time(chunk_header->timestamp);
            encode_buf_to_chunk_chain(meta_data_chain,
                    (uint8_t *) source->metadata_buf, source->metadata_len,
                    consumer->stream_id, 0, 0, meta_chunk->chunk_id);

            write_chunk_chain_payload(consumer->client, meta_data_chain, false);

            //发送audio sequence header
            if (NULL != buffer->audio_sh)
            {
                CLIENT_TRACE(consumer->client, "wrap audio sequence header and send."
                        "msg_len: %u stream_id: %u",
                        buffer->audio_sh->header.msg_len, consumer->stream_id);
                wrap_chunk_header(consumer->first_audio_chunk_header, 0,
                        consumer->audio_chunk_id, 0,
                        buffer->audio_sh->header.msg_len,
                        RTMP_MSG_AudioMessage, consumer->stream_id);
                rtmp_write_chunk_header(consumer->client->bls_socket,
                        consumer->first_audio_chunk_header);
                write_chunk_chain_payload(consumer->client,
                        buffer->audio_sh->chain, false);
            }

            //发送第一个audio chunk
            CLIENT_TRACE(consumer->client, "wrap first audio chunk header and send.");
            wrap_chunk_header(consumer->first_audio2_chunk_header, 0,
                    consumer->audio_chunk_id, 0, chunk_header->msg_len,
                    RTMP_MSG_AudioMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_audio2_chunk_header);
            rtmp_write_chunk_payload(consumer->client->bls_socket, chunk);

            consumer->state = CONSUMER_WAIT_VIDEO_SH;
            return;
        }
        break;

    case CONSUMER_WAIT_AUDIO_SH:
        /**
         * 收到音频帧，将音频帧和缓存的音频sh一起发出去
         */
        if (NULL != buffer->audio_sh)
        {
            //发送audio sequence header
            CLIENT_TRACE(consumer->client, "wrap audio sequence header and send."
                    "msg_len: %u stream_id: %u",
                    buffer->audio_sh->header.msg_len, consumer->stream_id);
            wrap_chunk_header(consumer->first_audio_chunk_header, 0,
                    consumer->audio_chunk_id, 0,
                    buffer->audio_sh->header.msg_len, RTMP_MSG_AudioMessage,
                    consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_audio_chunk_header);
            write_chunk_chain_payload(consumer->client,
                    buffer->audio_sh->chain, false);

            //发送第一个audio chunk
            CLIENT_TRACE(consumer->client, "wrap first audio chunk header and send.");
            wrap_chunk_header(consumer->first_audio2_chunk_header, 1,
                    consumer->audio_chunk_id, 0, chunk_header->msg_len,
                    RTMP_MSG_AudioMessage, consumer->stream_id);
            rtmp_write_chunk_header(consumer->client->bls_socket,
                    consumer->first_audio2_chunk_header);
            rtmp_write_chunk_payload(consumer->client->bls_socket, chunk);

            consumer->state = CONSUMER_RUN;
            return;
        }
        break;

    case CONSUMER_WAIT_AUDIO_SLOW_SPEED:

        if (consumer->chunk_in_buffer_num < MAX_CHUNK_BUFFER_LEN
                && chunk->format != RTMP_CHUNK_FMT_TYPE3)
        {
            CLIENT_NOTICE(consumer->client, "client consume speed up, start dispatch data");
            consumer->state = CONSUMER_RUN;
            rtmp_write_chunk(consumer->client->bls_socket, chunk,
                    consumer_write_chunk_cb, consumer->client->protocol);

            consumer->chunk_in_buffer_num++;
            return;
        }

        break;

    default:
        break;
    }
}

void consumer_write_chunk_cb(void *data)
{
    RtmpProtocol *p = (RtmpProtocol *) data;

    if (NULL != p->play_consumer)
    {
        p->play_consumer->chunk_in_buffer_num--;
    }
}
