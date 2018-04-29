cmake_minimum_required(VERSION 3.10)
project(mesh-agent-native C)

set(CMAKE_C_STANDARD 11)

include_directories("/usr/local/include")

set(SOURCE_FILES src/main.c src/etcd.c src/log.c src/util.c src/http_parser.c src/pool.c src/common.h
        src/ae.c src/opt/ae_epoll.c src/opt/ae_kqueue.c src/opt/ae_select.c src/zmalloc.c src/anet.c src/consumer.h src/consumer.c src/provider.c src/provider.h)

add_executable(mesh-agent-native ${SOURCE_FILES})

