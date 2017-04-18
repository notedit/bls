/*
 * RtmpProtocol.h
 *
 *  Created on: 2014-10-27
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef RTMPPROTOCOL_H_
#define RTMPPROTOCOL_H_

#include <uv.h>
#include <uv-private/ngx-queue.h>
#include <RtmpClient.h>
#include <RtmpChunkPool.h>
#include <utilities.h>
#include <map>
#include <BlsSource.h>
#include <BlsConsumer.h>
#include <string>

class RtmpProtocol
{
public:
    /**
     * 做rtmp的编码解码
     * @param c 客户端实例
     * @return
     */
    RtmpProtocol(RtmpClient *c);
    virtual ~RtmpProtocol();

    /**
     * 开始从client中读取数据，编码成message
     */
    void start();

    /**
     * 接收一个chunk数据
     */
    void expect_chunk();

    /**
     * 设置对端的ack window size
     * @param w 窗口大小 byte
     */
    void set_window_ack_size(size_t w);

    /**
     * 通过窗口大小，设置对端的带宽
     * @param w 窗口大小 byte
     * @param type 带宽类型
     */
    void set_peer_bandwidth(size_t w, uint8_t type);

    /**
     * 设置chunksize的大小，大小设置为DEFAULT_CHUNK_BUCKET_SIZE
     */
    void set_chunk_size();

    /**
     * 通知流开始（控制命令）
     */
    void send_stream_begin();
    void send_stream_eof();

    /**
     * 向端回复window ack
     */
    void response_window_ack();

    /**
     * 发送心跳包
     */
    void send_ping_request();

    /**
     * 回复心跳包
     */
    void send_pong_response(uint32_t t);

    /**
     * 处理ack window大小，当收到了足够的数据，就回复一个ack
     * @param recved
     */
    void process_window_ack(int recved);

    /**
     * 设置心跳间隔，并开启定时器
     * @param t 心跳时间间隔，单位秒
     */
    void set_ping_pong_interval(int t);

    /**
     * 关闭pingpong包的定时器
     */
    void stop_ping_pong_timer();

public:
    //关联的client实例
    RtmpClient *client;

    //channel的映射表，根据chunkid将chunk分配到对应的channel
    std::map<uint32_t, rtmp_channel_t*> channels;
    chunk_bucket_t *current_chunk;
    rtmp_channel_t *current_channel;

    //rtmp协议的发送窗口大小
    size_t window_size;

    //rtmp的ack窗口大小
    size_t acked_size;

    //记录连接上收到的所有数据大小
    size_t total_recved_size;

    //记录连接上收到的音视频帧数量
    size_t total_recved_video_msg;
    size_t total_recved_audio_msg;
    
    //心跳定时器
    uv_timer_t ping_pong_timer;
    bool pong_get;

    //当客户端发布后，创建source用于分发数据流
    source_bucket_t *publish_source;

    //当客户端订阅后，创建consumer用于接收数据
    bls_consumer_t *play_consumer;
};

/**
 * 计算某个chunk的payload是多少
 * @param c channel
 * @param client rtmp client
 * @return 计算出的payload大小
 */
uint32_t expect_payload_len(rtmp_channel_t *c, RtmpClient *client);

/**
 * 封装一个chunk的头信息到header字段中，用来发送
 * @param fmt
 * @param chunk_id
 * @param timestamp
 * @param msg_len
 * @param type_id
 * @param msg_id
 */
void wrap_chunk_header(chunk_bucket_t *b, uint8_t fmt, uint32_t chunk_id,
        uint32_t timestamp = 0, uint32_t msg_len = 0, uint8_t type_id = 0,
        uint32_t msg_id = 0);

/**
 * 将一个完整的message拆分成一个chunk chain
 * @param chain 拆分成的chunk的一个queue
 * @param buf payload数据
 * @param len payload长度
 * @param sid stream_id
 * @param ts timestamp
 * @param type type_id
 */
void encode_buf_to_chunk_chain(chunk_chain_t &chain, uint8_t *buf, size_t len,
        uint32_t sid, uint32_t ts, uint8_t type, uint32_t chunk_id = 3);

/**
 * 将一个chunk chain写给客户端
 * @param client
 * @param chain
 * @param auto_free 表示是否在发送成功后自动回收chunk
 */
void write_chunk_chain(RtmpClient *client, chunk_chain_t &chain,
        bool auto_free);

/**
 * 将一个chunk chain写给客户端,除了第一个chunk的header
 * @param client
 * @param chain
 * @param auto_free 表示是否在发送成功后自动回收chunk
 */
void write_chunk_chain_payload(RtmpClient *client, chunk_chain_t &chain,
        bool auto_free);

#endif /* RTMPPROTOCOL_H_ */
