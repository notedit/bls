/*
 * RtmpChunkPool.h
 *
 *  Created on: 2014-10-15
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef RTMPCHUNKPOOL_H_
#define RTMPCHUNKPOOL_H_

#include <v8stdint.h>
#include <uv-private/ngx-queue.h>
#include <uv.h>
#include <stdlib.h>
#include <string>
#include <BlsSocket.h>

/**
 * this pool is not thread safe, just use in main thread
 */

/**
 * the size of a bucket in chunk pool
 * this size is set according to rtmp chunk size + max chunk header
 * rtmp chunk size can not be larger than chunk bucket size
 */
#define DEFAULT_CHUNK_BUCKET_SIZE 8192

#define DEFAULT_CHUNK_SIZE 128

/**the number of chunk bucket in chunk pool
 * mem_size = chunk_pool_size * chunk_pool_size
 * example: if chunk_bucket_size is 200 and chunk_pool_size is 500
 *           the memory pool needs is 100000B
 */
#define DEFAULT_CHUNK_POOL_SIZE 5000

/**
 * rtmp chunk header struct
 * fm(2)+cid(6+8+8)+ts(3*8)+message_length(3*8)
 * +message_type(8)+sid(4*8)+ext_ts(4*8)
 */
#define RTMP_CHUNK_HEADER_MAX 18

/**
 * the chunk stream id used for some under-layer message,
 * for example, the PC(protocol control) message.
 */
#define RTMP_CID_ProtocolControl 0x02
/**
 * the AMF0/AMF3 command message, invoke method and return the result, over NetConnection.
 * generally use 0x03.
 */
#define RTMP_CID_OverConnection 0x03
/**
 * the AMF0/AMF3 command message, invoke method and return the result, over NetConnection,
 * the midst state(we guess).
 * rarely used, e.g. onStatus(NetStream.Play.Reset).
 */
#define RTMP_CID_OverConnection2 0x04
/**
 * the stream message(amf0/amf3), over NetStream.
 * generally use 0x05.
 */
#define RTMP_CID_OverStream 0x05
/**
 * the stream message(amf0/amf3), over NetStream, the midst state(we guess).
 * rarely used, e.g. play("mp4:mystram.f4v")
 */
#define RTMP_CID_OverStream2 0x08

/**
 * chunk state machine value for receive new chunk
 */
typedef enum chunk_state_e
{
    CHUNK_STATE_BEGIN, //!< CHUNK_STATE_BEGIN
    CHUNK_STATE_SIZE_BASIC_HEADER, //!< CHUNK_STATE_SIZE_BASIC_HEADER
    CHUNK_STATE_READ_EXT_BASIC_HEADER,//!< CHUNK_STATE_READ_EXT_BASIC_HEADER
    CHUNK_STATE_READ_MESSAGE_HEADER, //!< CHUNK_STATE_READ_MESSAGE_HEADER
    CHUNK_STATE_READ_EXT_TIME, //!< CHUNK_STATE_READ_EXT_TIME
    CHUNK_STATE_READ_PAYLOAD, //!< CHUNK_STATE_READ_PAYLOAD
    CHUNK_STATE_MERGE_HEADER, //!< CHUNK_STATE_MERGE_HEADER
    CHUNK_STATE_END
//!< CHUNK_STATE_END
} chunk_state_t;

/**
 * rtmp chunk fmt value
 */
#define RTMP_CHUNK_FMT_TYPE0 0
#define RTMP_CHUNK_FMT_TYPE1 1
#define RTMP_CHUNK_FMT_TYPE2 2
#define RTMP_CHUNK_FMT_TYPE3 3

/**
 * rtmp extend timestamp
 */
#define RTMP_CHUNK_EXTENDED_TIMESTAMP 0xFFFFFF

// E.4.3.1 VIDEODATA
// Frame Type UB [4]
// Type of video frame. The following values are defined:
//     1 = key frame (for AVC, a seekable frame)
//     2 = inter frame (for AVC, a non-seekable frame)
//     3 = disposable inter frame (H.263 only)
//     4 = generated key frame (reserved for server use only)
//     5 = video info/command frame
enum CodecVideoAVCFrame
{
    CodecVideoAVCFrameReserved = 0,

