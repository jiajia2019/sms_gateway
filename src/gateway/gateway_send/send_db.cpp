#include "send_db.h"
#include <hiredis/hiredis.h>

#define DB_KEY_RD_SMS_CHANNEL                   "rd_sms_channel"
#define DB_KEY_RD_SMS_CHANNEL_SEND_REMAINING    "rd_sms_channel_send_remaining"
#define DB_KEY_RD_SMS_INTERFACE_TYPE            "rd_sms_interface_type"
#define DB_KEY_RD_SMS_CHANNEL_STATUS            "rd_sms_channel_status"

typedef struct db_conf_s 
{
    char db_ip[MAX_IP_LEN];
    int db_port;
    char db_name[MAX_DB_DOMAIN_LEN];
    char db_psw[MAX_KEY_LEN];
} db_conf_t;

static db_conf_t g_db_conf;
static redisContext *g_pRedisContext = NULL;

static int load_db_conf(const char *conf)
{
    if (conf == NULL)
        return -1;

    cfg_t *cfg = cfg_create();
    if (cfg == NULL)
    {
        LOG_ERROR("cfg_create failed.\n");
        return -1;
    }

    int ret = parse_conf(conf, cfg);
    if (ret < 0)
    {
        LOG_ERROR("parse_conf failed.\n");
        return -1;
    }

    sec_dict_t *sec = get_section(cfg, "REDIS_DB");
    if (sec != NULL)
    {
        //db
        READ_CONF_STR_MUST(sec, "DB_IP",                g_db_conf.db_ip);
        READ_CONF_INT_MUST(sec, "DB_PORT",              g_db_conf.db_port);
        READ_CONF_STR_MUST(sec, "DB_NAME",              g_db_conf.db_name);
        READ_CONF_STR_MUST(sec, "DB_PSW",               g_db_conf.db_psw);
    }
    cfg_destroy(cfg);

    return 0;
}

int init_db(void *conf)
{
    LOG_INFO("%s:%d:%s:pid=%d\n", __FILE__, __LINE__, __func__, getpid());
    if (load_db_conf((const char*)conf) != 0)
        return -1;

    //连接数据库
    g_pRedisContext = redisConnect(g_db_conf.db_ip, g_db_conf.db_port);
    if ( g_pRedisContext == NULL || g_pRedisContext->err )
    {
        if( g_pRedisContext )
        {
            LOG_ERROR("Connection error: %s\n", g_pRedisContext->errstr);
            redisFree(g_pRedisContext);
            g_pRedisContext = NULL;
        }
        else
        {
             LOG_ERROR("Connection error: can't allocate redis context\n");
        }
        return -1;
    }

    //授权
    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"AUTH %s",g_db_conf.db_psw);
    if( reply == NULL ) return -1;
    if( reply->type == REDIS_REPLY_STATUS )
    {
        if ( strcmp(reply->str, "OK") == 0 )
        {
            freeReplyObject(reply);
            reply = NULL;
            return 0;
        }
    }
    freeReplyObject(reply);
    reply = NULL;
    return -1;
}

void uninit_db()
{
    if( g_pRedisContext )
    {
        redisFree(g_pRedisContext);
        g_pRedisContext = NULL;
    }
}


bool reconnect_db()
{
    //重连
    int ret = redisReconnect(g_pRedisContext);
    if( ret != REDIS_OK )
    {
        LOG_ERROR("redisReconnect fail.\n");
        return false;
    }

    //授权
    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"AUTH %s",g_db_conf.db_psw);
    if( reply == NULL ) return -1;
    if( reply->type == REDIS_REPLY_STATUS )
    {
        if ( strcmp(reply->str, "OK") == 0 )
        {
            freeReplyObject(reply);
            reply = NULL;
            return true;
        }
    }
    freeReplyObject(reply);
    reply = NULL;
    
    return false;
}

