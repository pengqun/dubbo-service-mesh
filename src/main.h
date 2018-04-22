#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <microhttpd.h>
#include <memory.h>
#include "http_parser.h"

typedef struct connection {
    http_parser parser;
    int fd;
    char buf[128];
    size_t length;
    ssize_t written;
} connection;

#endif
