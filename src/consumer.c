#include "consumer.h"
#include "util.h"

// Adjustable params
//#define NUM_CONN_FOR_CONSUMER 2048
//#define NUM_CONN_PER_PROVIDER 1024
#define NUM_CONN_FOR_CONSUMER 1024
#define NUM_CONN_PER_PROVIDER 512
//#define NUM_CONN_FOR_CONSUMER 512
//#define NUM_CONN_PER_PROVIDER 256

#ifdef LATENCY_AWARE
#define LOAD_BALANCE_THRESHOLD 10000
#define LOAD_PROTECT_THRESHOLD 100
#endif

static char neterr[256];

static endpoint_t endpoints[3];
static int num_endpoints = 0;
//static int round_robin_id = 0;

#ifdef LATENCY_AWARE
static int total_score = 0;
static int request_counter = 0;
#endif

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

endpoint_t *get_endpoint_least_loaded() ;

endpoint_t *get_endpoint_min_latency() ;

endpoint_t *get_endpoint_min_latency_prob() ;

void consumer_init() {
    log_msg(INFO, "Consumer init begin");
    discover_etcd_services();

    log_msg(INFO, "Init connection pool for consumer");
    connection_ca_pool = PoolInit(NUM_CONN_FOR_CONSUMER, NUM_CONN_FOR_CONSUMER,
                                  sizeof(connection_ca_t), NULL, NULL, NULL, NULL, NULL);
    PoolPrintSaturation(connection_ca_pool);

    srand((unsigned int) time(NULL));

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
    if (UNLIKELY(conn_ca->fd < 0)) {
        log_msg(WARN, "Connection closed for socket %d, ignore read_from_consumer", fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        return;
    }

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
//            endpoint_t *endpoint = get_endpoint_least_loaded();
            endpoint_t *endpoint = get_endpoint_min_latency();
//            endpoint_t *endpoint = get_endpoint_min_latency_prob();
            conn_apa = PoolGet(endpoint->conn_pool);
            if (UNLIKELY(conn_apa == NULL)) {
                log_msg(ERR, "No connection to remote agent available for socket %d", fd);
                abort_connection_ca(event_loop, conn_ca);
                return;
            }
            conn_ca->conn_apa = conn_apa;
            log_msg(DEBUG, "Pick up connection to %s:%d with socket %d",
                    conn_apa->endpoint->ip, conn_apa->endpoint->port, conn_apa->fd);
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
//        log_msg(INFO, "Consumer closed connection for socket %d", fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        PoolReturn(connection_ca_pool, conn_ca);
        conn_ca->fd = -1;
        close(fd);
        log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_ca_pool->outstanding);
    }
}

endpoint_t *get_endpoint_least_loaded() {
    endpoint_t *endpoint = &endpoints[0];
    int min_outstanding = endpoints[0].conn_pool->outstanding;
    for (int i = 1; i < num_endpoints; i++) {
        if (min_outstanding > endpoints[i].conn_pool->outstanding) {
            min_outstanding = endpoints[i].conn_pool->outstanding;
            endpoint = &endpoints[i];
        }
    }
//    for (int i = 0; i < num_endpoints; i++) {
//        log_msg(INFO, "Endpoint %d: %s:%d - outstanding %d",
//                i, endpoints[i].ip, endpoints[i].port, endpoints[i].conn_pool->outstanding);
//    }
    log_msg(DEBUG, "Load balance to endpoint %s:%d with outstanding %d",
            endpoint->ip, endpoint->port, min_outstanding);
    return endpoint;
}

