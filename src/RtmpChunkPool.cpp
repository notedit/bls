/*
 * RtmpChunkPool.cpp
 *
 *  Created on: 2014-10-15
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include <RtmpChunkPool.h>
#include <BlsSocket.h>
#include <stdlib.h>
#include <BlsLogger.h>
#include <utilities.h>
#include <RtmpClient.h>

#define MAX_ADD_POOL_SIZE_COUNT 100

typedef struct pool_queue_s
{
    ngx_queue_t queue;
} pool_queue_t;

size_t g_bucket_id;

static pool_queue_t *g_queue; //head point of pool chunk queue
static size_t g_payload_size; //the payload size set by user
static size_t g_pool_size; //the pool size set by user
static size_t g_valid_chunk_num; //the available chunk num. for debug

static uint8_t* g_pool_data_p[MAX_ADD_POOL_SIZE_COUNT]; //record malloc data point
static int g_add_pool_count; //the count of add pool size

int rtmp_write_chunk(BlsSocket *s, chunk_bucket_t *b, custom_write_cb cb,
        void *data)
{
    custom_write_data_t *custom_data = NULL;

    //add reference count to avoid be freed when write
    ++(b->ref_count);

    CLIENT_DEBUG(((RtmpClient *)s->bls_client),
            "send data to client. bucket_id: %u ref: %u"
            " buf1_len: %lu, buf2_len: %lu want %lu %lu",
            b->bucket_id, b->ref_count, ((uv_buf_t *)b)->len,
            (((uv_buf_t *)b)+1)->len,
            b->header_length, b->payload_length);

    uv_write_t *temp_req = (uv_write_t *) bls_malloc(sizeof(uv_write_t));

    if (NULL != cb && NULL != data)
    {
        custom_data = (custom_write_data_t *) bls_malloc(
                sizeof(custom_write_data_t));
        custom_data->cb = cb;
        custom_data->chunk = b;
        custom_data->data = data;

        temp_req->data = (void *) custom_data;

        return s->write(temp_req, (uv_buf_t *) (b), 2, _custom_bucket_write_cb);
    }
    else
    {
        temp_req->data = (void *) b;

        return s->write(temp_req, (uv_buf_t *) (b), 2, _bucket_write_cb);
    }
}

int rtmp_write_chunk_header(BlsSocket *s, chunk_bucket_t *b)
{
    ++(b->ref_count); //add reference count to avoid be freed when write

    CLIENT_DEBUG(((RtmpClient *)s->bls_client),
            "send chunk header to client. "
            "bucket_id: %uref: %u buf1_len: %lu",
            b->bucket_id, b->ref_count, ((uv_buf_t *)b)->len);

    uv_write_t *temp_req = (uv_write_t *) bls_malloc(sizeof(uv_write_t));
    temp_req->data = (void *) b;

    return s->write(temp_req, (uv_buf_t *) (b), 1, _bucket_write_cb);
}

int rtmp_write_chunk_payload(BlsSocket *s, chunk_bucket_t *b)
{
    ++(b->ref_count); //add reference count to avoid be freed when write

    CLIENT_DEBUG(((RtmpClient *)s->bls_client),
            "send chunk payload to client. "
            "bucket_id: %u ref: %u buf2_len: %lu",
            b->bucket_id, b->ref_count, (((uv_buf_t *)b) + 1)->len);

    uv_write_t *temp_req = (uv_write_t *) bls_malloc(sizeof(uv_write_t));
    temp_req->data = (void *) b;

    return s->write(temp_req, ((uv_buf_t *) b) + 1, 1, _bucket_write_cb);
}

void _bucket_write_cb(uv_write_t *req, int status)
{
    chunk_bucket_t *b = (chunk_bucket_t *) req->data;

    SYS_DEBUG("write bucket cb. "
            "status %d, bucket_id: %u ref: %u len1: %u len2: %u",
            status, b->bucket_id, b->ref_count,
            b->header_length, b->payload_length);

    free_chunk_bucket((chunk_bucket_t *) req->data);

    bls_free(req);
}

void _custom_bucket_write_cb(uv_write_t *req, int status)
{
    custom_write_data_t *custom_data = (custom_write_data_t *) req->data;

    SYS_DEBUG("custom write bucket cb. status %d, "
            "bucket_id: %u ref: %u len1: %u len2: %u",
            status, custom_data->chunk->bucket_id,
            custom_data->chunk->ref_count,
            custom_data->chunk->header_length,
            custom_data->chunk->payload_length);

    free_chunk_bucket(custom_data->chunk);

    custom_data->cb(custom_data->data);

    bls_free(custom_data);
    bls_free(req);
}

/**
 * malloc new memory to g_queue
 * @param payload_size 负载的大小
 * @param add_size 为chunk pool增加的大小
 */
