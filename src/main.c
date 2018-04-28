#include "main.h"
#include "consumer.h"
#include "provider.h"

#define AGENT_CONSUMER 1
#define AGENT_PROVIDER 2

#define ETCD_PORT 2379

#define MAX_ACCEPTS_PER_CALL 1
#define NET_IP_STR_LEN 46


static int dubbo_port = 0;
static int agent_type = 0;

static aeEventLoop *the_event_loop = NULL;
static char neterr[256];


void signal_handler(int sig) ;
void start_http_server(int server_port) ;
void accept_tcp_handler(aeEventLoop *el, int fd, void *privdata, int mask) ;

#ifdef MICRO_HTTP
void start_micro_http_server(int server_port) ;
#endif

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
        consumer_init();
    } else {
        provider_init(server_port, dubbo_port);
    }

#ifdef MICRO_HTTP
    start_micro_http_server(server_port);
#else
    start_http_server(server_port);
#endif

    if (agent_type == AGENT_CONSUMER) {
        consumer_cleanup();
    } else {
        provider_cleanup();
    }
    log_msg(INFO, "Quit.");

    return 0;
}

void signal_handler(int sig) {
    log_msg(INFO, "Got signal %d", sig);
    aeStop(the_event_loop);
}

void start_http_server(int server_port) {
    the_event_loop = aeCreateEventLoop(2048);

    int listen_fd = anetTcpServer(neterr, server_port, NULL, 40000);
    if (listen_fd == ANET_ERR) {
        log_msg(FATAL, "Failed to create listening socket: %s", neterr);
        exit(EXIT_FAILURE);
    }
    anetNonBlock(NULL, listen_fd);

    int ret = aeCreateFileEvent(the_event_loop, listen_fd, AE_READABLE, accept_tcp_handler, NULL);
    if (ret == ANET_ERR) {
        log_msg(FATAL, "Failed to create file event for accept_tcp_handler");
        exit(EXIT_FAILURE);
    }

#ifdef PROFILER
    ProfilerStart("iprofile");
    log_msg(INFO, "Start profiler");
#endif
    aeMain(the_event_loop);

#ifdef PROFILER
    ProfilerStop();
    log_msg(INFO, "Stop profiler");
#endif
}

void accept_tcp_handler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int client_port, client_fd, max = MAX_ACCEPTS_PER_CALL;
    char client_ip[NET_IP_STR_LEN];

    while (max--) {
        client_fd = anetTcpAccept(neterr, fd, client_ip, sizeof(client_ip), &client_port);
        if (client_fd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                log_msg(WARN, "Failed to accept client connection: %s", neterr);
            return;
        }
        log_msg(DEBUG, "Accept client connection: %s:%d with socket %d", client_ip, client_port, client_fd);

        anetNonBlock(NULL, client_fd);
        anetEnableTcpNoDelay(NULL, client_fd);

        if (agent_type == AGENT_CONSUMER) {
            consumer_http_handler(el, client_fd);
        } else {
            provider_http_handler(el, client_fd);
        }
    }
}

#ifdef MICRO_HTTP
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