#ifdef LATENCY_AWARE
endpoint_t *get_endpoint_min_latency() {
    endpoint_t *endpoint = &endpoints[0];
    if (UNLIKELY(endpoint->num_reqs < LOAD_BALANCE_THRESHOLD)) {
        return get_endpoint_least_loaded();
    }
    // Load balance: min-latency
    long min_latency = endpoints[0].total_ms / endpoints[0].num_reqs;
    for (int i = 1; i < num_endpoints; i++) {
        long latency = endpoints[i].total_ms / endpoints[i].num_reqs;
        if (min_latency > latency) {
            min_latency = latency;
            endpoint = &endpoints[i];
        }
    }
    for (int i = 0; i < num_endpoints; i++) {
        log_msg(DEBUG, "Endpoint %d: %ld ms = %ld / %d",
                i, endpoints[i].total_ms / endpoints[i].num_reqs,
                endpoints[i].total_ms, endpoints[i].num_reqs);
    }
    log_msg(DEBUG, "Load balance to endpoint %s:%d with latency %ld ms (%ld / %d)",
            endpoint->ip, endpoint->port, min_latency, endpoint->total_ms, endpoint->num_reqs);

    if (endpoint->conn_pool->outstanding >= LOAD_PROTECT_THRESHOLD) {
        log_msg(DEBUG, "Endpoint %s:%d: outstanding - %d, latency - %ld ms (%ld / %d)",
                endpoint->ip, endpoint->port, endpoint->conn_pool->outstanding,
                endpoint->total_ms / endpoint->num_reqs, endpoint->total_ms, endpoint->num_reqs);
        return get_endpoint_least_loaded();
    }
    return endpoint;
}

endpoint_t *get_endpoint_min_latency_prob() {
    endpoint_t *endpoint = &endpoints[0];
    if (request_counter++ % 100 == 0) {
        total_score = 0;
        for (int i = 0; i < num_endpoints; i++) {
            endpoint[i].score = (int) (1000000 / (endpoints[i].total_ms / endpoints[i].num_reqs));
            total_score += endpoint[i].score;
            log_msg(DEBUG, "Endpoint %d: latency - %ld ms (%ld / %d), score - %d", i,
                    endpoints[i].total_ms / endpoints[i].num_reqs,
                    endpoints[i].total_ms, endpoints[i].num_reqs, endpoint[i].score);
        }
    }

    int pointer = rand() % total_score;
    log_msg(DEBUG, "Generate random pointer %d with total score %d", pointer, total_score);

    for (int i = 0; i < num_endpoints; i++) {
        if (pointer < endpoint[i].score) {
            endpoint = &endpoints[i];
            break;
        }
        pointer -= endpoint[i].score;
    }
    log_msg(DEBUG, "Load balance to endpoint %s:%d: latency - %ld ms (%ld / %d)",
            endpoint->ip, endpoint->port, endpoint->total_ms / endpoint->num_reqs,
            endpoint->total_ms, endpoint->num_reqs);

    if (endpoint->conn_pool->outstanding >= LOAD_PROTECT_THRESHOLD) {
        log_msg(DEBUG, "Endpoint %s:%d: outstanding - %d, latency - %ld ms (%ld / %d)",
                endpoint->ip, endpoint->port, endpoint->conn_pool->outstanding,
                endpoint->total_ms / endpoint->num_reqs, endpoint->total_ms, endpoint->num_reqs);
        return get_endpoint_least_loaded();
    }
    return endpoint;
}
#endif

void write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    _write_to_remote_agent(event_loop, fd, privdata);
}

bool _write_to_remote_agent(aeEventLoop *event_loop, int fd, void *privdata) {
    connection_ca_t *conn_ca = privdata;
    if (UNLIKELY(conn_ca->fd < 0)) {
        log_msg(WARN, "Connection closed for socket %d, ignore write_to_remote_agent", fd);
//        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        return true;
    }

    ssize_t nwrite = write(fd, conn_ca->buf_in + conn_ca->nwrite_in,
                           (size_t) (conn_ca->nread_in - conn_ca->nwrite_in));

    if (LIKELY(nwrite >= 0)) {
        log_msg(DEBUG, "Write %d bytes to remote agent for socket %d", nwrite, fd);
        conn_ca->nwrite_in += nwrite;
        if (LIKELY(conn_ca->nwrite_in == conn_ca->nread_in)) {
            // Done writing
            aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);

#ifdef LATENCY_AWARE
            // Record request start
            connection_apa_t *conn_apa = conn_ca->conn_apa;
            conn_apa->req_start = get_current_time_ms();
#endif
        } else {
            log_msg(WARN, "Partial write for socket %d", fd);
        }
    } else {
//        if (UNLIKELY(errno == EWOULDBLOCK)) {
//            log_msg(WARN, "write_to_remote_agent would block");
//            return false;
//        }
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on write_to_remote_agent: %s", strerror(errno));
            return true;
        }
        log_msg(ERR, "Failed to write to remote agent: %s", strerror(errno));
        abort_connection_ca(event_loop, conn_ca);
    }
    return true;
}

