/*
 * RtmpHandShake.cpp
 *
 *  Created on: 2014-10-22
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include <BlsHandShake.h>
#include <BlsLogger.h>
#include <utilities.h>
#include <string.h>

char private_handshake_data[HANDSHAKE_RECV_BUF_LEN];

//如果握手失败，则关闭链接
#define HAND_SHAKE_FAIL(hs) \
    do {                    \
        hs->rtmp_client->close();     \
        bls_delete(hs->rtmp_client);    \
        bls_delete(hs);     \
        return;         \
    }while (0)

void hs_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t* buf)
{
    bls_handshake_t *hs = (bls_handshake_t *) handle->data;

    hs->buf.len = HANDSHAKE_RECV_BUF_LEN - hs->recved_len;
    hs->buf.base = (char *) hs->clientsig + hs->recved_len;

    buf->len = hs->buf.len;
    buf->base = hs->buf.base;

    return ;
}

/**
 *等待客户端返回握手数据C2
 * @param client
 * @param nread
 * @param buf
 */
void hs_expect_client_response(uv_stream_t* client, ssize_t nread, const uv_buf_t *buf)
{
    bls_handshake_t *hs = (bls_handshake_t *) client->data;

    if (nread < 0)
    {
        CLIENT_WARNING(hs->rtmp_client, "hand shake fail. read C2 error: %s",
                uv_err_name(nread));
        HAND_SHAKE_FAIL(hs);
    }

    hs->recved_len += nread;

    //握手结束，返回成功结果，并且释放hs
    if (hs->recved_len == HANDSHAKE_RECV_BUF_LEN)
    {
        CLIENT_NOTICE(hs->rtmp_client, "OMG!! hand shake success!");
        hs->rtmp_client->pause();
        hs->rtmp_client->hand_shake_finish(1);
        bls_delete(hs);
    }
}

/**
 * 发送握手的服务器信息，如果成功，则开始接收client的握手数据
 * @param buf
 * @param data 指向hs结构体
 * @param status libuv发送的结果
 */
void send_hs_server_response_cb(uv_buf_t *buf, void *data, int status)
{
    bls_handshake_t *hs = (bls_handshake_t *) data;

    if (status < 0)
    {
        CLIENT_WARNING(hs->rtmp_client, "hand shake fail. send server data error: %s",
                uv_err_name(status));
        HAND_SHAKE_FAIL(hs);
    }

    CLIENT_DEBUG(hs->rtmp_client, "Hand Shake send S1 S2 S3 success");

    hs->recved_len = 1; //C2只需要1536 byte
    hs->rtmp_client->register_read_cb((void *) hs, hs_alloc_buffer,
            hs_expect_client_response);
}

void hs_read_buffer(uv_stream_t* client, ssize_t nread, const uv_buf_t *buf)
{
    uv_buf_t out_buf;
    bls_handshake_t *hs = (bls_handshake_t *) client->data;

    if (nread < 0)
    {
        CLIENT_WARNING(hs->rtmp_client, "hand shake fail. read data error: %s",
                uv_err_name(nread));
        HAND_SHAKE_FAIL(hs);
    }

    hs->recved_len += nread;

    //get enough data,
    //calculate handshake response
    if (hs->recved_len == HANDSHAKE_RECV_BUF_LEN)
    {
        CLIENT_DEBUG(hs->rtmp_client, "HandShake recv C1 and C2");
        hs->rtmp_client->pause();

        if (!memcmp(hs->clientsig, HANDSHAKE_PRIVATE_SIG,
                HANDSHAKE_PRIVATE_SIG_LEN))
        {
            CLIENT_TRACE(hs->rtmp_client, "recv private hand shake sig. accept ^_^");
            hs->rtmp_client->hand_shake_finish(1);
            bls_delete(hs);
            return;
        }

        if (!SHandShake(*(hs->clientsig), hs->clientsig + 1, hs->serverbuf))
        {
            CLIENT_WARNING(hs->rtmp_client, "hand shake fail. generate data error");
            HAND_SHAKE_FAIL(hs);
        }

        //send server sig to client, and register read cb to finish handshake
        out_buf.base = (char *) hs->serverbuf;
        out_buf.len = HANDSHAKE_SEND_BUF_LEN;
        hs->rtmp_client->write_buf(&out_buf, (void *) hs,
                send_hs_server_response_cb);
    }
}

void hs_write_private_sig(RtmpClient* client)
{
    uv_buf_t out_buf;
    out_buf.len = HANDSHAKE_RECV_BUF_LEN;
    out_buf.base = private_handshake_data;

    memcpy(private_handshake_data, HANDSHAKE_PRIVATE_SIG,
            HANDSHAKE_PRIVATE_SIG_LEN);

    CLIENT_TRACE(client, "send private handshake sig to remote server");

    client->write_buf(&out_buf, NULL, NULL);

    return;
}

#undef HAND_SHAKE_FAIL
