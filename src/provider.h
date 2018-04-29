#ifndef MESH_AGENT_NATIVE_PROVIDER_H
#define MESH_AGENT_NATIVE_PROVIDER_H

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

//#define DEBUG_THIS_FILE
#include "log.h"

#include "pool.h"
#include "http_parser.h"
#include "ae.h"

// 1. For provider agent

// Consumer Agent <-> Agent
typedef struct connection_caa {
    int fd;
    char buf_in[2048];
    size_t nread_in;

    char *body;
    size_t len_body;

    char buf_req[2048];
    int len_req;
    size_t nwrite_req;

    char buf_resp[128];
    size_t nread_resp;

    http_parser parser;
    aeEventLoop *event_loop;
    struct connection_ap *conn_ap;
} connection_caa_t;

// Agent <-> Provider
typedef struct connection_ap {
    int fd;
} connection_ap_t;

void provider_init(int server_port, int dubbo_port);

void provider_http_handler(aeEventLoop *event_loop, int fd);

void provider_cleanup();

#endif //MESH_AGENT_NATIVE_PROVIDER_H
