/*
 * utilities.cpp
 *
 *  Created on: 2014-10-18
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include <utilities.h>
#include <BlsLogger.h>
#include <stdlib.h>
#include <sys/time.h>
#include <RtmpProtocol.h>

void* bls_malloc(size_t s)
{
    void *p;

    p = malloc(s);

    if (NULL == p)
    {
        FATAL("oooooooh no! malloc fail! process exit!!!!!!!!!");
        exit(1);
    }

    return p;
}

/**
 * 异步释放内存
 * @param handle
 */
void bls_close_tcp_cb(uv_handle_t *handle)
{
    RtmpProtocol *p;
    uv_tcp_t *t = (uv_tcp_t *) handle;

    if (NULL != t->data)
    {
        p = (RtmpProtocol *) t->data;
        bls_delete(p);
    }

    bls_free(t);
}

void bls_close_tcp(uv_tcp_t *handle)
{
    uv_close((uv_handle_t*) handle, bls_close_tcp_cb);
}

long get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint8_t read_1bytes(uint8_t *p)
{
    return *p;
}

uint16_t read_2bytes(uint8_t *p)
{
    uint16_t value;
    uint8_t *pp = (uint8_t*) &value;
    pp[1] = *p++;
    pp[0] = *p;

    return value;
}

uint32_t read_3bytes(uint8_t *p)
{
    uint32_t value = 0x00;
    uint8_t *pp = (uint8_t*) &value;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;

    return value;
}

uint32_t read_4bytes(uint8_t *p)
{
    uint32_t value = 0x00;
    uint8_t *pp = (uint8_t*) &value;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;

    return value;
}

int64_t read_8bytes(uint8_t *p)
{
    int64_t value;
    uint8_t *pp = (uint8_t*) &value;
    pp[7] = *p++;
    pp[6] = *p++;
    pp[5] = *p++;
    pp[4] = *p++;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;

    return value;
}

void write_1bytes(uint8_t *p, uint8_t value)
{
    *p = value;
}

void write_2bytes(uint8_t *p, uint16_t value)
{
    uint8_t *pp = (uint8_t*) &value;
    *p++ = pp[1];
    *p++ = pp[0];
}

void write_4bytes(uint8_t *p, uint32_t value)
{
    uint8_t *pp = (uint8_t*) &value;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void write_3bytes(uint8_t *p, uint32_t value)
{
    uint8_t *pp = (uint8_t*) &value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void write_8bytes(uint8_t *p, int64_t value)
{
    uint8_t *pp = (uint8_t*) &value;
    *p++ = pp[7];
    *p++ = pp[6];
    *p++ = pp[5];
    *p++ = pp[4];
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}
