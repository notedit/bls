/*
 * BlsSocket.h
 *
 *  Created on: 2014-10-27
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef BLSSOCKET_H_
#define BLSSOCKET_H_

#include <uv.h>

/**
 * 封装底层libuv的读写操作
 */
class BlsSocket
{
public:
    /**
     * 用libuv底层的socket初始化blssocket
     * @param uv_socket
     * @return
     */
    BlsSocket(uv_tcp_t *uv_socket, void *client);
    virtual ~BlsSocket();

    /**
     * 直接封装libuv底层的readstart
     * @param alloc_cb
     * @param read_cb
     */
    void start(uv_alloc_cb alloc_cb, uv_read_cb read_cb);

    /**
     * 封装libuv写数据的接口——uv_write
     * @param req
     * @param handle
     * @param bufs
     * @param bufcnt
     * @param cb
     * @return
     */
    int write(uv_write_t* req, uv_buf_t bufs[], int bufcnt, uv_write_cb cb);

    /**
     * 封装libuv底层的stop
     */
    void stop();

    /**
     * 封装libuv底层的close
     */
    void close();

    size_t recved_bytes;
    size_t send_bytes;

    void *bls_client;

private:
    uv_tcp_t *socket;

};

#endif /* BLSSOCKET_H_ */
