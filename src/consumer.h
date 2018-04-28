#ifndef MESH_AGENT_NATIVE_CONSUMER_H
#define MESH_AGENT_NATIVE_CONSUMER_H

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#define DEBUG_THIS_FILE
#include "log.h"

#include "pool.h"
#include "ae.h"

// Consumer <-> Agent
typedef struct connection_ca {
    int fd;

    char buf_in[2048];
    ssize_t nread_in;
    ssize_t nwrite_in;

    char buf_out[128];
    ssize_t nread_out;
    ssize_t nwrite_out;

    struct connection_apa *conn_apa;
} connection_ca_t;

// Agent <-> Provider Agent
typedef struct connection_apa {
    int fd;
    struct endpoint *endpoint;
} connection_apa_t;

typedef struct endpoint {
    char *ip;
    int port;
    Pool *conn_pool; // pool of connection_apa
//    int pending;
} endpoint_t;


void consumer_init();

void consumer_http_handler(aeEventLoop *event_loop, int fd);

void consumer_cleanup();

#endif //MESH_AGENT_NATIVE_CONSUMER_H