    CodecVideoAVCFrameKeyFrame = 1,
    CodecVideoAVCFrameInterFrame = 2,
    CodecVideoAVCFrameDisposableInterFrame = 3,
    CodecVideoAVCFrameGeneratedKeyFrame = 4,
    CodecVideoAVCFrameVideoInfoFrame = 5,
};

// AVCPacketType IF CodecID == 7 UI8
// The following values are defined:
//     0 = AVC sequence header
//     1 = AVC NALU
//     2 = AVC end of sequence (lower level NALU sequence ender is
//         not required or supported)
enum CodecVideoAVCType
{
    CodecVideoAVCTypeReserved = -1,

    CodecVideoAVCTypeSequenceHeader = 0,
    CodecVideoAVCTypeNALU = 1,
    CodecVideoAVCTypeSequenceHeaderEOF = 2,
};

// E.4.3.1 VIDEODATA
// CodecID UB [4]
// Codec Identifier. The following values are defined:
//     2 = Sorenson H.263
//     3 = Screen video
//     4 = On2 VP6
//     5 = On2 VP6 with alpha channel
//     6 = Screen video version 2
//     7 = AVC
enum CodecVideo
{
    CodecVideoReserved = 0,

    CodecVideoSorensonH263 = 2,
    CodecVideoScreenVideo = 3,
    CodecVideoOn2VP6 = 4,
    CodecVideoOn2VP6WithAlphaChannel = 5,
    CodecVideoScreenVideoVersion2 = 6,
    CodecVideoAVC = 7,
};

// SoundFormat UB [4]
// Format of SoundData. The following values are defined:
//     0 = Linear PCM, platform endian
//     1 = ADPCM
//     2 = MP3
//     3 = Linear PCM, little endian
//     4 = Nellymoser 16 kHz mono
//     5 = Nellymoser 8 kHz mono
//     6 = Nellymoser
//     7 = G.711 A-law logarithmic PCM
//     8 = G.711 mu-law logarithmic PCM
//     9 = reserved
//     10 = AAC
//     11 = Speex
//     14 = MP3 8 kHz
//     15 = Device-specific sound
// Formats 7, 8, 14, and 15 are reserved.
// AAC is supported in Flash Player 9,0,115,0 and higher.
// Speex is supported in Flash Player 10 and higher.
enum CodecAudio
{
    CodecAudioLinearPCMPlatformEndian = 0,
    CodecAudioADPCM = 1,
    CodecAudioMP3 = 2,
    CodecAudioLinearPCMLittleEndian = 3,
    CodecAudioNellymoser16kHzMono = 4,
    CodecAudioNellymoser8kHzMono = 5,
    CodecAudioNellymoser = 6,
    CodecAudioReservedG711AlawLogarithmicPCM = 7,
    CodecAudioReservedG711MuLawLogarithmicPCM = 8,
    CodecAudioReserved = 9,
    CodecAudioAAC = 10,
    CodecAudioSpeex = 11,
    CodecAudioReservedMP3_8kHz = 14,
    CodecAudioReservedDeviceSpecificSound = 15,
};

// AACPacketType IF SoundFormat == 10 UI8
// The following values are defined:
//     0 = AAC sequence header
//     1 = AAC raw
enum CodecAudioType
{
    CodecAudioTypeReserved = -1,
    CodecAudioTypeSequenceHeader = 0,
    CodecAudioTypeRawData = 1,
};

/**
 * chunk header info struct
 */
typedef struct chunk_header_s chunk_header_t;
struct chunk_header_s
{
    /*chunk header info*/
    uint32_t stream_id;
    uint32_t timestamp;
    uint32_t delta_timestamp;
    uint8_t type;

    uint32_t msg_len;
    uint32_t msg_recv;

    chunk_header_s()
    {
        stream_id = 0;
        timestamp = 0;
        delta_timestamp = 0;
        type = 0;
        msg_len = 0;
        msg_recv = 0;
    }
};

/**
 * chunk data bucket, basic unit of send/recv
 */
typedef struct chunk_bucket_s chunk_bucket_t;
struct chunk_bucket_s
{
    /*manage info*/
    /*according to uv_buf_t*/
    uint8_t *header_start_p;
    size_t header_length;
    uint8_t *payload_start_p;
    size_t payload_length;

    //source flag
    std::string *source_stream_name;

