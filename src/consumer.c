#include "consumer.h"

// Adjustable params
#define NUM_CONN_FOR_CONSUMER 2048
#define NUM_CONN_PER_PROVIDER 1024
//#define LOAD_BALANCE_THRESHOLD 10

static char neterr[256];

static endpoint_t endpoints[3];
static int num_endpoints = 0;
//static int round_robin_id = 0;

static Pool *connection_ca_pool = NULL;


void discover_etcd_services() ;
void on_etcd_service_endpoint(const char *key, const char *value, void *arg);

int init_connection_apa(void *elem, void *data) ;
void cleanup_connection_apa(void *elem) ;

void read_from_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
bool _write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata) ;
void read_from_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
bool _write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata) ;

void abort_connection_ca(aeEventLoop *event_loop, connection_ca_t *conn_ca) ;

void consumer_init() {
    log_msg(INFO, "Consumer init begin");
    discover_etcd_services();

    log_msg(INFO, "Init connection pool for consumer");
    connection_ca_pool = PoolInit(NUM_CONN_FOR_CONSUMER, NUM_CONN_FOR_CONSUMER,
                                  sizeof(connection_ca_t), NULL, NULL, NULL, NULL, NULL);
    PoolPrintSaturation(connection_ca_pool);

    log_msg(INFO, "Consumer init done");
}

void consumer_cleanup() {
    log_msg(INFO, "Consumer cleanup done");
}

void consumer_http_handler(aeEventLoop *event_loop, int fd) {
    // Fetch a connection object from pool
    connection_ca_t *conn_ca = PoolGet(connection_ca_pool);
    if (UNLIKELY(conn_ca == NULL)) {
        log_msg(ERR, "No connection object available, abort connection");
        close(fd);
        return;
    }
    log_msg(DEBUG, "Fetched connection object from pool, active: %d", connection_ca_pool->outstanding);

    memset(conn_ca, 0, sizeof(connection_ca_t));
    conn_ca->fd = fd;

    // Read from consumer
    if (UNLIKELY(aeCreateFileEvent(event_loop, fd, AE_READABLE, read_from_consumer, conn_ca) == AE_ERR)) {
        log_msg(ERR, "Failed to create readable event for read_from_consumer, socket %d", fd);
        abort_connection_ca(event_loop, conn_ca);
    }
}

void read_from_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nread = read(fd, conn_ca->buf_in + conn_ca->nread_in,
                         sizeof(conn_ca->buf_in) - conn_ca->nread_in);

    if (LIKELY(nread > 0)) {
        log_msg(DEBUG, "Read %d bytes from consumer for socket %d", nread, fd);
        conn_ca->nread_in += nread;

        // Reset buf_out pointers
        conn_ca->nread_out = 0;
        conn_ca->nwrite_out = 0;

        // Get connection to remote agent
        connection_apa_t *conn_apa = conn_ca->conn_apa;
        if (LIKELY(conn_apa == NULL)) {
            // Load balance: least-loaded
            endpoint_t *endpoint = &endpoints[0];
            int min_outstanding = endpoints[0].conn_pool->outstanding;
#ifdef LOAD_BALANCE_THRESHOLD
            if (min_outstanding > LOAD_BALANCE_THRESHOLD) {
#endif
                for (int i = 1; i < num_endpoints; i++) {
                    if (min_outstanding > endpoints[i].conn_pool->outstanding) {
                        min_outstanding = endpoints[i].conn_pool->outstanding;
                        endpoint = &endpoints[i];
                    }
                }
#ifdef LOAD_BALANCE_THRESHOLD
            }
#endif
            conn_apa = PoolGet(endpoint->conn_pool);
            if (UNLIKELY(conn_apa == NULL)) {
                log_msg(ERR, "No connection to remote agent available for socket %d", fd);
                abort_connection_ca(event_loop, conn_ca);
                return;
            }
            conn_ca->conn_apa = conn_apa;
            log_msg(DEBUG, "Pick up connection to %s:%d with socket %d and outstanding %d",
                    conn_apa->endpoint->ip, conn_apa->endpoint->port, conn_apa->fd, min_outstanding);
        }

//        if (UNLIKELY(!_write_to_remote_agent(event_loop, conn_apa->fd, conn_ca))) {
            // Write to remote agent
            if (UNLIKELY(aeCreateFileEvent(event_loop, conn_apa->fd, AE_WRITABLE, write_to_remote_agent, conn_ca) ==
                         AE_ERR)) {
                log_msg(ERR, "Failed to create writable event for write_to_remote_agent: %s", strerror(errno));
                abort_connection_ca(event_loop, conn_ca);
            }
//        }

        // Read from remote agent
        if (UNLIKELY(aeCreateFileEvent(event_loop, conn_apa->fd, AE_READABLE, read_from_remote_agent, conn_ca) == AE_ERR)) {
            log_msg(ERR, "Failed to create readable event for read_from_remote_agent: %s", strerror(errno));
            abort_connection_ca(event_loop, conn_ca);
        }

    } else if (UNLIKELY(nread < 0)) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read_from_consumer: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from consumer: %s", strerror(errno));
        abort_connection_ca(event_loop, conn_ca);

    } else {
        log_msg(DEBUG, "Consumer closed connection for socket %d", fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        PoolReturn(connection_ca_pool, conn_ca);
        close(fd);
        log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_ca_pool->outstanding);
    }
}

