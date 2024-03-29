#ifndef MESH_AGENT_NATIVE_CONSUMER_H
#define MESH_AGENT_NATIVE_CONSUMER_H

#include "common.h"
#include "pool.h"
#include "ae.h"
#include "etcd.h"
#include "http_parser.h"
#include "anet.h"

// Adjustable params
#define CONSUMER_HTTP_REQ_BUF_SIZE 2048
#define CONSUMER_HTTP_RESP_BUF_SIZE 256
#define LATENCY_AWARE

// Consumer <-> Agent
typedef struct connection_ca {
    int fd;

    char buf_in[CONSUMER_HTTP_REQ_BUF_SIZE];
    ssize_t nread_in;
    ssize_t nwrite_in;

    char buf_out[CONSUMER_HTTP_RESP_BUF_SIZE];
    ssize_t nread_out;
    ssize_t nwrite_out;

    struct connection_apa *conn_apa;
} connection_ca_t;

// Agent <-> Provider Agent
typedef struct connection_apa {
    int fd;
    struct endpoint *endpoint;
#ifdef LATENCY_AWARE
    long req_start;
#endif
} connection_apa_t;

typedef struct endpoint {
    char *ip;
    int port;
#ifdef LATENCY_AWARE
    long total_ms;
    int num_reqs;
    int score;
#endif
    Pool *conn_pool; // pool of connection_apa
} endpoint_t;


void consumer_init();

void consumer_http_handler(aeEventLoop *event_loop, int fd);

void consumer_cleanup();

#endif //MESH_AGENT_NATIVE_CONSUMER_H