    //引用计数
    uint16_t ref_count;

    size_t chunk_id;

    uint8_t buffer_header[3];

    chunk_state_t state;
    uint8_t format;
    uint32_t bucket_id;

    size_t header_recv_len;
    size_t payload_recv_len;

    /*for uv write*/
    uv_write_t *req;

    /*chunk data, include header and payload*/
    uint8_t *data;

    ngx_queue_t queue;
};

/*
 * 自定义的写chunk回调函数
 */
typedef void (*custom_write_cb)(void *data);

/*
 * 自定义写chunk的回调数据格式
 */
typedef struct cuntom_write_data_s custom_write_data_t;
struct cuntom_write_data_s
{
    custom_write_cb cb;
    void *data;
    chunk_bucket_t *chunk;
};

/**
 * use libuv to write a chunk.
 * bucket will not be free until write finish
 * @param s bls socket for write
 * @param b bucket struct
 * @param cb custom write chunk cb
 * @param data custom write chunk cb data
 * @return 0 for success, others for fail
 */
int rtmp_write_chunk(BlsSocket *s, chunk_bucket_t *b,
        custom_write_cb cb = NULL, void *data = NULL);

/**
 * use libuv to write a chunk header.
 * bucket will not be free until write finish
 * @param s bls socket for write
 * @param b bucket struct
 * @return 0 for success, others for fail
 */
int rtmp_write_chunk_header(BlsSocket *s, chunk_bucket_t *b);

/**
 * use libuv to write a chunk payload.
 * bucket will not be free until write finish
 * @param s bls socket for write
 * @param b bucket struct
 * @return 0 for success, others for fail
 */
int rtmp_write_chunk_payload(BlsSocket *s, chunk_bucket_t *b);

/**
 * for inner use
 * auto free bucket when write finish
 * @param req uv struct
 * @param status write result
 */
void _bucket_write_cb(uv_write_t *req, int status);

/**
 * for custom write chunk cb
 * @param req
 * @param status
 */
void _custom_bucket_write_cb(uv_write_t *req, int status);

/**
 * 按照指定的size初始化chunk pool
 * @param bucket_size 每个bucket中payload的大小
 * @param pool_size pool里bucket的个数
 * @return 0 for success, -1 for fail
 */
int init_chunk_pool(size_t payload_size, size_t pool_size);

/**
 * 从pool中获取一个未使用的bucket,当空间不够时申请新的空间
 * @return NULL for fail
 */
chunk_bucket_s *alloc_chunk_bucket();

/**
 * 回收一个bucket
 * @param bucket
 */
void free_chunk_bucket(chunk_bucket_s *bucket);

/**
 * 回收一个chunk队列
 * @param bucket
 */
void free_chunk_bucket_queue(ngx_queue_t *q);

/**
 * 释放整个chunk pool的空间
 */
void free_chunk_pool();

/**
 * 判断一个chunk是否视频关键帧的第一个chunk
 * @param bucket
 * @return
 */
bool chunk_is_keyframe(chunk_bucket_t *video_bucket);

/**
 * 判断一个chunk是否是视频sequence header
 * @param video_bucket
 * @return
 */
bool chunk_is_video_sh(chunk_bucket_t *video_bucket);

/**
 * 判断一个chunk是否是音频sequence header
 * @param video_bucket
 * @return
 */
bool chunk_is_audio_sh(chunk_bucket_t *audio_bucket);

typedef struct chunk_chain_s chunk_chain_t;
struct chunk_chain_s
{
    ngx_queue_t queue;

    chunk_chain_s()
    {
        ngx_queue_init(&queue);
    }

    ~chunk_chain_s()
    {
        //collect chunk bucket in this channel
        ngx_queue_t *q = NULL;
        chunk_bucket_t *b = NULL;
        ngx_queue_foreach(q, &(queue))
        {
            if (NULL != b)
            {
                free_chunk_bucket(b);
            }
            b = ngx_queue_data(q, chunk_bucket_t, queue);
        }
        if (NULL != b)
        {
            free_chunk_bucket(b);
        }
    }
};

typedef struct rtmp_channel_s rtmp_channel_t;
struct rtmp_channel_s
{
    chunk_header_t header;
    chunk_chain_t chain;
};

#endif /* RTMPCHUNKPOOL_H_ */
