/*
 * RtmpClient.cpp
 *
 *  Created on: 2014-10-20
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "RtmpClient.h"
#include <string.h>
#include <stdio.h>
#include <utilities.h>
#include <BlsLogger.h>
#include <RtmpChunkPool.h>
#include <BlsHandShake.h>
#include <RtmpProtocol.h>
//#include <node.h>
//#include <uv.h>
#include <nan.h>

using Nan::New;

extern Nan::Callback *g_connect_cb;
//extern v8::Persistent<v8::Function> g_connect_cb;

/**
 * rtmpclient写buf的回调函数，释放req，并且回调外部指定的回调函数
 * @param req
 * @param status
 */
void __bls_write_buf_cb(uv_write_t *req, int status)
{
    bls_write_buf_t *bls_buf = (bls_write_buf_t *) req->data;

    if (NULL != bls_buf->cb)
        bls_buf->cb(bls_buf->buf, bls_buf->data, status);

    bls_free(req);
    bls_free(bls_buf);
}

void __bls_read_n_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    bls_read_buf_s *bls_read_buf = (bls_read_buf_s *) handle->data;

    uv_buf_t temp_buf;
    temp_buf.len = bls_read_buf->expect_len - bls_read_buf->recved_len;
    temp_buf.base = (char *) bls_read_buf->buf + bls_read_buf->recved_len;

    buf->len = temp_buf.len;
    buf->base = temp_buf.base;
    return;
}

void __bls_read_n_read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t *buf)
{
    bls_read_buf_t *bls_read_buf = (bls_read_buf_t *) handle->data;

    //如果接受数据失败，直接调用回调函数返回错误
    if (nread < 0)
    {
        CLIENT_WARNING(bls_read_buf->client, "read data fail. %s",
                uv_err_name(nread));
        bls_read_buf->cb(bls_read_buf->buf, bls_read_buf->data, -1);
        return;
    }

    bls_read_buf->recved_len += nread;

    //接收到了足够的数据，回调外部注册的回调函数
    if (bls_read_buf->recved_len == bls_read_buf->expect_len)
    {
        bls_read_buf->client->pause();
        bls_read_buf->cb(bls_read_buf->buf, bls_read_buf->data,
                bls_read_buf->expect_len);
        return;
    }
}

RtmpClient::RtmpClient(RtmpServer *server, uv_tcp_t *client)
{
    bls_socket = new BlsSocket(client, (void *) this);
    uv_client = client;
    uv_client->data = NULL;
    rtmp_server = server;
    enable_video_up = false;

    uv_tcp_keepalive(client, 1, 10);
    uv_tcp_nodelay(client, 1);

    chunk_size = DEFAULT_CHUNK_SIZE;

    //generate a global unique client id
    memset(id, 0, 20);
    long now_time = get_current_time();
    snprintf(id, 19, "%ld_%d", now_time, rand() % 10000);

    //get client ip
    memset(ip, 0, 20);
    struct sockaddr_in name;
    int len = sizeof(name);
    uv_tcp_getpeername(uv_client, (struct sockaddr *) &name, &len);
    uv_ip4_name(&name, ip, sizeof(ip));

    protocol = NULL;

    uv_client->data = (void *) this;
    is_reading = false;
    read_info = (bls_read_buf_t *) malloc(sizeof(bls_read_buf_t));

#ifndef NODE_EXTEND
    node_buf_len = 1000;
    node_read_buf = (uint8_t *) malloc(1000);
    node_write_buf = (uint8_t *) malloc(1000);
    node_av_buf = (uint8_t *) malloc(2*1024*1024);
#endif

    CLIENT_NOTICE(this, "new rtmp client come. ip : %s", ip);
}

RtmpClient::~RtmpClient()
{
    //release in close call back
    //bls_free(uv_client);
    bls_delete(bls_socket);
    bls_free(read_info);

#ifdef NODE_EXTEND
    //node_on_msg_cb.Dispose();
    //node_on_close_cb.Dispose();
#endif
}

void RtmpClient::start()
{
    //start handshake session at first
    bls_handshake_t *hs = new bls_handshake_t(this);
    uv_client->data = (void *) hs;
    bls_socket->start(hs_alloc_buffer, hs_read_buffer);
    is_reading = true;
}

void RtmpClient::close()
{
    RtmpProtocol *p;

    if (NULL != uv_client)
    {
        pause();

        if (protocol)
        {
            p = (RtmpProtocol *) protocol;

            uv_client->data = protocol;
            p->stop_ping_pong_timer();

            //set consumer state to end, avoid be sent video
            if (NULL != p->play_consumer)
            {
                p->play_consumer->state = CONSUMER_END;
            }
        }
        else
        {
            uv_client->data = NULL;
        }

        bls_socket->close();
        uv_client = NULL;

        if (NULL != rtmp_server)
            rtmp_server->update_client_count(-1);
    }
}

