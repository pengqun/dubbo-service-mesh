#include <getopt.h>
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

    log_msg(INFO, "%s %d %d", type, server_port, dubbo_port);

    return 0;
}