void write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    _write_to_remote_agent(event_loop, fd, privdata);
}

bool _write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nwrite = write(fd, conn_ca->buf_in + conn_ca->nwrite_in,
                           (size_t) (conn_ca->nread_in - conn_ca->nwrite_in));

    if (LIKELY(nwrite >= 0)) {
        log_msg(DEBUG, "Write %d bytes to remote agent for socket %d", nwrite, fd);
        conn_ca->nwrite_in += nwrite;
        if (LIKELY(conn_ca->nwrite_in == conn_ca->nread_in)) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
        } else {
            log_msg(WARN, "Partial write for socket %d", fd);
        }
    } else {
//        if (UNLIKELY(errno == EWOULDBLOCK)) {
//            log_msg(WARN, "write_to_remote_agent would block");
//            return false;
//        }
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
            return true;
        }
        log_msg(ERR, "Failed to write to remote agent: %s", strerror(errno));
        abort_connection_ca(event_loop, conn_ca);
    }
    return true;
}

void read_from_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nread = read(fd, conn_ca->buf_out + conn_ca->nread_out,
                         sizeof(conn_ca->buf_out) - conn_ca->nread_out);

    if (LIKELY(nread > 0)) {
        log_msg(DEBUG, "Read %d bytes from remote agent for socket %d", nread, fd);
        conn_ca->nread_out += nread;

        // Reset buf_in pointers
        conn_ca->nread_in = 0;
        conn_ca->nwrite_in = 0;

        // Write back to consumer
//        if (UNLIKELY(!_write_to_consumer(event_loop, conn_ca->fd, conn_ca))) {
            if (aeCreateFileEvent(event_loop, conn_ca->fd, AE_WRITABLE, write_to_consumer, conn_ca) == AE_ERR) {
                log_msg(ERR, "Failed to create writable event for write_to_consumer: %s", strerror(errno));
                abort_connection_ca(event_loop, conn_ca);
            }
//        }

    } else if (UNLIKELY(nread < 0)) {
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on read_from_remote_agent: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from remote agent: %s", strerror(errno));
        abort_connection_ca(event_loop, conn_ca);

    } else {
        log_msg(ERR, "Remote agent from %s:%d closed connection for socket %d",
                conn_ca->conn_apa->endpoint->ip, conn_ca->conn_apa->endpoint->port, fd);
        abort_connection_ca(event_loop, conn_ca);
    }
}

void write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    _write_to_consumer(event_loop, fd, privdata);
}

bool _write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nwrite = write(fd, conn_ca->buf_out + conn_ca->nwrite_out,
                           (size_t) (conn_ca->nread_out - conn_ca->nwrite_out));

    if (LIKELY(nwrite >= 0)) {
        log_msg(DEBUG, "Write %d bytes to consumer for socket %d", nwrite, fd);
        conn_ca->nwrite_out += nwrite;

        if (LIKELY(conn_ca->nwrite_out == conn_ca->nread_out)) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);

            // Release current connection to remote agent
            connection_apa_t *conn_apa = conn_ca->conn_apa;
            if (LIKELY(conn_apa != NULL)) {
                aeDeleteFileEvent(event_loop, conn_apa->fd, AE_WRITABLE | AE_READABLE);
                PoolReturn(conn_apa->endpoint->conn_pool, conn_apa);
                conn_ca->conn_apa = NULL;
                log_msg(DEBUG, "Release connection to remote agent %s:%d for socket %d",
                        conn_apa->endpoint->ip, conn_apa->endpoint->port, conn_apa->fd);
            } else {
                log_msg(ERR, "No connection to remote agent to release for socket %d", fd);
            }
        }
    } else {
//        if (UNLIKELY(errno == EWOULDBLOCK)) {
//            log_msg(WARN, "write_to_consumer would block");
//            return false;
//        }
        if (UNLIKELY(errno == EAGAIN)) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
        }
        log_msg(ERR, "Failed to write to consumer: %s", strerror(errno));
        abort_connection_ca(event_loop, conn_ca);
    }
    return true;
}

