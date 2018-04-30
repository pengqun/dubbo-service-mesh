#include "provider.h"

//#define INTERFACE "en0"
//#define INTERFACE "eth0"
#define INTERFACE "docker0"

// Adjustable params
#define NUM_CONN_FOR_CONSUMER 1024
#define NUM_CONN_TO_PROVIDER 1024

static char neterr[256];
static char etcd_key[128];

static char resp_buffer[128];
static size_t pre_len = 0;

static Pool *connection_caa_pool = NULL;
static Pool *connection_ap_pool = NULL;

static http_parser_settings parser_settings;

static uint32_t cur_request_id = 1;


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

void abort_connection_caa(aeEventLoop *event_loop, connection_caa_t *conn_caa) ;

char *url_decode(char *target, const char *str, int length) ;

void provider_init(int server_port, int dubbo_port) {
    log_msg(INFO, "Provider init begin");
    register_etcd_service(server_port);

    log_msg(INFO, "Init HTTP connection pool");
    connection_caa_pool = PoolInit(NUM_CONN_FOR_CONSUMER, NUM_CONN_FOR_CONSUMER,
                                   sizeof(connection_caa_t), NULL, NULL, NULL, NULL, NULL);
    PoolPrintSaturation(connection_caa_pool);

    log_msg(INFO, "Init Dubbo connection pool");
    connection_ap_pool = PoolInit(NUM_CONN_TO_PROVIDER, NUM_CONN_TO_PROVIDER, sizeof(connection_caa_t),
                                   NULL, init_connection_ap, (void *) (uint64_t) dubbo_port, cleanup_connection_ap, NULL);
    PoolPrintSaturation(connection_ap_pool);

    parser_settings.on_url = on_http_url;
    parser_settings.on_body = on_http_body;
    parser_settings.on_message_complete = on_http_complete;

    sprintf(resp_buffer, "HTTP/1.1 200 OK\r\nContent-Length:");
    pre_len = strlen(resp_buffer);

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
    if (UNLIKELY(conn_caa == NULL)) {
        log_msg(ERR, "No connection object available, abort connection");
        close(fd);
        return;
    }
    log_msg(DEBUG, "Fetched connection object from pool, active: %d", connection_caa_pool->outstanding);

    memset(conn_caa, 0, sizeof(connection_caa_t));
    conn_caa->fd = fd;
    conn_caa->event_loop = event_loop;

    http_parser_init(&conn_caa->parser, HTTP_REQUEST);
    conn_caa->parser.data = conn_caa;

    // Read from consumer agent
    if (UNLIKELY(aeCreateFileEvent(event_loop, fd, AE_READABLE, read_from_consumer_agent, conn_caa) == AE_ERR)) {
        log_msg(FATAL, "Failed to create readable event for read_from_consumer_agent");
        abort_connection_caa(event_loop, conn_caa);
    }
}

void read_from_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

    ssize_t nread = read(fd, conn_caa->buf_in + conn_caa->nread_in,
                         sizeof(conn_caa->buf_in) - conn_caa->nread_in);

    if (LIKELY(nread > 0)) {
        log_msg(DEBUG, "Read %d bytes from consumer agent for socket %d", nread, fd);

        // Feed input to HTTP parser
        size_t nparsed = http_parser_execute(&conn_caa->parser, &parser_settings,
                                             conn_caa->buf_in + conn_caa->nread_in, (size_t) nread);
        conn_caa->nread_in += nread;

        if (UNLIKELY(nparsed != nread)) {
            log_msg(ERR, "Failed to parse HTTP response from remote agent");
            abort_connection_caa(event_loop, conn_caa);
        }

    } else if (UNLIKELY(nread < 0)) {
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from consumer agent: %s", strerror(errno));
        abort_connection_caa(event_loop, conn_caa);

    } else {
        // Also feed zero input to HTTP parser
        http_parser_execute(&conn_caa->parser, &parser_settings,
                            conn_caa->buf_in + conn_caa->nread_in, (size_t) nread);

        log_msg(ERR, "Consumer agent closed connection for socket %d", fd);
        close(fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        PoolReturn(connection_caa_pool, conn_caa);
        log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_caa_pool->outstanding);
    }
}

int on_http_url(http_parser *parser, const char *at, size_t length) {
//    log_msg(DEBUG, "On HTTP URL: %.*s", length, at);
    return 0;
}