void RtmpClient::register_read_cb(void *p_data, uv_alloc_cb alloc_cb,
        uv_read_cb read_cb)
{
    on_alloc = alloc_cb;
    on_read = read_cb;

    if (NULL == uv_client)
        return;

    if (NULL == p_data)
        uv_client->data = (void *) this;
    else
        uv_client->data = p_data;

    if (is_reading)
        bls_socket->stop();

    bls_socket->start(alloc_cb, read_cb);
    is_reading = true;

}

void RtmpClient::pause()
{
    bls_socket->stop();
    is_reading = false;
}

void RtmpClient::resume()
{
    bls_socket->start(on_alloc, on_read);
    is_reading = true;
}

void RtmpClient::hand_shake_finish(int status)
{
    RtmpProtocol *protocol = new RtmpProtocol(this);
    this->protocol = (void *) protocol;

    if (NULL != rtmp_server)
    {
        protocol->set_ping_pong_interval(rtmp_server->ping_pong_interval_time);
    }

    protocol->start();

#ifdef NODE_EXTEND
    //cb to nodejs for a new client!
    Nan::HandleScope scope;
    //v8::Local<v8::Value> new_node_client = v8::External::Wrap((void *)this);
    v8::Local<v8::Value> new_node_client = Nan::New<v8::External>((void *)this);
    v8::Local<v8::Value> argv_res[] =
    {
        new_node_client,
        New(id).ToLocalChecked(),
        New(ip).ToLocalChecked(),
    };

    g_connect_cb->Call(3, argv_res);
    //g_connect_cb->Call(v8::Context::GetCurrent()->Global(), 3, argv_res);
#endif

    return;
}

int RtmpClient::write_buf(uv_buf_t *buf, void *data, bls_write_buf_cb cb)
{
    bls_write_buf_t *bls_buf = (bls_write_buf_t *) malloc(
            sizeof(bls_write_buf_t));
    bls_buf->buf = buf;
    bls_buf->cb = cb;
    bls_buf->data = data;

    uv_write_t *req = (uv_write_t *) malloc(sizeof(uv_write_t));
    req->data = (void*) bls_buf;

    return bls_socket->write(req, buf, 1, __bls_write_buf_cb);
}

int RtmpClient::read_n(uint8_t *buf, size_t n, void *data, bls_read_n_cb cb)
{
    bls_read_buf_t *bls_buf = read_info;
    bls_buf->buf = buf;
    bls_buf->cb = cb;
    bls_buf->client = this;
    bls_buf->data = data;
    bls_buf->expect_len = n;
    bls_buf->recved_len = 0;

    if (n == 0)
    {
        CLIENT_WARNING(this, "try to read 0. close it!");
        close();
        return 0;
    }

    read_info = bls_buf;

    register_read_cb((void *) bls_buf, __bls_read_n_alloc_cb,
            __bls_read_n_read_cb);
    return 0;
}

typedef struct remote_connect_data_s remote_connect_data_t;
struct remote_connect_data_s
{
    //v8::Persistent<v8::Function> data;
    Nan::Callback *data;
    bls_connect_cb cb_func;
    uv_tcp_t *r_client;
};

/**
 * 内部回调函数，用于获取级联状态并回调外部回调函数
 * @param req
 * @param status
 */
void _remote_connect_cb(uv_connect_t* req, int status)
{
    remote_connect_data_t *r_data = (remote_connect_data_t *) req->data;

    if (status == 0)
    {
        RtmpClient *r_rtmp_client = new RtmpClient(NULL, r_data->r_client);

        //发送握手信息（私有的协议）
        hs_write_private_sig(r_rtmp_client);

        r_data->cb_func(r_rtmp_client, r_data->data);
    }
    else
    {
        SYS_TRACE("the remote server network is not reachable %s",
                uv_err_name(status));

        r_data->r_client->data = NULL;

        //链接失败，关闭socket
        bls_close_tcp(r_data->r_client);
        r_data->cb_func(NULL, r_data->data);
    }

    bls_free(r_data);
    bls_free(req);
}

//void connect_remote_server(const char *ip, int port, v8::Persistent<
        //v8::Function> data, bls_connect_cb cb_func)
void connect_remote_server(const char *ip, int port, Nan::Callback *data, bls_connect_cb cb_func)
{
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(ip);

    struct sockaddr_in addr;
    uv_ip4_addr(ip, port, &addr);

    uv_tcp_t *r_client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), r_client);

    remote_connect_data_t *r_data;
    r_data = (remote_connect_data_t *) malloc(sizeof(remote_connect_data_t));
    r_data->data = data;
    r_data->cb_func = cb_func;
    r_data->r_client = r_client;

    uv_connect_t* req = (uv_connect_t *) malloc(sizeof(uv_connect_t));
    req->data = (void *) r_data;

    SYS_TRACE("try to connect remote server. ip: %s port: %d", ip, port);

    //uv_tcp_connect(req, r_client, address, _remote_connect_cb);
    uv_tcp_connect(req, r_client, (const struct sockaddr*)&addr, _remote_connect_cb);
}
