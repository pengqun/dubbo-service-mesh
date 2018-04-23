#include "main.h"
#include "log.h"
#include "etcd.h"
#include "util.h"
#include "ae.h"
#include "anet.h"

#define AGENT_CONSUMER 1
#define AGENT_PROVIDER 2

#define ETCD_PORT 2379

#define MAX_ACCEPTS_PER_CALL 1
#define NET_IP_STR_LEN 46

static int dubbo_port = 0;
static int agent_type = 0;

static char *endpoints[3];
static int num_endpoints = 0;
static aeEventLoop *event_loop = NULL;
static char neterr[256];

static http_parser_settings parser_settings;

void register_etcd_service(int server_port) {
    char *ip_addr = get_local_ip_addr();
    log_msg(INFO, "Local IP address: %s", ip_addr);

    char etcd_key[128];
    sprintf(etcd_key, "/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/%s:%d",
            ip_addr, server_port);

    int ret = etcd_set(etcd_key, "", 3600, 0);
    if (ret != 0) {
        log_msg(FATAL, "Failed to do etcd_set: %d", ret);
    }
    log_msg(INFO, "Register service at: %s", etcd_key);
}

void key_value_callback(const char *key, const char *value, void *arg) {
    log_msg(INFO, "Got etcd service: %s", key);
    char *endpoint = strrchr(key, '/') + 1;
    endpoints[num_endpoints] = malloc(strlen(endpoint));
    strcpy(endpoints[num_endpoints], endpoint);
    num_endpoints++;
}

void discover_etcd_services() {
    long long modifiedIndex = 0;
    int ret = etcd_get_directory("/dubbomesh/com.alibaba.dubbo.performance.demo.provider.IHelloService/",
                                 key_value_callback, NULL, &modifiedIndex);
    if (ret != 0) {
        log_msg(FATAL, "Failed to do etcd_get_directory: %d", ret);
    }
    log_msg(INFO, "Discovered total %d service endpoints", num_endpoints);
    for (int i = 0; i < num_endpoints; i++) {
        log_msg(INFO, "\tendpoint %d: %s", i, endpoints[i]);
    }
}

#if 1

void writeResponseToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    connection *conn = privdata;

    ssize_t nwritten = write(conn->fd, conn->buf + conn->written, conn->length - conn->written);
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
            return;
        }
        log_msg(WARN, "Failed to write to client: %s", strerror(errno));
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
        close(fd);
    }

    conn->written += nwritten;
    if (conn->written == conn->length) {
        conn->written = 0;
        aeDeleteFileEvent(event_loop, fd, AE_WRITABLE);
    }
}

void call_remote_provider(connection * conn) {
    char *data = "my_data";

    conn->buf[0] = '\0';
    sprintf(conn->buf, "HTTP/1.1 200 OK\r\n"
                       "Content-Length: %ld\r\n"
                       "\r\n"
                       "%s", strlen(data), data);
    conn->length = strlen(conn->buf);
    conn->written = 0;

    if (aeCreateFileEvent(event_loop, conn->fd, AE_WRITABLE, writeResponseToClient, conn) == AE_ERR) {
        log_msg(WARN, "Failed to create writable event for client");
        close(conn->fd);
    }
}

void call_local_provider(connection * conn) {
    char *data = "my_data";

    conn->buf[0] = '\0';
    sprintf(conn->buf, "HTTP/1.1 200 OK\r\n"
                       "Content-Length: %ld\r\n"
                       "\r\n"
                       "%s", strlen(data), data);
    conn->length = strlen(conn->buf);
    conn->written = 0;

    if (aeCreateFileEvent(event_loop, conn->fd, AE_WRITABLE, writeResponseToClient, conn) == AE_ERR) {
        log_msg(WARN, "Failed to create writable event for client");
        close(conn->fd);
    }
}

int http_parser_on_url(http_parser *parser, const char *at, size_t length) {
    log_msg(DEBUG, "On url: %.*s", length, at);
    return 0;
}

int http_parser_on_body(http_parser *parser, const char *at, size_t length) {
    log_msg(DEBUG, "On body: %.*s", length, at);
    return 0;
}

