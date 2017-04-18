/*
 * RtmpServer.h
 *
 *  Created on: 2014-10-13
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef RTMPSERVER_H_
#define RTMPSERVER_H_

#define DEFAULT_MAX_CLIENT 2000

#include <RtmpChunkPool.h>
#include <uv.h>

/**
 * rtmp server read this struct to initialize resource
 */
typedef struct rtmp_config_s rtmp_config_t;
struct rtmp_config_s
{
    int port;
    int max_client_num;
    const char *log_conf_path;
    int log_level;

    size_t chunk_bucket_size;
    size_t chunk_pool_size;
    int ping_pong_time;

    rtmp_config_s()
    {
        port = 1935;
        max_client_num = DEFAULT_MAX_CLIENT;
        log_conf_path = NULL;
        ping_pong_time = 30;

        chunk_bucket_size = DEFAULT_CHUNK_BUCKET_SIZE;
        chunk_pool_size = DEFAULT_CHUNK_POOL_SIZE;
    }
};

/**
 * 当有新的连接上来时，调用的回调函数
 * @param new_client RtmpClient指针
 */
typedef void (*on_accept_cb)(void *new_client);

/*
 * listen port and process different client
 */
class RtmpServer
{
public:

    /**
     * 初始化服务需要的资源，不包括chunk pool和日志等信息
     * @param conf 配置信息
     * @return
     */
    RtmpServer(rtmp_config_t conf);

    /**
     * 释放server的申请的资源，一般在程序整个退出时才调用
     * @return
     */
    virtual ~RtmpServer();

    /**
     * 根据配置信息，监听服务端口
     * @return
     */
    int start();

    /**
     * 接受一个新的链接，创建Rtmplient
     */
    void accept_client();

    /**
     * 增加或者减少client计数
     * @param num 要增加的数量（负数表示减少）
     */
    void update_client_count(int num);

    /**
     * 注册accept的事件回调函数
     * @param cb_func
     */
    void register_on_accept(on_accept_cb cb_func);

private:
    int port;
    int max_client_num;
    int client_count;

    /*libuv data struct*/
    uv_loop_t *main_loop;
    uv_tcp_t uv_server;

    on_accept_cb accept_cb;

public:
    int ping_pong_interval_time;
};

#endif /* RTMPSERVER_H_ */
