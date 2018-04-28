//
// Created by Chu Heng on 2018/4/25.
//

#include "provider.h"
#include "util.h"
#include "etcd.h"
#include "anet.h"
//#include <jansson.h>


#define INTERFACE "en0"
//#define INTERFACE "eth0"

#define NUM_CONN_FOR_CONSUMER 8
#define NUM_CONN_TO_PROVIDER NUM_CONN_FOR_CONSUMER

static char neterr[256];

static char etcd_key[128];

static Pool *connection_caa_pool = NULL;
static Pool *connection_ap_pool = NULL;

static http_parser_settings parser_settings;

static uint64_t cur_request_id = 1;


void register_etcd_service(int server_port) ;
void deregister_etcd_service() ;

int on_http_url(http_parser *parser, const char *at, size_t length) ;
int on_http_body(http_parser *parser, const char *at, size_t length) ;
int on_http_complete(http_parser *parser) ;

int init_connection_ap(void *elem, void *data) ;
void cleanup_connection_ap(void *elem) ;

void read_from_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void read_from_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;

void provider_init(int server_port, int dubbo_port) {
    log_msg(INFO, "Provider init begin");
    register_etcd_service(server_port);

    log_msg(DEBUG, "Init HTTP connection pool for provider");
    connection_caa_pool = PoolInit(NUM_CONN_FOR_CONSUMER, NUM_CONN_FOR_CONSUMER,
                                   sizeof(connection_caa_t), NULL, NULL, NULL, NULL, NULL);
    PoolPrintSaturation(connection_caa_pool);

    log_msg(DEBUG, "Init Dubbo connection pool for provider");
    connection_ap_pool = PoolInit(NUM_CONN_TO_PROVIDER, NUM_CONN_TO_PROVIDER, sizeof(connection_caa_t),
                                   NULL, init_connection_ap, (void *) (uint64_t) dubbo_port, cleanup_connection_ap, NULL);
    PoolPrintSaturation(connection_ap_pool);

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
//    connection_caa_t *conn_caa = malloc(sizeof(connection_caa_t));
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

    ssize_t nread = read(fd, conn_caa->buf_in + conn_caa->nread_in, sizeof(conn_caa->buf_in) - conn_caa->nread_in);
    if (nread >= 0) {
        log_msg(DEBUG, "Read %d bytes from consumer agent for socket %d", nread, fd);

        size_t nparsed = http_parser_execute(&conn_caa->parser, &parser_settings,
                                             conn_caa->buf_in + conn_caa->nread_in, (size_t) nread);
        conn_caa->nread_in += nread;

        if (nread > 0 && nparsed == nread) {
            return;
        }

        if (nread == 0) {
            log_msg(WARN, "Consumer agent closed connection");
        } else {
            log_msg(WARN, "Failed to parse HTTP response from remote agent");
        }
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
//    log_msg(DEBUG, "On HTTP URL: %.*s", length, at);
    return 0;
}

int on_http_body(http_parser *parser, const char *at, size_t length) {
//    log_msg(DEBUG, "On HTTP body: %.*s", length, at);
    connection_caa_t *conn_caa = parser->data;
//    conn->buf_in = strrchr(at, '=') + 1;
//    conn->len_in = length - (conn->buf_in - at);
//    log_msg(DEBUG, "Buf: %.*s", conn->len_in, conn->buf_in);
//    log_msg(DEBUG, "Received buf_in len_in: %d", conn->len_in);
    return 0;
}

int on_http_complete(http_parser *parser) {
//    log_msg(DEBUG, "On HTTP complete");
    connection_caa_t *conn_caa = parser->data;

    // Assemble request buf
    char *buf = conn_caa->buf_req;

    // magic & flags
    *((uint16_t *) buf) = htons(0xdabb);
    buf[2] = (char) (0xc0 | 6);
    buf += 4;

    // request id
    *((uint64_t *) buf) = htonll(cur_request_id);
    buf += 8;

    // data length
    char *buf_len = buf;
    buf += 4;

    int data_len = sprintf(buf,
            "\"2.0.1\"\n"  // dubbo version
            "\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"\n" // service name
            "null\n"  // service version
            "\"hash\"\n" // method name
            "\"Ljava/lang/String;\"\n" // method parameter types
            "\"123ab\"\n" // method arguments
            "{\"path\":\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"}\n" // attachments
    );
    *((uint32_t *) buf_len) = htonl(data_len);

    conn_caa->len_req = data_len + 16;

//    sprintf(buf, "\"2.0.1\"\n");
//    // service name
//    sprintf(buf, "\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"\n");
//    // service version
//    sprintf(buf, );
//    // method name
//    sprintf(buf, "\"hash\"\n");
//    // method parameter types
//    sprintf(buf, "\"Ljava/lang/String;\"\n");
//    // method arguments
//    sprintf(buf, "\"123ab\"\n");
//    // attachments
//    sprintf(buf, "{\"path\":\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"}\n");

    ++cur_request_id;
    log_msg(DEBUG, "Next requestID: %d", cur_request_id);

    // Reset buf pointer
    conn_caa->nread_in = 0;

    // Fetch a connection to local provider
    connection_ap_t *conn_ap = PoolGet(connection_ap_pool);
    if (conn_ap == NULL) {
        log_msg(WARN, "No connection to provider available, abort");
        PoolReturn(connection_caa_pool, conn_caa);
        close(conn_caa->fd);
        return 0;
    }
    conn_caa->conn_ap = conn_ap;

    // Write to local dubbo provider
    if (aeCreateFileEvent(conn_caa->event_loop, conn_ap->fd, AE_WRITABLE, write_to_local_provider, conn_caa) == AE_ERR) {
        log_msg(WARN, "Failed to create writable event for write_to_local_provider");
        PoolReturn(connection_caa_pool, conn_caa);
        close(conn_caa->fd);
    }

    // Read from local dubbo provider
    if (aeCreateFileEvent(conn_caa->event_loop, conn_ap->fd, AE_READABLE, read_from_local_provider, conn_caa) == AE_ERR) {
        log_msg(WARN, "Failed to create readable event for read_from_local_provider");
        PoolReturn(connection_caa_pool, conn_caa);
        close(conn_caa->fd);
    }

    return 0;
}