int on_http_body(http_parser *parser, const char *at, size_t length) {
//    log_msg(DEBUG, "On HTTP body: %.*s", length, at);
    connection_caa_t *conn_caa = parser->data;
    conn_caa->body = (char *) at;
    conn_caa->len_body = length;
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
    buf += 4;
    *((uint32_t *) buf) = htonl(cur_request_id);
    buf += 4;

    // data length
    char *buf_len = buf;
    buf += 4;

    // real data
    char *body = conn_caa->body;
    size_t len_body = conn_caa->len_body;

    // XXX can do better
    char *service = strchr(body, '=') + 1;
    body = strchr(service, '&');
    int service_len = (int) (body - service);
    char *method = strchr(body, '=') + 1;
    body = strchr(method, '&');
    int method_len = (int) (body - method);
    char *type = strchr(body, '=') + 1;
    body = strchr(type, '&');
    int type_len = (int) (body - type);
    char *arg = strchr(body, '=') + 1;
    int arg_len = (int) (conn_caa->body + len_body - arg);

//    // url decode for parameter type
//    char decoded[128];
//    url_decode(decoded, type, type_len);

    // No runtime decoding, just look up pre-defined mapping (or better: prefix tree)
    if (LIKELY(strncmp(type, "Ljava%2Flang%2FString%3B", (size_t) type_len) == 0)) {
        type = "Ljava/lang/String;";
        type_len = (int) strlen(type);
    }

    /*
       "\"2.0.1\"\n"  // dubbo version
       "\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"\n" // service name
       "null\n"  // service version
       "\"hash\"\n" // method name
       "\"Ljava/lang/String;\"\n" // method parameter types
       "\"123ab\"\n" // method arguments
       "{\"path\":\"com.alibaba.dubbo.performance.demo.provider.IHelloService\"}\n" // attachments
     */

    int data_len = sprintf(buf,
                           "\"2.0.1\"\n"  // dubbo version
                           "\"%.*s\"\n"   // service name
                           "null\n"       // service version
                           "\"%.*s\"\n"   // method name
                           "\"%.*s\"\n"   // method parameter types
                           "\"%.*s\"\n"   // method arguments
                           "{\"path\":\"%.*s\"}\n", // attachments
                           service_len, service, method_len, method, type_len, type, arg_len, arg,
                           service_len, service
    );
//    log_msg(DEBUG, "Request: %.*s", data_len, buf);

    // Re-fill data length field
    *((uint32_t *) buf_len) = htonl(data_len);

    conn_caa->len_req = data_len + 16;

    log_msg(DEBUG, "Current requestID: %d", cur_request_id);
    ++cur_request_id;

#if 0
    // For testing: skip dubbo and return imediatly
    if (aeCreateFileEvent(conn_caa->event_loop, conn_caa->fd, AE_WRITABLE, write_to_consumer_agent, conn_caa) == AE_ERR) {
        log_msg(ERR, "Failed to create writable event for write_to_consumer_agent");
        close(conn_caa->fd);
    }
#else

    connection_ap_t *conn_ap = conn_caa->conn_ap;
    if (UNLIKELY(conn_ap == NULL)) {
        // Binding connection to local provider
        conn_ap = PoolGet(connection_ap_pool);
        if (conn_ap == NULL) {
            log_msg(FATAL, "No connection to local provider available");
            abort_connection_caa(conn_caa->event_loop, conn_caa);
        }
        conn_caa->conn_ap = conn_ap;
        log_msg(DEBUG, "Binding connection %d to %d", conn_caa->fd, conn_ap->fd);
    }

    // Write to local dubbo provider
    if (UNLIKELY(aeCreateFileEvent(conn_caa->event_loop, conn_ap->fd, AE_WRITABLE, write_to_local_provider, conn_caa) == AE_ERR)) {
        log_msg(ERR, "Failed to create writable event for write_to_local_provider");
        abort_connection_caa(conn_caa->event_loop, conn_caa);
    }

    // Read from local dubbo provider
    if (UNLIKELY(aeCreateFileEvent(conn_caa->event_loop, conn_ap->fd, AE_READABLE, read_from_local_provider, conn_caa) == AE_ERR)) {
        log_msg(ERR, "Failed to create readable event for read_from_local_provider");
        abort_connection_caa(conn_caa->event_loop, conn_caa);
    }
#endif

    return 0;
}

//void dump_conn(connection_caa_t *conn_caa) {
//    log_msg(DEBUG, "%d %d", conn_caa->nwrite_req, conn_caa->len_req);
//}

void write_to_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

    ssize_t nwrite = write(fd, conn_caa->buf_req + conn_caa->nwrite_req,
                           (size_t) (conn_caa->len_req - conn_caa->nwrite_req));

    if (LIKELY(nwrite >= 0)) {
        log_msg(DEBUG, "Write %d bytes to local provider for socket %d", nwrite, fd);
        conn_caa->nwrite_req += nwrite;
        if (conn_caa->nwrite_req == conn_caa->len_req) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
        }
    } else {
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to write to local provider with socket %d: %s", fd, strerror(errno));
        abort_connection_caa(event_loop, conn_caa);
    }
}

