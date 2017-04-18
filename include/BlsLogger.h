/*
 * BlsLogger.h
 *
 *  Created on: 2014-10-13
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#ifndef BLSLOGGER_H_
#define BLSLOGGER_H_

//#include <com_log.h>
#include <spdlog/spdlog.h>
#include <stdio.h>

namespace spd = spdlog;

/**
 * 初始化日志模块，程序开始时执行一次
 * @param conf_path 配置文件相对路径 conf/log_conf.conf
 */
void init_bls_logger(const char*conf_path, int level);

/**
 * 关闭日志模块
 */
void close_bls_logger();

//#define BMS_LOG(fmt, args...) com_writelog("BMS", fmt, ##args)
#define BMS_LOG(fmt, args...) spd::get("bls")->info(fmt, ##args)

//#define DEBUG_LOG(fmt, args...) com_writelog(COMLOG_DEBUG, fmt, ##args)
//#define TRACE(fmt, args...) com_writelog(COMLOG_TRACE, fmt, ##args)
//#define NOTICE(fmt, args...) com_writelog(COMLOG_NOTICE, fmt, ##args)
//#define WARNING(fmt, args...) com_writelog(COMLOG_WARNING, fmt, ##args)
//#define FATAL(fmt, args...) com_writelog(COMLOG_FATAL, fmt, ##args)
//
#define DEBUG_LOG(fmt, args...) spd::get("bls")->debug(fmt, ##args)
//#define DEBUG_LOG(fmt, args...) spd::get("bls")->debug(fmt)
#define TRACE(fmt, args...) spd::get("bls")->info(fmt, ##args)
#define NOTICE(fmt, args...) spd::get("bls")->critical(fmt, ##args)
#define WARNING(fmt, args...) spd::get("bls")->warn(fmt, ##args)
#define FATAL(fmt, args...) spd::get("bls")->error(fmt, ##args)

#define SYS_DEBUG(fmt, args...) DEBUG_LOG("[SYSTEM]" fmt, ##args)
#define SYS_TRACE(fmt, args...) TRACE("[SYSTEM]" fmt, ##args)
#define SYS_NOTICE(fmt, args...) NOTICE("[SYSTEM]" fmt, ##args)
#define SYS_WARNING(fmt, args...) WARNING("[SYSTEM]" fmt, ##args)
#define SYS_FATAL(fmt, args...) FATAL("[SYSTEM]" fmt, ##args)

#define CLIENT_DEBUG(client, fmt, args...) \
    DEBUG_LOG("[%s] " fmt, client->id, ##args)
#define CLIENT_TRACE(client, fmt, args...) \
    TRACE("[%s] " fmt, client->id, ##args)
#define CLIENT_NOTICE(client, fmt, args...) \
    NOTICE("[%s] " fmt, client->id, ##args)
#define CLIENT_WARNING(client, fmt, args...) \
    WARNING("[%s] " fmt, client->id, ##args)
#define CLIENT_FATAL(client, fmt, args...) \
    FATAL("[%s] " fmt, client->id, ##args)

#endif /* BLSLOGGER_H_ */