void write_to_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;
//    connection_ap_t *conn_ap = conn_caa->conn_ap;

    ssize_t nwrite = write(fd, conn_caa->buf_req + conn_caa->nwrite_req,
                           (size_t) (conn_caa->len_req - conn_caa->nwrite_req));

    if (nwrite >= 0) {
        log_msg(DEBUG, "Write %d bytes to local provider for socket %d", nwrite, fd);
        conn_caa->nwrite_req += nwrite;
        if (conn_caa->nwrite_req == conn_caa->len_req) {
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);

            // Reset buf pointers
            conn_caa->len_req = 0;
            conn_caa->nwrite_req = 0;
        }
    }

    if (nwrite == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to write to local provider: %s", strerror(errno));
        aeDeleteFileEvent(event_loop, conn_caa->fd, AE_WRITABLE | AE_READABLE);
        close(conn_caa->fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        close(fd);
    }
}

void read_from_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;
//    connection_ap_t *conn_ap = conn_caa->conn_ap;

    ssize_t nread = read(fd, conn_caa->buf_resp + conn_caa->nread_resp, sizeof(conn_caa->buf_resp) - conn_caa->nread_resp);
    if (nread > 0) {
        log_msg(DEBUG, "Read %d bytes from local provider for socket %d", nread, fd);

        conn_caa->nread_resp += nread;

        if (conn_caa->nread_resp > 16) {

            uint32_t data_len = ntohl(*((uint32_t *) &conn_caa->buf_resp[12]));
            log_msg(DEBUG, "Got data_len %d", data_len);

            if (conn_caa->nread_resp < 16 + data_len) {
                log_msg(DEBUG, "Incomplete response");
                return;
            }

            // Write to consumer agent
            if (aeCreateFileEvent(conn_caa->event_loop, conn_caa->fd, AE_WRITABLE, write_to_consumer_agent, conn_caa) == AE_ERR) {
                log_msg(WARN, "Failed to create writable event for write_to_consumer_agent");
                PoolReturn(connection_caa_pool, conn_caa);
                close(conn_caa->fd);
            }
            return;
        }

    }

    if (nread == 0) {
        log_msg(WARN, "Local provider closed connection");
    }
    if (nread < 0) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to read from local provider: %s", strerror(errno));
    }
    aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
    close(fd);
    aeDeleteFileEvent(event_loop, conn_caa->fd, AE_WRITABLE | AE_READABLE);
    close(conn_caa->fd);
}

void write_to_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;
//    connection_ap_t *conn_ap = conn_caa->conn_ap;

//    char *data = "REDIS";

    char *data = conn_caa->buf_resp + 18;
    conn_caa->buf_resp[conn_caa->nread_resp] = '\0';

    char buf[128];
    sprintf(buf, "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %ld\r\n"
                 "\r\n"
                 "%s", strlen(data), data);

    ssize_t nwrite = write(fd, buf, strlen(buf));

    if (nwrite >= 0) {
        log_msg(DEBUG, "Write %d bytes to consumer agent for socket %d", nwrite, fd);

        if (nwrite == strlen(buf)) {
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
            http_parser_init(&conn_caa->parser, HTTP_REQUEST);

            // Reset buf pointer
            conn_caa->nread_resp = 0;
            conn_caa->nread_resp = 0;
        } else {
            log_msg(DEBUG, "Partial write for %d", fd);
        }
    }

    if (nwrite == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to write to consumer agent: %s", strerror(errno));
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        close(fd);
    }
}

void register_etcd_service(int server_port) {
    char *ip_addr = get_local_ip_addr(INTERFACE);
    log_msg(INFO, "Local IP address: %s", ip_addr);

    sprintf(etcd_key, "/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/%s:%d",
            ip_addr, server_port);

    int ret = etcd_set(etcd_key, "", 3600, 0);
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

int init_connection_ap(void *elem, void *data) {
    connection_ap_t *conn_ap = elem;
    char *addr = "127.0.0.1";
    int port = (int) data;

    int fd = anetTcpNonBlockConnect(neterr, addr, port);
    if (errno != EINPROGRESS) {
        log_msg(ERR, "Failed to connect to local provider %s:%d", addr, port);
        close(fd);
        conn_ap->fd = -1;
    } else {
        anetEnableTcpNoDelay(NULL, fd);
        log_msg(DEBUG, "Build connection to local provider %s:%d", addr, port);
        conn_ap->fd = fd;
    }
    return 1;
}

void cleanup_connection_ap(void *elem) {
    log_msg(DEBUG, "Cleanup connection to local provider");
    connection_ap_t *conn_ap = elem;
    if (conn_ap->fd > 0) {
        close(conn_ap->fd);
    }
}
