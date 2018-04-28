#include "consumer.h"
#include "etcd.h"
#include "http_parser.h"
#include "anet.h"

#define NUM_CONN_FOR_CONSUMER 512
#define NUM_CONN_PER_PROVIDER 256

static char neterr[256];

static endpoint_t endpoints[3];
static int num_endpoints = 0;
static int round_robin_id = 0;

static Pool *connection_ca_pool = NULL;


void discover_etcd_services() ;
void on_etcd_service_endpoint(const char *key, const char *value, void *arg);

int init_connection_apa(void *elem, void *data) ;
void cleanup_connection_apa(void *elem) ;

void read_from_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void read_from_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;
void write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) ;

void consumer_init() {
    log_msg(INFO, "Consumer init begin");
    discover_etcd_services();

    log_msg(DEBUG, "Init connection pool for consumer");
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
    if (conn_ca == NULL) {
        log_msg(FATAL, "No connection object available, abort");
        exit(-1);
    }
    log_msg(DEBUG, "Fetched connection object from pool, active: %d", connection_ca_pool->outstanding);
    memset(conn_ca, 0, sizeof(connection_ca_t));
    conn_ca->fd = fd;

    // Read from consumer
    if (aeCreateFileEvent(event_loop, fd, AE_READABLE, read_from_consumer, conn_ca) == AE_ERR) {
        log_msg(FATAL, "Failed to create readable event for read_from_consumer");
        exit(-1);
    }
}

void read_from_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nread = read(fd, conn_ca->buf_in + conn_ca->nread_in, sizeof(conn_ca->buf_in) - conn_ca->nread_in);

    if (nread > 0) {
        log_msg(DEBUG, "Read %d bytes from consumer for socket %d", nread, fd);
        conn_ca->nread_in += nread;

        // Reset buf_out pointers
        conn_ca->nread_out = 0;
        conn_ca->nwrite_out = 0;

        connection_apa_t *conn_apa = conn_ca->conn_apa;

        if (conn_apa == NULL) {
            // Load balance: pick up a remote endpoint
            endpoint_t *endpoint = &endpoints[round_robin_id++ % num_endpoints];
            conn_apa = PoolGet(endpoint->conn_pool);
            if (conn_apa == NULL) {
                log_msg(ERR, "No connection to remote agent available, abort");
//                aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
//                PoolReturn(connection_ca_pool, conn_ca);
//                close(fd);
//                return;
                exit(-1);
            }
            conn_ca->conn_apa = conn_apa;
            log_msg(DEBUG, "Pick up connection to %s:%d with socket %d",
                    conn_apa->endpoint->ip, conn_apa->endpoint->port, conn_apa->fd);
        }

        // Write to remote agent
        if (aeCreateFileEvent(event_loop, conn_apa->fd, AE_WRITABLE, write_to_remote_agent, conn_ca) == AE_ERR) {
            log_msg(ERR, "Failed to create writable event for write_to_remote_agent");
            exit(-1);
        }
        // Read from remote agent
        if (aeCreateFileEvent(event_loop, conn_apa->fd, AE_READABLE, read_from_remote_agent, conn_ca) == AE_ERR) {
            log_msg(ERR, "Failed to create readable event for read_from_remote_agent");
            exit(-1);
        }
        return;
    }

    if (nread < 0) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read_from_consumer: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from consumer: %s", strerror(errno));
        exit(-1);
    }
    if (nread == 0) {
        log_msg(INFO, "Consumer closed connection");
    }
    aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
    PoolReturn(connection_ca_pool, conn_ca);
    log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_ca_pool->outstanding);
    close(fd);
}

void write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nwrite = write(fd, conn_ca->buf_in + conn_ca->nwrite_in,
                           (size_t) (conn_ca->nread_in - conn_ca->nwrite_in));

    if (nwrite >= 0) {
        log_msg(DEBUG, "Write %d bytes to remote agent for socket %d", nwrite, fd);
        conn_ca->nwrite_in += nwrite;
        if (conn_ca->nwrite_in == conn_ca->nread_in) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
        }
    }

    if (nwrite == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to write to remote agent: %s", strerror(errno));
        exit(-1);
    }
}

void read_from_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nread = read(fd, conn_ca->buf_out + conn_ca->nread_out, sizeof(conn_ca->buf_out) - conn_ca->nread_out);

    if (nread > 0) {
        log_msg(DEBUG, "Read %d bytes from remote agent for socket %d", nread, fd);
        conn_ca->nread_out += nread;

        // Reset buf_in pointers
        conn_ca->nread_in = 0;
        conn_ca->nwrite_in = 0;

        // Write back to consumer
        if (aeCreateFileEvent(event_loop, conn_ca->fd, AE_WRITABLE, write_to_consumer, conn_ca) == AE_ERR) {
            log_msg(ERR, "Failed to create writable event for write_to_consumer");
            exit(-1);
        }
        return;
    }

    if (nread < 0) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read_from_remote_agent: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to read from remote agent: %s", strerror(errno));
        exit(-1);
    }
    if (nread == 0) {
        log_msg(ERR, "Remote agent closed connection");
        exit(-1);
    }
    aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
    close(fd);
    aeDeleteFileEvent(event_loop, conn_ca->fd, AE_WRITABLE | AE_READABLE);
    close(conn_ca->fd);
}

void write_to_consumer(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;

    ssize_t nwrite = write(fd, conn_ca->buf_out + conn_ca->nwrite_out,
                           (size_t) (conn_ca->nread_out - conn_ca->nwrite_out));

    if (nwrite >= 0) {
        log_msg(DEBUG, "Write %d bytes to consumer for socket %d", nwrite, fd);
        conn_ca->nwrite_out += nwrite;
        if (conn_ca->nwrite_out == conn_ca->nread_out) {
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);

            // Release current connection to remote agent
            connection_apa_t *conn_apa = conn_ca->conn_apa;
            if (conn_apa != NULL) {
                PoolReturn(conn_apa->endpoint->conn_pool, conn_apa);
                conn_ca->conn_apa = NULL;
                log_msg(DEBUG, "Release connection to %s:%d for socket %d",
                        conn_apa->endpoint->ip, conn_apa->endpoint->port, conn_apa->fd);
            }
        }
    }

    if (nwrite == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on write: %s", strerror(errno));
            return;
        }
        log_msg(ERR, "Failed to write to consumer: %s", strerror(errno));
        exit(-1);
    }
}

void discover_etcd_services() {
    long long modifiedIndex = 0;
    int ret = etcd_get_directory("/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/",
                                 on_etcd_service_endpoint, NULL, &modifiedIndex);
    if (ret != 0) {
        log_msg(FATAL, "Failed to do etcd_get_directory: %d", ret);
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
//    endpoints[num_endpoints].pending = 0;

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

    int fd = anetTcpNonBlockConnect(neterr, endpoint->ip, endpoint->port);
    if (errno != EINPROGRESS) {
        log_msg(ERR, "Failed to connect to remote agent %s:%d", endpoint->ip, endpoint->port);
        close(fd);
        conn_apa->fd = -1;
    } else {
        anetEnableTcpNoDelay(NULL, fd);
        conn_apa->fd = fd;
        log_msg(DEBUG, "Build connection to remote agent %s:%d", endpoint->ip, endpoint->port);
    }
    return 1;
}

void cleanup_connection_apa(void *elem) {
    log_msg(DEBUG, "Cleanup connection to remote agent");
    connection_apa_t *conn_apa = elem;
    if (conn_apa->fd > 0) {
        close(conn_apa->fd);
    }
}
