#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <microhttpd.h>
#include <memory.h>


void register_etcd_service(int server_port);

void discover_etcd_services();

void start_http_server(int server_port);

int access_handler(void *cls,
                   struct MHD_Connection *connection,
                   const char *url,
                   const char *method,
                   const char *version,
                   const char *upload_data,
                   size_t *upload_data_size,
                   void **con_cl);

#endif
