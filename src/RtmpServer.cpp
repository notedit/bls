/*
 * RtmpServer.cpp
 *
 *  Created on: 2014-10-13
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include "RtmpServer.h"
#include <RtmpClient.h>
#include <BlsLogger.h>
#include <utilities.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * uv callback for a new client come
 * @param server
 * @param status connect status
 */
void on_new_connection(uv_stream_t *server, int status)
{
    RtmpServer *rtmp_server = (RtmpServer *) server->data;

    if (status)
    {
        SYS_WARNING("get a new client error. status %d error: %s",
                status, uv_err_name(status));
        return;
    }

    rtmp_server->accept_client();
}

RtmpServer::RtmpServer(rtmp_config_t conf)
{
    port = conf.port;
    max_client_num = conf.max_client_num;
    client_count = 0;
    accept_cb = NULL;
    ping_pong_interval_time = conf.ping_pong_time;

    main_loop = uv_default_loop();
    uv_tcp_init(main_loop, &uv_server);
    uv_server.data = (void *) this;
}

RtmpServer::~RtmpServer()
{
    uv_close((uv_handle_t *) &uv_server, NULL);
}

int RtmpServer::start()
{
    struct sockaddr_in bind_addr;
    int listen_result;

    /*init rand seed for rtmp client id*/
    srand(getpid());

    /*bind fd and listen port*/
    uv_ip4_addr("0.0.0.0", port, &bind_addr);
    uv_tcp_bind(&uv_server, (const struct sockaddr*)&bind_addr, 0);
    listen_result
            = uv_listen((uv_stream_t*) &uv_server, 128, on_new_connection);

    if (listen_result)
    {
        SYS_FATAL("can not listen port %d. err: %s",
                port, uv_err_name(listen_result));
        return -1;
    }

    SYS_NOTICE("rtmp server start to listen port %d", port);

    return 0;
}

void RtmpServer::accept_client()
{
    RtmpClient *new_rtmp_client;
    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(main_loop, client);

    if (uv_accept((uv_stream_t *) &uv_server, (uv_stream_t*) client) == 0)
    {
        //如果超过连接上限，则拒绝连接
        if (client_count > max_client_num)
        {
            SYS_WARNING("reach max client number. reject new client.");
            bls_close_tcp(client);

            return;
        }
        else
        {
            update_client_count(1);
            new_rtmp_client = new RtmpClient(this, client);

            if (NULL != accept_cb)
                accept_cb(new_rtmp_client);
        }
    }
    else
    {
        SYS_WARNING("accept new client fail. error: %s",
                uv_err_name(-1));
        bls_close_tcp(client);

    }
}

void RtmpServer::update_client_count(int num)
{
    client_count += num;
}

void RtmpServer::register_on_accept(on_accept_cb cb_func)
{
    accept_cb = cb_func;
}