void add_pool_queue(size_t payload_size, size_t add_size)
{
    int one_bucket_size = payload_size + RTMP_CHUNK_HEADER_MAX;
    size_t mem_need = one_bucket_size * add_size;
    chunk_bucket_t *temp_bucket;
    uint8_t *temp_buffer;
    pool_queue_t *n_queue;

    if (g_add_pool_count >= MAX_ADD_POOL_SIZE_COUNT)
    {
        SYS_FATAL("add too much space to pool, it is not allowed. "
                "count: %d payload_size: %lu pool_size: %lu",
                MAX_ADD_POOL_SIZE_COUNT, g_payload_size, g_pool_size
        );
        return;
    }

    SYS_NOTICE("try to add new chunk queue in pool. "
            "payload_size: %lu pool_size: %lu",
            payload_size, add_size);

    n_queue = (pool_queue_t *) bls_malloc(sizeof(pool_queue_t));
    ngx_queue_init(&n_queue->queue);

    temp_buffer = (uint8_t *) bls_malloc(mem_need * sizeof(uint8_t));

    g_pool_data_p[g_add_pool_count] = temp_buffer;
    ++g_add_pool_count;

    for (unsigned int i = 0; i < add_size; ++i)
    {
        temp_bucket = (chunk_bucket_t *) bls_malloc(sizeof(chunk_bucket_t));

        /*initialize a bucket*/
        temp_bucket->header_start_p = temp_buffer;
        temp_bucket->header_length = RTMP_CHUNK_HEADER_MAX;
        temp_bucket->payload_start_p = temp_buffer + RTMP_CHUNK_HEADER_MAX;
        temp_bucket->payload_length = payload_size;
        temp_bucket->ref_count = 0;
        temp_bucket->bucket_id = ++g_bucket_id;

        temp_bucket->req = (uv_write_t *) bls_malloc(sizeof(uv_write_t));
        temp_bucket->req->data = (void *) temp_bucket;

        temp_bucket->source_stream_name = NULL;

        temp_bucket->data = temp_buffer;
        /*finish initialize a new bucket*/

        ngx_queue_insert_tail(&n_queue->queue, &temp_bucket->queue);

        temp_buffer += one_bucket_size;
    }

    g_valid_chunk_num += add_size;

    ngx_queue_add(&g_queue->queue, &n_queue->queue);

    bls_free(n_queue);

    SYS_NOTICE("add new chunk queue to pool success.");
}

int init_chunk_pool(size_t payload_size, size_t pool_size)
{
    g_payload_size = payload_size;
    g_pool_size = pool_size;
    g_add_pool_count = 0;
    g_valid_chunk_num = 0;

    g_queue = (pool_queue_t *) bls_malloc(sizeof(pool_queue_t));
    ngx_queue_init(&g_queue->queue);

    add_pool_queue(payload_size, pool_size);

    SYS_NOTICE("init chunk pool success! payload_size: %lu pool_size: %lu",
            payload_size, pool_size);

    return 0;
}

