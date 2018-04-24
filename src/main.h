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

#define NUM_CONN_PER_PROVIDER 64

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
    char buf[32];
    size_t length;
    ssize_t read;
} connection_out_t;

typedef struct endpoint {
    char *ip;
    int port;
    connection_out_t conections[NUM_CONN_PER_PROVIDER];
    aeEventLoop *event_loop;
    int pending;
} endpoint_t;

#endif
