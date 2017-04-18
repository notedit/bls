/*
 * BlsMessage.h
 *
 *  Created on: 2014-10-31
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef BLSMESSAGE_H_
#define BLSMESSAGE_H_

#include <uv.h>
#include <RtmpProtocol.h>
#include <RtmpChunkPool.h>

/**
 5. Protocol Control Messages
 RTMP reserves message type IDs 1-7 for protocol control messages.
 These messages contain information needed by the RTM Chunk Stream
 protocol or RTMP itself. Protocol messages with IDs 1 & 2 are
 reserved for usage with RTM Chunk Stream protocol. Protocol messages
 with IDs 3-6 are reserved for usage of RTMP. Protocol message with ID
 7 is used between edge server and origin server.
 */
#define RTMP_MSG_SetChunkSize                 0x01
#define RTMP_MSG_AbortMessage                 0x02
#define RTMP_MSG_Acknowledgement             0x03
#define RTMP_MSG_UserControlMessage         0x04
#define RTMP_MSG_WindowAcknowledgementSize     0x05
#define RTMP_MSG_SetPeerBandwidth             0x06
#define RTMP_MSG_EdgeAndOriginServerCommand 0x07
/**
 3. Types of messages
 The server and the client send messages over the network to
 communicate with each other. The messages can be of any type which
 includes audio messages, video messages, command messages, shared
 object messages, data messages, and user control messages.
 3.1. Command message
 Command messages carry the AMF-encoded commands between the client
 and the server. These messages have been assigned message type value
 of 20 for AMF0 encoding and message type value of 17 for AMF3
 encoding. These messages are sent to perform some operations like
 connect, createStream, publish, play, pause on the peer. Command
 messages like onstatus, result etc. are used to inform the sender
 about the status of the requested commands. A command message
 consists of command name, transaction ID, and command object that
 contains related parameters. A client or a server can request Remote
 Procedure Calls (RPC) over streams that are communicated using the
 command messages to the peer.
 */
#define RTMP_MSG_AMF3CommandMessage         17 // 0x11
#define RTMP_MSG_AMF0CommandMessage         20 // 0x14
/**
 3.2. Data message
 The client or the server sends this message to send Metadata or any
 user data to the peer. Metadata includes details about the
 data(audio, video etc.) like creation time, duration, theme and so
 on. These messages have been assigned message type value of 18 for
 AMF0 and message type value of 15 for AMF3.
 */
#define RTMP_MSG_AMF0DataMessage             18 // 0x12
#define RTMP_MSG_AMF3DataMessage             15 // 0x0F
/**
 3.3. Shared object message
 A shared object is a Flash object (a collection of name value pairs)
 that are in synchronization across multiple clients, instances, and
 so on. The message types kMsgContainer=19 for AMF0 and
 kMsgContainerEx=16 for AMF3 are reserved for shared object events.
 Each message can contain multiple events.
 */
#define RTMP_MSG_AMF3SharedObject             16 // 0x10
#define RTMP_MSG_AMF0SharedObject             19 // 0x13
/**
 3.4. Audio message
 The client or the server sends this message to send audio data to the
 peer. The message type value of 8 is reserved for audio messages.
 */
#define RTMP_MSG_AudioMessage                 8 // 0x08
/* *
 3.5. Video message
 The client or the server sends this message to send video data to the
 peer. The message type value of 9 is reserved for video messages.
 These messages are large and can delay the sending of other type of
 messages. To avoid such a situation, the video message is assigned
 the lowest priority.
 */
#define RTMP_MSG_VideoMessage                 9 // 0x09
/**
 3.6. Aggregate message
 An aggregate message is a single message that contains a list of submessages.
 The message type value of 22 is reserved for aggregate
 messages.
 */
#define RTMP_MSG_AggregateMessage             22 // 0x16
typedef struct bls_message_s bls_message_t;
struct bls_message_s
{
    RtmpProtocol *protocol;
    rtmp_channel_t channel;
};

typedef void (*message_decoder)(bls_message_t &);

/**
 * 初始化chunk的type到message处理函数的映射关系
 */
void init_type_message_map();

/**
 * 处理message的函数，在线程池中执行decode，回调中执行cb
 */
typedef struct message_worker_s message_worker_t;
struct message_worker_s
{
    message_decoder decode;
    //    uv_after_work_cb cb;
    bool available;
};

#define _MESSAGE_WORKER_DECLARE(w) \
    void msg_##w##_decode(bls_message_t &msg);
//    void msg_##w##_cb(uv_work_t* req, int status);

/**
 * 声明不同类型的message对应的处理函数
 */
_MESSAGE_WORKER_DECLARE(set_chunk_size);
_MESSAGE_WORKER_DECLARE(abort_message);
_MESSAGE_WORKER_DECLARE(acknowledgement);
_MESSAGE_WORKER_DECLARE(window_acknowledgement_size);
_MESSAGE_WORKER_DECLARE(set_peer_bandwidth);
_MESSAGE_WORKER_DECLARE(user_control_message);
_MESSAGE_WORKER_DECLARE(command_message_amf0);
_MESSAGE_WORKER_DECLARE(command_message_amf3);
_MESSAGE_WORKER_DECLARE(msg_data_message_amf0);
_MESSAGE_WORKER_DECLARE(msg_data_message_amf3);
_MESSAGE_WORKER_DECLARE(share_object_message_amf0);
_MESSAGE_WORKER_DECLARE(share_object_message_amf3);
_MESSAGE_WORKER_DECLARE(audio);
_MESSAGE_WORKER_DECLARE(video);
_MESSAGE_WORKER_DECLARE(aggregate_message);

#undef _MESSAGE_WORKER_DECLARE

/**
 * 处理一个完整的message
 * 将channel转成一个rtmp里的message后，调用对应的message处理函数
 * @param channel
 */
void msg_decode(RtmpProtocol *protocol);

#endif /* BLSMESSAGE_H_ */
