/*
 * BlsSource.h
 *
 *  Created on: 2014-11-11
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef BLSSOURCE_H_
#define BLSSOURCE_H_

#include <BlsConsumer.h>
#include <RtmpChunkPool.h>
#include <uv-private/ngx-queue.h>
#include <stdlib.h>
#include <string>

/**
 * 默认的source数量，表示允许同时多少个流在发布
 */
#define DEFAULT_SOURCE_POOL_SIZE 3000

/**
 * metadata的最大长度
 */
#define MAX_METADATA_BUF_LEN 3000

/**
 * gop中缓存的帧的最大数量
 */
#define MAX_GOP_LEN 1000

/**
 * source表示在直播时，管理直播数据分发的中心
 */
class BlsSource
{
public:
    BlsSource(uint32_t video, uint32_t audio);
    virtual ~BlsSource();

    /**
     * 在source中创建一个consumer
     * @param consumer
     */
    void add_consumer(bls_consumer_t *consumer);

    /**
     * 从source中删除一个consumer
     * @param consumer
     */
    void delete_consumer(bls_consumer_t *consumer);

    /**
     * 处理音视频数据chunk
     * @param channel
     * @param chunk
     */
    void on_av_chunk(rtmp_channel_t *channel, chunk_bucket_t* chunk);

    /**
     * 处理视频sh
     * @param channel
     */
    void on_video_msg(rtmp_channel_t *channel);

    /**
     * 处理音频sh
     * @param channel
     */
    void on_audio_msg(rtmp_channel_t *channel);

    /**
     * 处理metadata
     * @param channel
     * @param buffer metadata内容的buffer
     */
    void on_metadata(rtmp_channel_t *channel, uint8_t *buffer = NULL);

    /**
     * 在缓存的metadata中找到absRecordTime的位置
     */
    void find_abs_time_in_metadata();

    /**
     * 更新metadata buf里的absrecordtime
     * @param time_add 增加的时间大小
     */
    void update_abs_record_time(size_t time_add);

    /**
     * 删除所有相关的consumer,并且删除所有旧信息，只保留chunkid
     */
    void clear_consumer();

    /**
     * 判断是否有consumer
     * @return
     */
    bool consumer_is_empty();

    /**
     * 在gop中添加新的msg
     * @param channel
     */
    void add_msg_to_gop(rtmp_channel_t *channel);

    /**
     * 清理gop中的数据
     */
    void clear_gop();

    /**
     * 提取aac sequence header数据
     * @param target 将数据拷贝到的目标地址
     * @return -1表示失败, 否则返回数据大小
     */
    int copy_aac_sh_data(uint8_t *target);

    /**
     * 提取avc sequence header数据
     * @param target 将数据拷贝到的目标地址
     * @return -1表示失败, 否则返回数据大小
     */
    int copy_avc_sh_data(uint8_t *target);

    //发布的流名称
    std::string stream_name;
    //标志是否正在发布
    bool is_publishing;

    //source使用的音视频chunkid，不同的source使用不同的chunkid
    uint32_t video_chunkid;
    uint32_t audio_chunkid;

    //用于缓存metadata信息
    char metadata_buf[MAX_METADATA_BUF_LEN];
    size_t metadata_len;
    double abs_record_time;
    size_t time_index;

private:
    av_buffer_t key_cache;

    uint32_t video_time;
    uint32_t audio_time;

    ngx_queue_t consumer_queue;
};

typedef struct source_bucket_s source_bucket_t;
struct source_bucket_s
{
    BlsSource *source;
    ngx_queue_t queue;
};

/**
 * 改变一个channel的chuankid
 * @param channel
 */
void change_channel_chunkid(rtmp_channel_t *channel, uint32_t new_id);

/**
 * 初始化source pool
 */
void init_source_pool();

/**
 * 从pool中申请一个source
 * @return
 */
source_bucket_t *apply_source();

/**
 * 回收一个source
 * @param source
 */
void collect_source(source_bucket_t *source);

/**
 * 申请一个用来publish的source
 * @param stream_name 流的名字
 * @return
 */
source_bucket_t *get_publish_source(std::string stream_name);

/**
 * 申请一个用来play的source
 * @param stream_name 流的名字
 * @return
 */
source_bucket_t *get_play_source(std::string stream_name);

#endif /* BLSSOURCE_H_ */
