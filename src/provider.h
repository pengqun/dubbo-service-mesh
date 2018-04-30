#ifndef MESH_AGENT_NATIVE_PROVIDER_H
#define MESH_AGENT_NATIVE_PROVIDER_H

#include "common.h"
#include "pool.h"
#include "http_parser.h"
#include "ae.h"
#include "util.h"
#include "etcd.h"
#include "anet.h"

// Adjustable params
#define PROVIDER_HTTP_REQ_BUF_SIZE 4096
#define PROVIDER_DUBBO_REQ_BUF_SIZE 4096
#define PROVIDER_DUBBO_RESP_BUF_SIZE 512

// Consumer Agent <-> Agent
typedef struct connection_caa {
    int fd;
    char buf_in[PROVIDER_HTTP_REQ_BUF_SIZE];
    size_t nread_in;

    char *body;
    size_t len_body;

    char buf_req[PROVIDER_DUBBO_REQ_BUF_SIZE];
    int len_req;
    size_t nwrite_req;

    char buf_resp[PROVIDER_DUBBO_RESP_BUF_SIZE];
    size_t nread_resp;

    http_parser parser;
    bool processing;

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
