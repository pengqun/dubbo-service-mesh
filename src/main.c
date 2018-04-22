#include "main.h"
#include "log.h"
#include "etcd.h"
#include "util.h"

static char *endpoints[3];
static int num_endpoints = 0;

int main(int argc, char **argv) {
    int c;
    int server_port = 0;
    int dubbo_port = 0;
    char *type = NULL;
    char *etcd_host = NULL;
    char *log_dir = NULL;

    while ((c = getopt(argc, argv, "t:e:p:d:l:")) != -1) {
        switch (c) {
            case 't':
                type = optarg;
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
                exit(-1);
        }
    }

    init_log(log_dir);
    log_msg(INFO, "Init log with dir %s", log_dir);

    etcd_init(etcd_host, 2379, 0);
    log_msg(INFO, "Init etcd to host %s", etcd_host);

//    char *value = malloc(1000);
//    etcd_get("my_key", &value, NULL);
//    log_msg(INFO, "Got etcd value: %s", value);


    if (strcmp(type, "consumer") == 0) {
        discover_etcd_services();
    } else {
        register_etcd_service(server_port);
    }

    start_http_server(server_port);

    log_msg(INFO, "%s %d %d", type, server_port, dubbo_port);

    return 0;
}

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

static void key_value_callback(const char *key, const char *value, void *arg) {
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

void start_http_server(int server_port) {
    struct MHD_Daemon *d = MHD_start_daemon(
            MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
            (uint16_t) server_port, NULL, NULL, &access_handler, NULL, MHD_OPTION_END);
    if (d == NULL) {
        log_msg(FATAL, "Failed to start http server");
        exit(-1);
    }
    (void) getc (stdin);
    MHD_stop_daemon(d);
}

int access_handler(void *cls,
                   struct MHD_Connection *connection,
                   const char *url,
                   const char *method,
                   const char *version,
                   const char *upload_data,
                   size_t *upload_data_size,
                   void **con_cl) {
    static int dummy;
    const char * page = "MICRO!";
    struct MHD_Response * response;
    int ret;

    if (&dummy != *con_cl)
    {
        /* The first time only the headers are valid,
           do not respond in the first round... */
        *con_cl = &dummy;
        return MHD_YES;
    }
    *con_cl = NULL; /* clear context pointer */

    response = MHD_create_response_from_buffer (strlen(page),
                                                (void*) page,
                                                MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection,
                             MHD_HTTP_OK,
                             response);
    MHD_destroy_response(response);
    return ret;
}