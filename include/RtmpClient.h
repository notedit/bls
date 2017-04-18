/*
 * RtmpClient.h
 *
 *  Created on: 2014-10-20
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef RTMPCLIENT_H_
#define RTMPCLIENT_H_

#include <RtmpServer.h>
#include <BlsLogger.h>
#include <BlsSocket.h>
#include <nan.h>
#include <uv.h>

typedef void (*bls_write_buf_cb)(uv_buf_t *buf, void *data, int status);
typedef void (*bls_read_n_cb)(uint8_t *buf, void *data, int recved);

typedef struct bls_write_buf_s bls_write_buf_t;
struct bls_write_buf_s
{
    uv_buf_t *buf;
    void *data;
    bls_write_buf_cb cb;
};

typedef struct bls_read_buf_s bls_read_buf_t;

class RtmpClient
{
public:
    /**
     * initialize a new rtmp client object
     * @param server
     * @param client
     * @return
     */
    RtmpClient(RtmpServer *server, uv_tcp_t *client);
    virtual ~RtmpClient();

    /**
     * start read data from buffer and process hand shake
     */
    void start();

    /**
     * close connection
     */
    void close();

    /**
     * 处理RTMP握手返回的结果，如果握手成功，则开始接受chunk数据
     * 握手失败则断开链接
     *
     * @param status 0 表示成功， -1表示失败
     */
    void hand_shake_finish(int status);

    /**
     * 注册处理流数据的回调函数
     * @param p_data 可以携带自定义的数据，如果没有，则用client本身
     * @param alloc_cb 分配内存空间的回调
     * @param read_cb 读取数据的回调
     */
    void register_read_cb(void *p_data, uv_alloc_cb alloc_cb,
            uv_read_cb read_cb);

    /**
     * 暂停收数据
     */
    void pause();

    /**
     * 恢复收数据，触发回调函数
     */
    void resume();

    /**
     * 向client写数据
     * @param buf libuv的buffer数据结构数组
     * @param data 外部自定义数据
     * @param cb 完成buf之后的回调函数
     * @return
     */
    int write_buf(uv_buf_t *buf, void *data, bls_write_buf_cb cb);

    /**
     * 从client中读取n个字节的数据，放进buf中
     * @param buf 用来接收数据的uv_buf_t数据结构
     * @param n 表示要读取的数据大小
     * @param data 外部自定义数据，回调时会携带返回
     * @param cb 读到数据后的回调函数
     * @return
     */
    int read_n(uint8_t *buf, size_t n, void *data, bls_read_n_cb cb);

public:
    char id[20];
    char ip[20];
    uv_tcp_t *uv_client;
    BlsSocket *bls_socket;
    RtmpServer *rtmp_server;

    bls_read_buf_t *read_info;

    uint8_t *node_read_buf;
    uint8_t *node_write_buf;
    uint8_t *node_av_buf;
    size_t node_buf_len;

    size_t chunk_size;

    void *protocol;

    //v8::Persistent<v8::Function> node_on_msg_cb;
    //v8::Persistent<v8::Function> node_on_close_cb;

    //v8::Persistent<v8::Function> node_on_av_cb;
    Nan::Callback *node_on_msg_cb;
    Nan::Callback *node_on_close_cb;

    Nan::Callback *node_on_av_cb;
    bool enable_video_up;

private:
    bool is_reading;
    uv_alloc_cb on_alloc;
    uv_read_cb on_read;
};

struct bls_read_buf_s
{
    size_t expect_len;
    size_t recved_len;
    uint8_t *buf;
    RtmpClient *client;
    void *data;
    bls_read_n_cb cb;
};

/**
 * 创建级联链接的回调函数，级联成功返回一个RtmpClient实例指针，否则返回NULL
 * @param
 */
typedef void (*bls_connect_cb)(RtmpClient *r_rtmp_client, Nan::Callback *data);
//typedef void (*bls_connect_cb)(RtmpClient *r_rtmp_client, v8::Persistent<
        //v8::Function> data);

/**
 * 级联远端机器，并且完成握手
 * @param ip 远端机器的ip
 * @param port 远端机器的port
 * @param data 外部自定义数据
 * @param cb_func 外部回调函数
 * @return 成功返回一个RtmpClient实例指针，失败返回NULL
 */
void connect_remote_server(const char *ip, int port, Nan::Callback *data, bls_connect_cb cb_func);
//void connect_remote_server(const char *ip, int port, v8::Persistent<
        //v8::Function> data, bls_connect_cb cb_func);

#endif /* RTMPCLIENT_H_ */
