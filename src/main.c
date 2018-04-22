#include "main.h"
#include "log.h"
#include "etcd.h"

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

    etcd_set("my_key", "my_value", 3600, 0);
    char *value = malloc(1000);
    etcd_get("my_key", &value, NULL);
    log_msg(INFO, "Got etcd value: %s", value);

    start_http_server(server_port);

//    if (strcmp(type, "consumer") == 0) {
//    }

    log_msg(INFO, "%s %d %d", type, server_port, dubbo_port);

    return 0;
}

void start_http_server(int port) {
    struct MHD_Daemon *d = MHD_start_daemon(
            MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
            (uint16_t) port, NULL, NULL, &access_handler, NULL, MHD_OPTION_END);
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