int http_parser_on_complete(http_parser *parser) {
    log_msg(DEBUG, "On complete");
    connection *conn = parser->data;

    if (agent_type == AGENT_CONSUMER) {
        call_remote_provider(conn);
    } else {
        call_local_provider(conn);
    }
    return 0;
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    connection *conn = privdata;
    char buf[1024];

    ssize_t nread = read(fd, buf, sizeof(buf));

    if (nread >= 0) {
        size_t nparsed = http_parser_execute(&conn->parser, &parser_settings, buf, (size_t) nread);
        if (nparsed != nread) {
            log_msg(WARN, "Failed to parse HTTP request");
            goto close_socket;
        }
        if (nread == 0) {
            log_msg(DEBUG, "Client closed connection");
            goto close_socket;
        }
        return;
    }

    if (errno == EAGAIN) {
        log_msg(WARN, "Got EAGAIN on read: %s", strerror(errno));
        return;
    }
    log_msg(WARN, "Failed to read from client: %s", strerror(errno));

close_socket:
    aeDeleteFileEvent(event_loop, fd, AE_WRITABLE | AE_READABLE);
    close(fd);
    free(conn);
    log_msg(DEBUG, "Closed connection to %d", fd);
}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int client_port, client_fd, max = MAX_ACCEPTS_PER_CALL;
    char client_ip[NET_IP_STR_LEN];

    while (max--) {
        client_fd = anetTcpAccept(neterr, fd, client_ip, sizeof(client_ip), &client_port);
        if (client_fd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                log_msg(WARN, "Failed to accept client connection: %s", neterr);
            return;
        }
        log_msg(DEBUG, "Accept client connection: %s:%d - %d", client_ip, client_port, client_fd);

        anetNonBlock(NULL, client_fd);
        anetEnableTcpNoDelay(NULL, client_fd);

        connection *conn = malloc(sizeof(connection));
        conn->fd = client_fd;
        http_parser_init(&conn->parser, HTTP_REQUEST);
        conn->parser.data = conn;

        if (aeCreateFileEvent(event_loop, client_fd, AE_READABLE, readQueryFromClient, conn) == AE_ERR) {
            log_msg(WARN, "Failed to create file event for accepted socket");
            free(conn);
            close(client_fd);
        }
    }
}

void start_http_server(int server_port) {
    parser_settings.on_url = http_parser_on_url;
    parser_settings.on_body = http_parser_on_body;
    parser_settings.on_message_complete = http_parser_on_complete;

    event_loop = aeCreateEventLoop(1024);

    int listen_fd = anetTcpServer(neterr, server_port, NULL, 40000);
    if (listen_fd == ANET_ERR) {
        log_msg(FATAL, "Failed to create listening socket: %s", neterr);
        exit(EXIT_FAILURE);
    }
    anetNonBlock(NULL, listen_fd);

    int ret = aeCreateFileEvent(event_loop, listen_fd, AE_READABLE, acceptTcpHandler, NULL);
    if (ret == ANET_ERR) {
        log_msg(FATAL, "Failed to create file event for listening socket");
        exit(EXIT_FAILURE);
    }

#ifdef PROFILER
    ProfilerStart("iprofile");
    log_msg(INFO, "Start profiler");
#endif
    aeMain(event_loop);

#ifdef PROFILER
    ProfilerStop();
    log_msg(INFO, "Stop profiler");
#endif
}

#endif

#if 0
int access_handler(void *cls, struct MHD_Connection *connection,
                   const char *url, const char *method, const char *version,
                   const char *upload_data, size_t *upload_data_size, void **con_cl) {
    static int dummy;
    const char * page = "MICRO";
    struct MHD_Response * response;
    int ret;
    if (&dummy != *con_cl) {
        *con_cl = &dummy;
        return MHD_YES;
    }
    *con_cl = NULL;
    response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

void start_micro_http_server(int server_port) {
    struct MHD_Daemon *d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
            (uint16_t) server_port, NULL, NULL, &access_handler, NULL, MHD_OPTION_END);
    if (d == NULL) {
        log_msg(FATAL, "Failed to start http server");
        exit(-1);
    }
    (void) getc (stdin);
    MHD_stop_daemon(d);
}

#endif

void signal_handler(int sig) {
    log_msg(INFO, "Got signal %d", sig);
    aeStop(event_loop);
}

int main(int argc, char **argv) {
    int c;
    int server_port = 0;
    char *etcd_host = NULL;
    char *log_dir = NULL;

    while ((c = getopt(argc, argv, "t:e:p:d:l:")) != -1) {
        switch (c) {
            case 't':
                if (strcmp(optarg, "consumer") == 0) {
                    agent_type = AGENT_CONSUMER;
                } else {
                    agent_type = AGENT_PROVIDER;
                }
                break;
            case 'e':
                etcd_host = optarg;
                break;
            case 'p':
                server_port = atoi(optarg);
                break;
            case 'd':
                dubbo_port = atoi(optarg);
                break;
            case 'l':
                log_dir = optarg;
                break;
            default:
                printf("Unknown option '%c'", c);
                exit(EXIT_FAILURE);
        }
    }

    init_log(log_dir);
    log_msg(INFO, "Init log with dir %s", log_dir);

    signal(SIGINT, signal_handler);

    etcd_init(etcd_host, ETCD_PORT, 0);
    log_msg(INFO, "Init etcd to host %s", etcd_host);

    if (agent_type == AGENT_CONSUMER) {
        discover_etcd_services();
    } else {
        register_etcd_service(server_port);
    }

    start_http_server(server_port);

//    start_micro_http_server(server_port);

    log_msg(INFO, "Quit.");

    return 0;
}
