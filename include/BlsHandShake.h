/*
 * RtmpHandShake.h
 *
 *  Created on: 2014-10-22
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef RTMPHANDSHAKE_H_
#define RTMPHANDSHAKE_H_

#include <uv.h>
#include <RtmpClient.h>
#include <librtmp/handshake.h>

//握手签名数据的大小（C1/C2）
//#define RTMP_SIG_SIZE 1536
#define HANDSHAKE_RECV_BUF_LEN RTMP_SIG_SIZE + 1
#define HANDSHAKE_SEND_BUF_LEN RTMP_SIG_SIZE + RTMP_SIG_SIZE +1

#define HANDSHAKE_PRIVATE_SIG "rtmp-addon"
#define HANDSHAKE_PRIVATE_SIG_LEN 16

typedef struct bls_handshake_s bls_handshake_t;
struct bls_handshake_s
{

    /*for read buffer info*/
    uv_buf_t buf;
    size_t recved_len;
    uint8_t clientsig[HANDSHAKE_RECV_BUF_LEN];
    uint8_t serverbuf[HANDSHAKE_SEND_BUF_LEN];

    /*client info*/
    RtmpClient *rtmp_client;

    bls_handshake_s(RtmpClient *client)
    {
        rtmp_client = client;

        buf.base = (char *) clientsig;
        buf.len = RTMP_SIG_SIZE + 1;
        recved_len = 0;
    }
};

/**
 * 握手阶段的内存分配回调函数
 * 为C0/C1/C2数据分配空间
 * @param handle handle中的data需要指向一个bls_handshake_t数据
 * @param suggested_size
 * @return
 */
void hs_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

/**
 * 握手时处理收到的数据，同时向设备发送S0/S1/S2
 * 交互完成后，调用client的handshake_finish接口，返回握手结果
 * @param client data指针指向bls_handshake_t结构
 * @param nread 读到的数据
 * @param buf 数据存放地址
 */
void hs_read_buffer(uv_stream_t* client, ssize_t nread, const uv_buf_t *buf);

/**
 * 用于级联，向远端服务器发送特殊字段来完成握手
 * @param client
 */
void hs_write_private_sig(RtmpClient* client);

#endif /* RTMPHANDSHAKE_H_ */
