/*
 * BlsSocket.cpp
 *
 *  Created on: 2014-10-27
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "BlsSocket.h"
#include "utilities.h"

BlsSocket::BlsSocket(uv_tcp_t *uv_socket, void *client)
{
    socket = uv_socket;
    this->bls_client = client;
    recved_bytes = 0;
    send_bytes = 0;
}

BlsSocket::~BlsSocket()
{

}

void BlsSocket::start(uv_alloc_cb alloc_cb, uv_read_cb read_cb)
{
    uv_read_start((uv_stream_t*) socket, alloc_cb, read_cb);
}

int BlsSocket::write(uv_write_t* req, uv_buf_t bufs[], int bufcnt,
        uv_write_cb cb)
{
    for(int i = 0; i < bufcnt; i++)
    {
        send_bytes += bufs[i].len;
    }

    return uv_write(req, (uv_stream_t *) socket, bufs, bufcnt, cb);
}

void BlsSocket::stop()
{
    uv_read_stop((uv_stream_t *) socket);
}

void BlsSocket::close()
{
    if (NULL != socket)
    {
        bls_close_tcp(socket);
        socket = NULL;
    }
}
