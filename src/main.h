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

#include "http_parser.h"

#define DEBUG_THIS_FILE

typedef struct connection {
    http_parser parser;
    int fd;
    char buf[128];
    size_t length;
    ssize_t written;
} connection;

typedef struct endpoint {

} endpoint;

#endif