int query_channel_info(const char *channel_id,string &info)
{
    if( g_pRedisContext == NULL )
    {
        return -1;
    }

    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"HGET %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL,
                                                    channel_id);
    if( reply == NULL )
    {
        if( !reconnect_db() )
        {
            LOG_ERROR("reconnect_db fail.\n");
            return -1;
        }
        reply = (redisReply *)redisCommand(g_pRedisContext,"HGET %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL,
                                                    channel_id);
        if( reply == NULL ) return -1;
    }
    if( reply->type == REDIS_REPLY_NIL )
    {
        freeReplyObject(reply);
        reply = NULL;
        return 1;
    }
    if( reply->type == REDIS_REPLY_STRING )
    {
        info = reply->str;
        freeReplyObject(reply);
        reply = NULL;
        return 0;
    }
    LOG_ERROR("redisCommand error.type[%d]msg[%s]\n",reply->type,reply->str);
    freeReplyObject(reply);
    reply = NULL;
    return -1;
}

int query_httpchannel_info(const char *interface_id,uint32_t &http_type)
{
    if( g_pRedisContext == NULL )
    {
        return -1;
    }

    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"HGET %s %s",
                                                    DB_KEY_RD_SMS_INTERFACE_TYPE,
                                                    interface_id);
    if( reply == NULL )
    {
        if( !reconnect_db() )
        {
            LOG_ERROR("reconnect_db fail.\n");
            return -1;
        }
        reply = (redisReply *)redisCommand(g_pRedisContext,"HGET %s %s",
                                                    DB_KEY_RD_SMS_INTERFACE_TYPE,
                                                    interface_id);
        if( reply == NULL ) return -1;
    }
    if( reply->type != REDIS_REPLY_STRING )
    {
        LOG_ERROR("redisCommand error.type[%d]msg[%s]\n",reply->type,reply->str);
        freeReplyObject(reply);
        reply = NULL;
        return -1;
    }

    http_type = atoi(reply->str);
    freeReplyObject(reply);
    reply = NULL;

    return 0;
}

int save_channel_remaining(const char *channel_id,int cnt,int &resultValue)
{
    if( g_pRedisContext == NULL )
    {
        return -1;
    }

    char tmp[32] = {0};
    snprintf(tmp, sizeof(tmp), "%d", cnt);

    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"HINCRBY %s %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL_SEND_REMAINING,
                                                    channel_id,
                                                    tmp);
    if( reply == NULL )
    {
        if( !reconnect_db() )
        {
            LOG_ERROR("reconnect_db fail.\n");
            return -1;
        }
        reply = (redisReply *)redisCommand(g_pRedisContext,"HINCRBY %s %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL_SEND_REMAINING,
                                                    channel_id,
                                                    tmp);
        if( reply == NULL ) return -1;
    }
    if( reply->type != REDIS_REPLY_INTEGER )
    {
        LOG_ERROR("redisCommand error.type[%d]msg[%s]\n",reply->type,reply->str);
        freeReplyObject(reply);
        reply = NULL;
        return -1;
    }

    resultValue = reply->integer;
    
    freeReplyObject(reply);
    reply = NULL;
    return 0;
}


int save_channel_status(string value)
{
    if( g_pRedisContext == NULL )
    {
        return -1;
    }

    redisReply *reply = (redisReply *)redisCommand(g_pRedisContext,"RPUSH %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL_STATUS,
                                                    value.c_str());
    if( reply == NULL )
    {
        if( !reconnect_db() )
        {
            LOG_ERROR("reconnect_db fail.\n");
            return -1;
        }
        reply = (redisReply *)redisCommand(g_pRedisContext,"RPUSH %s %s",
                                                    DB_KEY_RD_SMS_CHANNEL_STATUS,
                                                    value.c_str());
        if( reply == NULL ) return -1;
    }
    if( reply->type != REDIS_REPLY_INTEGER )
    {
        LOG_ERROR("redisCommand error.type[%d]msg[%s]\n",reply->type,reply->str);
        freeReplyObject(reply);
        reply = NULL;
        return -1;
    }

    freeReplyObject(reply);
    reply = NULL;
    
    return 0;
}