void discover_etcd_services() {
    long long modifiedIndex = 0;
    int ret = etcd_get_directory("/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/",
                                 on_etcd_service_endpoint, NULL, &modifiedIndex);
    if (UNLIKELY(ret != 0)) {
        log_msg(ERR, "Failed to do etcd_get_directory: %d", ret);
        exit(-1);
    }
    log_msg(INFO, "Discovered total %d service endpoints", num_endpoints);
}

void on_etcd_service_endpoint(const char *key, const char *value, void *arg) {
    log_msg(INFO, "Got etcd service: %s", key);
    char *ip_and_port = strrchr(key, '/') + 1;
    char *port = strdup(strrchr(key, ':') + 1);
    char *ip = strndup(ip_and_port, strlen(ip_and_port) - strlen(port) - 1);

    endpoints[num_endpoints].ip = ip;
    endpoints[num_endpoints].port = atoi(port);

    // Set up connection pool to this endpoint
    endpoints[num_endpoints].conn_pool = PoolInit(
            NUM_CONN_PER_PROVIDER, NUM_CONN_PER_PROVIDER, sizeof(connection_apa_t),
            NULL, init_connection_apa, &endpoints[num_endpoints], cleanup_connection_apa, NULL
    );
    PoolPrintSaturation(endpoints[num_endpoints].conn_pool);
    num_endpoints++;
}

int init_connection_apa(void *elem, void *data) {
    endpoint_t *endpoint = data;
    connection_apa_t *conn_apa = elem;
    memset(conn_apa, 0, sizeof(connection_apa_t));
    conn_apa->endpoint = endpoint;

    int fd;
    do {
        fd = anetTcpConnect(neterr, endpoint->ip, endpoint->port);
        if (fd < 0) {
            log_msg(WARN, "Failed to connect to remote agent %s:%d - %s, Sleep 1 seconds to retry later",
                    endpoint->ip, endpoint->port, neterr);
            sleep(1);
        } else {
            anetNonBlock(NULL, fd);
            anetEnableTcpNoDelay(NULL, fd);
            conn_apa->fd = fd;
            log_msg(DEBUG, "Build connection to remote agent %s:%d with socket %d", endpoint->ip, endpoint->port, fd);
        }
    } while (fd < 0);

    return 1;
}

void cleanup_connection_apa(void *elem) {
    log_msg(DEBUG, "Cleanup connection to remote agent");
    connection_apa_t *conn_apa = elem;
    if (conn_apa->fd > 0) {
        close(conn_apa->fd);
    }
}

void abort_connection_ca(aeEventLoop *event_loop, connection_ca_t *conn_ca) {
    // Dump data
//    log_msg(WARN, "Conn ca: nread_in - %d, nwrite_in - %d, nread_out - %d, nwrite_out - %d",
//            conn_ca->nread_in, conn_ca->nwrite_in, conn_ca->nread_out, conn_ca->nwrite_out);
//    log_msg(WARN, "Conn ca data: buf_in - %.*s, buf_out - %.*s",
//            conn_ca->nread_in, conn_ca->buf_in, conn_ca->nread_out, conn_ca->buf_out);
//
//    aeDeleteFileEvent(event_loop, conn_ca->fd, AE_WRITABLE | AE_READABLE);
//    close(conn_ca->fd);
//    log_msg(ERR, "Abort connection to consumer with socket: %d", conn_ca->fd);
//
//    connection_apa_t *conn_apa = conn_ca->conn_apa;
//    if (conn_apa != NULL) {
//        aeDeleteFileEvent(event_loop, conn_apa->fd, AE_WRITABLE | AE_READABLE);
//        close(conn_apa->fd);
////    PoolReturn(conn_apa->endpoint->pool, conn_apa);
//        log_msg(ERR, "Abort connection to provider agent with socket: %d", conn_apa->fd);
//    }
//
//    PoolReturn(connection_ca_pool, conn_ca);
//    log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_ca_pool->outstanding);
}