void read_from_remote_agent(aeEventLoop *event_loop, int fd, void *privdata, int mask) {
    connection_ca_t *conn_ca = privdata;
    if (UNLIKELY(conn_ca->fd < 0)) {
        log_msg(WARN, "Connection closed for socket %d, ignore read_from_remote_agent", fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        return;
    }

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

#ifdef LATENCY_AWARE
        connection_apa_t *conn_apa = conn_ca->conn_apa;
        conn_apa->endpoint->num_reqs++;
//        conn_apa->endpoint->total_ms += (get_current_time_ms() - conn_apa->req_start) - 10;
        conn_apa->endpoint->total_ms += (get_current_time_ms() - conn_apa->req_start);
#endif

    } else if (UNLIKELY(nread < 0)) {
        if (errno == EAGAIN) {
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
    if (UNLIKELY(conn_ca->fd < 0)) {
        log_msg(WARN, "Connection closed for socket %d, ignore write_to_consumer", fd);
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        return true;
    }

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
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on write_to_consumer: %s", strerror(errno));
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

#ifdef LATENCY_AWARE
    // Set up initial latency stats to avoid zero case
    endpoints[num_endpoints].total_ms = 1;
    endpoints[num_endpoints].num_reqs = 1;
    endpoints[num_endpoints].score = 0;
#endif

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
    log_msg(INFO, "Cleanup connection to remote agent");
    connection_apa_t *conn_apa = elem;
    if (conn_apa->fd > 0) {
        log_msg(WARN, "Abort connection to provider agent with socket: %d", conn_apa->fd);
        close(conn_apa->fd);
        conn_apa->fd = -1;
    }
}

void abort_connection_ca(aeEventLoop *event_loop, connection_ca_t *conn_ca) {
    // Dump data
    log_msg(WARN, "Conn ca: nread_in - %d, nwrite_in - %d, nread_out - %d, nwrite_out - %d",
            conn_ca->nread_in, conn_ca->nwrite_in, conn_ca->nread_out, conn_ca->nwrite_out);
//    log_msg(WARN, "Conn ca data: buf_in - %.*s, buf_out - %.*s",
//            conn_ca->nread_in, conn_ca->buf_in, conn_ca->nread_out, conn_ca->buf_out);

    log_msg(ERR, "Abort connection to consumer with socket: %d", conn_ca->fd);
    aeDeleteFileEvent(event_loop, conn_ca->fd, AE_WRITABLE | AE_READABLE);
    close(conn_ca->fd);
    conn_ca->fd = -1;

    connection_apa_t *conn_apa = conn_ca->conn_apa;
    if (conn_apa != NULL) {
        log_msg(ERR, "Abort connection to provider agent with socket: %d", conn_apa->fd);
        aeDeleteFileEvent(event_loop, conn_apa->fd, AE_WRITABLE | AE_READABLE);
        close(conn_apa->fd);
        conn_apa->fd = -1;
//    PoolReturn(conn_apa->endpoint->pool, conn_apa);
    }

    PoolReturn(connection_ca_pool, conn_ca);
    log_msg(DEBUG, "Returned connection object to pool, active: %d", connection_ca_pool->outstanding);
}
