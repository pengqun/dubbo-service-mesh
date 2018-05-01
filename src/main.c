#include "main.h"
#include "consumer.h"
#include "provider.h"
#include "debug.h"

#define AGENT_CONSUMER 1
#define AGENT_PROVIDER 2

#define ETCD_PORT 2379
#define NET_IP_STR_LEN 46

// Adjustable params
//#define EV_MAX_SET_SIZE 4096
#define EV_MAX_SET_SIZE 2048
#define TCP_LISTEN_BACKLOG 40000
#define MAX_ACCEPTS_PER_CALL 1


static int dubbo_port = 0;
static int agent_type = 0;

static aeEventLoop *the_event_loop = NULL;
static char neterr[256];


void init_signals() ;
int do_listen(int server_port) ;
void accept_tcp_handler(aeEventLoop *el, int fd, void *privdata, int mask) ;

//void set_cpu_affinity();
void do_fork() ;
void monitor_accepts(int listen_fd) ;

void signal_handler(int sig) ;

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
                    prctl(PR_SET_NAME, "agent-consumer", 0, 0, 0);
                } else if (strcmp(optarg, "provider-small") == 0) {
                    agent_type = AGENT_PROVIDER;
                    prctl(PR_SET_NAME, "agent-small", 0, 0, 0);
                } else if (strcmp(optarg, "provider-medium") == 0) {
                    agent_type = AGENT_PROVIDER;
                    prctl(PR_SET_NAME, "agent-medium", 0, 0, 0);
                } else {
                    agent_type = AGENT_PROVIDER;
                    prctl(PR_SET_NAME, "agent-large", 0, 0, 0);
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

    init_signals();
//    init_debug();

    setpriority(PRIO_PROCESS, 0, -20);
    
//    set_cpu_affinity();

    etcd_init(etcd_host, ETCD_PORT, 0);
    log_msg(INFO, "Init etcd to host %s", etcd_host);

    if (agent_type == AGENT_CONSUMER) {
//        do_fork();
        int listen_fd = do_listen(server_port);
        monitor_accepts(listen_fd);
        consumer_init();
    } else {
//        do_fork();
        int listen_fd = do_listen(server_port);
        monitor_accepts(listen_fd);
        provider_init(server_port, dubbo_port);
    }

#ifdef PROFILER
    ProfilerStart("/root/logs/iprofile");
    log_msg(INFO, "Start profiler");
#endif
    aeMain(the_event_loop);

#ifdef PROFILER
    ProfilerStop();
    log_msg(INFO, "Stop profiler..");
    sleep(60);
#endif

    if (agent_type == AGENT_CONSUMER) {
        consumer_cleanup();
    } else {
        provider_cleanup();
    }
    log_msg(INFO, "Quit.");

    return 0;
}

void init_signals() {
    struct sigaction sigact;
    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGHUP, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
    sigaction(SIGPIPE, &sigact, NULL);
    sigaction(SIGCHLD, &sigact, NULL);
    sigaction(SIGURG, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);
}

void signal_handler(int sig) {
    switch (sig) {
        case SIGQUIT:
            log_msg_r(INFO, "QUIT signal ended program.");
            break;
        case SIGTERM:
            log_msg_r(INFO, "TERM signal ended program.");
            break;
        case SIGINT:
            log_msg_r(INFO, "INT signal ended program.");
            break;
        case SIGHUP:
            /* Ignore SIGHUP signal, just print a warning. */
            log_msg_r(WARN, "Program hanged up.");
            break;
        case SIGSEGV:
            log_msg_r(ERR, "Segmentation fault!");
            dump_stack();
            /* To generate the core file. */
            abort();
        case SIGPIPE:
            log_msg_r(INFO, "Receive SIGPIPE.");
            break;
        case SIGCHLD:
            //log_msg_r(INFO, "Receive SIGCHLD.");
            break;
        case SIGUSR1:
            log_msg_r(INFO, "Receive SIGUSR1.");
            break;
        case SIGURG:
            log_msg_r(INFO, "Receive SIGURG.");
            break;
        default:
            log_msg_r(FATAL, "Unknown signal(%d) ended program!", sig);
    }

    aeStop(the_event_loop);
}

int do_listen(int server_port) {
    int listen_fd = anetTcpServer(neterr, server_port, NULL, TCP_LISTEN_BACKLOG);
    if (listen_fd == ANET_ERR) {
        log_msg(ERR, "Failed to create listening socket: %s", neterr);
        exit(EXIT_FAILURE);
    }
    anetNonBlock(NULL, listen_fd);
    return listen_fd;
}

void monitor_accepts(int listen_fd) {
    the_event_loop = aeCreateEventLoop(EV_MAX_SET_SIZE);

    int ret = aeCreateFileEvent(the_event_loop, listen_fd, AE_READABLE, accept_tcp_handler, NULL);
    if (ret == ANET_ERR) {
        log_msg(ERR, "Failed to create file event for accept_tcp_handler: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void accept_tcp_handler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int client_port, client_fd, max = MAX_ACCEPTS_PER_CALL;
    char client_ip[NET_IP_STR_LEN];

    while (max--) {
        client_fd = anetTcpAccept(neterr, fd, client_ip, sizeof(client_ip), &client_port);
        if (UNLIKELY(client_fd == ANET_ERR)) {
            if (errno != EWOULDBLOCK) {
                log_msg(ERR, "Failed to accept client connection: %s", neterr);
            }
            return;
        }
//        log_msg(INFO, "Accept client connection: %s:%d with socket %d", client_ip, client_port, client_fd);

        anetNonBlock(NULL, client_fd);
        anetEnableTcpNoDelay(NULL, client_fd);

        if (agent_type == AGENT_CONSUMER) {
            consumer_http_handler(el, client_fd);
        } else {
            provider_http_handler(el, client_fd);
        }
    }
}

void do_fork() {
    pid_t pid = fork();
    if (pid == -1) {
        log_msg(ERR, "Fork error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid == 0) {
        log_msg(INFO, "In child process");
    } else {
        log_msg(INFO, "In parent process, forked child with PID %d", pid);
    }
}

#if 0
void set_cpu_affinity() {
    int num_cpu = (int) sysconf(_SC_NPROCESSORS_CONF);
    log_msg(INFO, "Number of CPUs: ", num_cpu);

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(num_cpu - 1, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        log_msg(WARN, "Set CPU affinity failed: %s\n", strerror(errno));
    }
}
#endif
