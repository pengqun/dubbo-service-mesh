#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <microhttpd.h>
#include <curl/curl.h>

#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#define DEBUG_THIS_FILE

#include "log.h"
#include "etcd.h"
#include "util.h"
#include "ae.h"
#include "anet.h"
#include "http_parser.h"
#include "pool.h"

typedef struct connection_in {
    http_parser parser;
    int fd;
    char *buf;
    size_t length;
    ssize_t written;
} connection_in_t;

typedef struct connection_out {
    http_parser parser;
    int fd;
    char *buf;
    size_t length;
//    ssize_t read;
} connection_out_t;

typedef struct endpoint {
    char *ip;
    int port;
    Pool *conn_pool;
    aeEventLoop *event_loop;
} endpoint_t;

#endif
