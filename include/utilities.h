/*
 * utilities.h
 *
 *  Created on: 2014-10-14
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef UTILITIES_H_
#define UTILITIES_H_

#include <stddef.h>
#include <uv.h>

#define bls_delete(x)    \
    do{                 \
        if(NULL != (x))   \
            delete (x);   \
        (x) = NULL;       \
    }while(0)

#define bls_free(x)        \
    do{                   \
        if(NULL != (x))     \
            free((x));      \
        (x) = NULL;         \
    }while(0)

#define bls_simple_min(x, y) ((x) > (y) ? (y) : (x))

void* bls_malloc(size_t);

/**
 * 关闭TCP连接，异步释放libuv的tcp句柄
 * @param handle
 */
void bls_close_tcp(uv_tcp_t *handle);

/**
 * 获取系统的毫秒时间戳
 * @return time stamp
 */
long get_current_time();

/**
 * 处理rtmp流里的单位数据
 * @param p
 * @return
 */
uint8_t read_1bytes(uint8_t *p);
uint16_t read_2bytes(uint8_t *p);
uint32_t read_3bytes(uint8_t *p);
uint32_t read_4bytes(uint8_t *p);
int64_t read_8bytes(uint8_t *p);
void write_1bytes(uint8_t *p, uint8_t value);
void write_2bytes(uint8_t *p, uint16_t value);
void write_4bytes(uint8_t *p, uint32_t value);
void write_3bytes(uint8_t *p, uint32_t value);
void write_8bytes(uint8_t *p, int64_t value);
#endif /* UTILITIES_H_ */