void read_from_local_provider(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

    ssize_t nread = read(fd, conn_caa->buf_resp + conn_caa->nread_resp,
                         sizeof(conn_caa->buf_resp) - conn_caa->nread_resp);

    if (LIKELY(nread > 0)) {
        log_msg(DEBUG, "Read %d bytes from local provider for socket %d", nread, fd);
        conn_caa->nread_resp += nread;

        if (conn_caa->nread_resp > 16) {
            // Full header available
            uint32_t data_len = ntohl(*((uint32_t *) &conn_caa->buf_resp[12]));
            log_msg(DEBUG, "Got data_len %d", data_len);

            // TODO give up?
            if (conn_caa->nread_resp < 16 + data_len) {
                log_msg(WARN, "Incomplete response");
                return;
            }

            // Write back to consumer agent
            if (aeCreateFileEvent(event_loop, conn_caa->fd, AE_WRITABLE, write_to_consumer_agent, conn_caa) == AE_ERR) {
                log_msg(ERR, "Failed to create writable event for write_to_consumer_agent");
                abort_connection_caa(event_loop, conn_caa);
            }
        }

    } else if (UNLIKELY(nread < 0)) {
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from local provider: %s", strerror(errno));
        abort_connection_caa(event_loop, conn_caa);
    } else {
        log_msg(ERR, "Local provider closed connection");
        abort_connection_caa(event_loop, conn_caa);
    }
}

void write_to_consumer_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_caa_t *conn_caa = privdata;

//    char *data = "hah";
    char *data = conn_caa->buf_resp + 18;
    conn_caa->buf_resp[conn_caa->nread_resp - 1] = '\0'; // ignore last newline
    size_t data_len = conn_caa->nread_resp - 19;

    int add_len = sprintf(resp_buffer + pre_len, "%ld\r\n\r\n%s", data_len, data);
    size_t buf_len = pre_len + add_len;
//    log_msg(DEBUG, "Response: %.*s", buf_len, resp_buffer);

    ssize_t nwrite = write(fd, resp_buffer, buf_len);

    if (LIKELY(nwrite >= 0)) {
        log_msg(DEBUG, "Write %d bytes to consumer agent for socket %d", nwrite, fd);

        if (LIKELY(nwrite == buf_len)) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
            http_parser_init(&conn_caa->parser, HTTP_REQUEST);

            // Reset buf pointer
            conn_caa->nread_in = 0;
            conn_caa->len_body = 0;
            conn_caa->len_req = 0;
            conn_caa->nwrite_req = 0;
            conn_caa->nread_resp = 0;

        } else {
            // XXX
            log_msg(WARN, "Partial write for %d", fd);
        }
    } else {
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to write to consumer agent: %s", strerror(errno));
        abort_connection_caa(event_loop, conn_caa);
    }
}

void register_etcd_service(int server_port) {
    char *ip_addr = get_local_ip_addr(INTERFACE);
    log_msg(INFO, "Local IP address: %s", ip_addr);

    sprintf(etcd_key, "/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/%s:%d",
            ip_addr, server_port);

    int ret = etcd_set(etcd_key, "", 3600, 0);
    if (ret != 0) {
        log_msg(ERR, "Failed to do etcd_set: %d", ret);
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

    int fd;
    do {
        fd = anetTcpConnect(neterr, addr, port);
        if (fd < 0) {
            log_msg(WARN, "Failed to connect to local provider %s:%d - %s, Sleep 1 seconds to retry later",
                    addr, port, neterr);
            sleep(1);
        } else {
            anetNonBlock(NULL, fd);
            anetEnableTcpNoDelay(NULL, fd);
            conn_ap->fd = fd;
            log_msg(DEBUG, "Build connection to local provider %s:%d with socket %d", addr, port, fd);
        }
    } while (fd < 0);

    return 1;
}

void cleanup_connection_ap(void *elem) {
    log_msg(DEBUG, "Cleanup connection to local provider");
    connection_ap_t *conn_ap = elem;
    if (conn_ap->fd > 0) {
        close(conn_ap->fd);
    }
}

void abort_connection_caa(aeEventLoop *event_loop, connection_caa_t *conn_caa) {
    close(conn_caa->fd);
    aeDeleteFileEvent(event_loop, conn_caa->fd, AE_WRITABLE | AE_READABLE);
    log_msg(ERR, "Abort connection to consumer agent with socket: %d", conn_caa->fd);

    connection_ap_t *conn_ap = conn_caa->conn_ap;
    if (conn_ap != NULL) {
        close(conn_ap->fd);
        aeDeleteFileEvent(event_loop, conn_ap->fd, AE_WRITABLE | AE_READABLE);
        log_msg(ERR, "Abort connection to local provider with socket: %d", conn_ap->fd);
    }

    PoolReturn(connection_caa_pool, conn_caa);
    log_msg(DEBUG, "Released connection object to pool, active: %d", connection_caa_pool->outstanding);
}