chunk_bucket_t *alloc_chunk_bucket()
{
    ngx_queue_t *q;
    chunk_bucket_t *b;

    if (ngx_queue_empty(&g_queue->queue))
    {
        add_pool_queue(g_payload_size, g_pool_size);
    }

    q = ngx_queue_last(&g_queue->queue);
    ngx_queue_remove(q);
    b = ngx_queue_data(q, chunk_bucket_t, queue);
    ++b->ref_count;

    b->header_start_p = b->data;
    b->payload_start_p = b->data + RTMP_CHUNK_HEADER_MAX;

    b->header_recv_len = 0;
    b->payload_recv_len = 0;
    b->payload_length = 0;

    g_valid_chunk_num --;

    SYS_DEBUG("get a chunk bucket from pool queue. "
            "bucket_id: %u p: %p pre: %p next: %p "
            "available chunk num: %lu",
            b->bucket_id, &b->queue, b->queue.prev, b->queue.next,
            g_valid_chunk_num);

    return b;
}

void free_chunk_bucket(chunk_bucket_t *bucket)
{
    SYS_DEBUG("before bucket collect back to pool. "
            "bucket_id: %u ref: %u p: %p pre: %p next: %p",
            bucket->bucket_id, bucket->ref_count, &bucket->queue,
            bucket->queue.prev, bucket->queue.next);

    if (--(bucket->ref_count) > 0)
    {
        return;
    }

    memset(bucket->data, 0, g_payload_size + RTMP_CHUNK_HEADER_MAX);

    //add this bucket back to pool queue
    ngx_queue_insert_head(&g_queue->queue, &bucket->queue);

    bls_delete(bucket->source_stream_name);

    g_valid_chunk_num ++;

    SYS_DEBUG("bucket collect back to pool. "
            "bucket_id: %u p: %p pre: %p next: %p "
            "available chunk num: %lu",
            bucket->bucket_id, &bucket->queue,
            bucket->queue.prev, bucket->queue.next,
            g_valid_chunk_num);
}

void free_chunk_bucket_queue(ngx_queue_t *q)
{
    ngx_queue_add(&g_queue->queue, q);
    ngx_queue_init(q);
}

/**
 * 释放整个chunk pool的空间
 */
void free_chunk_pool()
{
    ngx_queue_t *q = NULL;
    chunk_bucket_t *b = NULL;

    //release chunk queue
    ngx_queue_foreach(q, &g_queue->queue)
    {
        if (NULL != b)
        {
            bls_free(b->req);
            bls_free(b);
        }
        b = ngx_queue_data(q, chunk_bucket_t, queue);
    }
    if (NULL != b)
    {
        bls_free(b->req);
        bls_free(b);
    }

    //free space malloc before
    for (int i = 0; i < g_add_pool_count; ++i)
    {
        bls_free(g_pool_data_p[i]);
    }

    bls_free(g_queue);
}

bool chunk_is_keyframe(chunk_bucket_t *video_bucket)
{
    if (video_bucket->format == RTMP_CHUNK_FMT_TYPE3)
    {
        return false;
    }

    uint8_t frame_type = *(video_bucket->payload_start_p);
    frame_type = (frame_type >> 4) & 0x0F;

    return frame_type == CodecVideoAVCFrameKeyFrame;
}

bool chunk_is_video_sh(chunk_bucket_t *video_bucket)
{
    if (video_bucket->format == RTMP_CHUNK_FMT_TYPE3)
    {
        return false;
    }

    uint8_t *data = video_bucket->payload_start_p;

    char codec_id = *(char*) data;
    codec_id = codec_id & 0x0F;

    if (codec_id != CodecVideoAVC)
        return false;

    char frame_type = *(char*) data;
    frame_type = (frame_type >> 4) & 0x0F;

    char avc_packet_type = *(char*) (data + 1);

    return frame_type == CodecVideoAVCFrameKeyFrame &&
            avc_packet_type == CodecVideoAVCTypeSequenceHeader;
}

bool chunk_is_audio_sh(chunk_bucket_t *audio_bucket)
{
    if (audio_bucket->format == RTMP_CHUNK_FMT_TYPE3)
    {
        return false;
    }

    uint8_t *data = audio_bucket->payload_start_p;

    char sound_format = *(char*) data;
    sound_format = (sound_format >> 4) & 0x0F;

    if (sound_format != CodecAudioAAC)
        return false;

    char aac_packet_type = *(char*) (data + 1);

    return aac_packet_type == CodecAudioTypeSequenceHeader;
}
