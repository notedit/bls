/*
 * BlsLogger.cpp
 *
 *  Created on: 2014-10-13
 *      Author: chenyuliang01
 *
 * Copyright (c) 2014, -- blscam All Rights Reserved.
 */

#include <BlsLogger.h>

bool g_has_been_inited = false;

/**
 * 用配置文件初始化日志模块
 * @param path 配置文件路径
 */
void init_bls_logger(const char* path, int level)
{
    if (g_has_been_inited)
    {
        return;
    }
    else
    {
        g_has_been_inited = true;
    }

    //spd::level val = static_cast<spd::level>(level);
    spd::set_level(spd::level::info);

    auto daily_logger = spd::daily_logger_mt("bls", path, 0, 0);
    daily_logger->flush_on(spd::level::debug);

    spd::get("bls")->info("nononono",2);
    SYS_NOTICE("init logger finish");

    //const char *dir_path = ".";
    //int ret = com_loadlog(dir_path, path);

    //if (ret != 0)
    //{
        //fprintf(stderr, "load log config err\n");
        //exit(1);
    //}
}

void close_bls_logger()
{
    //com_closelog();
    spdlog::drop_all();
}
