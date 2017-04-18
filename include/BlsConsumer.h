/*
 * BlsConsumer.h
 *
 *  Created on: 2014-11-9
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef BLSCONSUMER_H_
#define BLSCONSUMER_H_

#include <RtmpClient.h>
#include <RtmpChunkPool.h>
#include <uv-private/ngx-queue.h>
#include <utilities.h>
#include <string>

/**
 * 表示一个播放端最多缓存多少chunk，如果
 * 缓存过多的chunk超过最大值，则不会再转发新的chunk，
 * 直到已经缓存的chunk消耗完
 */
#define MAX_CHUNK_BUFFER_LEN 400

/**
 * 缓存metadata/关键帧/音视频sh
 */
typedef struct av_buffer_s av_buffer_t;
struct av_buffer_s
{
    rtmp_channel_t *metadata;
    rtmp_channel_t *key_frame;
    rtmp_channel_t *video_sh;
    rtmp_channel_t *audio_sh;
    rtmp_channel_t *gop;
    uint32_t gop_counter;

    void *source;

    av_buffer_s()
    {
        metadata = NULL;
        video_sh = NULL;
        audio_sh = NULL;
        key_frame = NULL;
        gop = NULL;

        gop_counter = 0;
    }

    ~av_buffer_s()
    {
        bls_delete(metadata);
        bls_delete(video_sh);
        bls_delete(audio_sh);
        bls_delete(key_frame);
        bls_delete(gop);
    }
};

/**
 * consumer state machine value for live stream
 */
typedef enum consumer_state_e
{
    CONSUMER_WAIT_METADATA,
    CONSUMER_WAIT_AV_SH,
    CONSUMER_WAIT_AUDIO_SH,
    CONSUMER_WAIT_VIDEO_SH,
    CONSUMER_WAIT_KEYFRAME,
    CONSUMER_RUN,
    CONSUMER_WAIT_VIDEO_SLOW_SPEED,
    CONSUMER_WAIT_AUDIO_SLOW_SPEED,
    CONSUMER_END
} consumer_state_t;

/**
 * consumer表示在做直播流分发的时候，每一个consumer对应到一个source
 * 表示一个player
 */
typedef struct bls_consumer_s bls_consumer_t;
struct bls_consumer_s
{
    RtmpClient *client; //对应的客户端
    uint32_t stream_id; //该consumer使用的streamid，由server分配
    uint32_t video_chunk_id;
    uint32_t audio_chunk_id;

    consumer_state_t state;

    std::string *stream_name;

    bool is_alive;

    chunk_bucket_t *first_audio_chunk_header; //发出去的第一个音频 chunk所使用的header模板
    chunk_bucket_t *first_video_chunk_header; //发出去的第一个视频 chunk所使用的header模板
    chunk_bucket_t *first_key_fram_chunk_header; //发出去的第一个关键帧chunk所使用的header模板
    chunk_bucket_t *first_audio2_chunk_header; //发出去的第一个非audio sh chunk所使用的header模板
    chunk_bucket_t *metadata_chunk_header; //发送metadata的chunk的header模板

    uint32_t chunk_in_buffer_num;

    ngx_queue_t queue;
};

/**
 * 初始化一个consumer
 * @param client 客户端
 * @param stream_id 客户端分配到的流id
 * @param video_chunk_id 与source绑定的视频chunk id，用于初始化header模板
 * @param audio_chunk_id 与source绑定的音频chunk id，用于初始化header模板
 * @return 生成的consumer
 */
bls_consumer_t* init_consumer(RtmpClient *client, std::string stream_name,
        uint32_t stream_id, uint32_t video_chunk_id, uint32_t audio_chunk_id);

/**
 * 释放一个consumer的资源
 * @param consumer
 */
void free_consumer(bls_consumer_t *consumer);

/**
 * 向consumer发送一个视频包
 * @param consumer
 * @param chunk 包的内容
 * @param chunk_header 包的header
 * @param buffer 当前缓存的数据
 */
void consumer_send_video(bls_consumer_t* consumer, chunk_bucket_t *chunk,
        chunk_header_t *chunk_header, av_buffer_t *buffer);

/**
 * 向consumer发送一个音频包
 * @param consumer
 * @param chunk 包的内容
 * @param chunk_header 包的header
 * @param buffer 当前缓存的数据
 */
void consumer_send_audio(bls_consumer_t* consumer, chunk_bucket_t *chunk,
        chunk_header_t *chunk_header, av_buffer_t *buffer);

/**
 * consumer 写chunk的回调
 * @param data protocol
 */
void consumer_write_chunk_cb(void *data);

#endif /* BLSCONSUMER_H_ */
