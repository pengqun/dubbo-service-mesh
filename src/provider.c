//
// Created by Chu Heng on 2018/4/25.
//

#include "provider.h"
#include "util.h"
#include "etcd.h"


#define INTERFACE "en0"
//#define INTERFACE "eth0"

static char etcd_key[128];

static Pool *connection_caa_pool = NULL;

static http_parser_settings parser_settings;


void register_etcd_service(int server_port) ;
void deregister_etcd_service() ;

void read_from_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;

int on_http_url(http_parser *parser, const char *at, size_t length) ;
int on_http_body(http_parser *parser, const char *at, size_t length) ;
int on_http_complete(http_parser *parser) ;

void write_to_consumer_agent(aeEventLoop *el, int fd, void *privdata, int mask) ;

void provider_init(int server_port, int dubbo_port) {
    log_msg(INFO, "Provider init begin");
    register_etcd_service(server_port);

    log_msg(DEBUG, "Init connection pool for provider");
    connection_caa_pool = PoolInit(256, 256, sizeof(connection_caa_t), NULL, NULL, NULL, NULL, NULL);
    PoolPrintSaturation(connection_caa_pool);

    parser_settings.on_url = on_http_url;
    parser_settings.on_body = on_http_body;
    parser_settings.on_message_complete = on_http_complete;

    log_msg(INFO, "Provider init done");
}

void provider_cleanup() {
    log_msg(INFO, "Provider cleanup begin");
    deregister_etcd_service();
    log_msg(INFO, "Provider cleanup done");
}

void provider_http_handler(aeEventLoop *event_loop, int fd) {
    // Fetch a connection object from pool
    connection_caa_t *conn_caa = PoolGet(connection_caa_pool);
    memset(conn_caa, 0, sizeof(connection_caa_t));
    conn_caa->fd = fd;
    http_parser_init(&conn_caa->parser, HTTP_REQUEST);
    conn_caa->parser.data = conn_caa;
    conn_caa->event_loop = event_loop;

    // Read from consumer
    if (aeCreateFileEvent(event_loop, fd, AE_READABLE, read_from_consumer_agent, conn_caa) == AE_ERR) {
        log_msg(WARN, "Failed to create readable event for read_from_consumer_agent");
        PoolReturn(connection_caa_pool, conn_caa);
        close(fd);
    }
}

void read_from_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

    ssize_t nread = read(fd, conn_caa->buf + conn_caa->nread, sizeof(conn_caa->buf) - conn_caa->nread);
    if (nread > 0) {
        log_msg(DEBUG, "Read %d bytes from consumer agent for socket %d", nread, fd);

        size_t nparsed = http_parser_execute(&conn_caa->parser, &parser_settings,
                                             conn_caa->buf + conn_caa->nread, (size_t) nread);

        conn_caa->nread += nread;

        if (nparsed == nread) {
            return;
        }
        log_msg(WARN, "Failed to parse HTTP response from remote agent");
    }
    if (nread == 0) {
        log_msg(WARN, "Consumer agent closed connection");
    }
    if (nread < 0) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to read from consumer agent: %s", strerror(errno));
    }
    aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
    PoolReturn(connection_caa_pool, conn_caa);
    close(fd);
}

int on_http_url(http_parser *parser, const char *at, size_t length) {
    log_msg(DEBUG, "On HTTP URL: %.*s", length, at);
    return 0;
}

int on_http_body(http_parser *parser, const char *at, size_t length) {
    log_msg(DEBUG, "On HTTP body: %.*s", length, at);
    connection_caa_t *conn_caa = parser->data;
//    conn->buf_in = strrchr(at, '=') + 1;
//    conn->len_in = length - (conn->buf_in - at);
//    log_msg(DEBUG, "Buf: %.*s", conn->len_in, conn->buf_in);
//    log_msg(DEBUG, "Received buf_in len_in: %d", conn->len_in);
    return 0;
}

int on_http_complete(http_parser *parser) {
    log_msg(DEBUG, "On HTTP complete");
    connection_caa_t *conn_caa = parser->data;
    http_parser_init(&conn_caa->parser, HTTP_REQUEST);

    // TODO call local dubbo provider

    // Write to consumer agent
    if (aeCreateFileEvent(conn_caa->event_loop, conn_caa->fd, AE_WRITABLE, write_to_consumer_agent, conn_caa) == AE_ERR) {
        log_msg(WARN, "Failed to create writable event for write_to_consumer_agent");
        PoolReturn(connection_caa_pool, conn_caa);
        close(conn_caa->fd);
    }

    return 0;
}

void write_to_consumer_agent(aeEventLoop *el, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

    char *data = "OK";
    char buf[128];
    sprintf(buf, "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %ld\r\n"
                 "\r\n"
                 "%s", strlen(data), data);

    ssize_t nwrite = write(conn_caa->fd, buf, strlen(buf));

    if (nwrite >= 0) {
        log_msg(DEBUG, "Write %d bytes to consumer agent for socket %d", nwrite, fd);

        if (nwrite == strlen(buf)) {
            aeDeleteFileEvent(el, fd, AE_WRITABLE);
        }
    }

    if (nwrite == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to write to consumer agent: %s", strerror(errno));
        aeDeleteFileEvent(el, fd, AE_WRITABLE | AE_READABLE);
        close(fd);
    }
}

void register_etcd_service(int server_port) {
    char *ip_addr = get_local_ip_addr(INTERFACE);
    log_msg(INFO, "Local IP address: %s", ip_addr);

    sprintf(etcd_key, "/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/%s:%d",
            ip_addr, server_port);

    int ret = etcd_set(etcd_key, "", 600, 0);
    if (ret != 0) {
        log_msg(FATAL, "Failed to do etcd_set: %d", ret);
    }
    log_msg(INFO, "Register service at: %s", etcd_key);
}

void deregister_etcd_service() {
    int ret = etcd_del(etcd_key);
    if (ret != 0) {
        log_msg(WARN, "Failed to do etcd_del: %d", ret);
    }
    log_msg(INFO, "Deregister service at: %s", etcd_key);
}
