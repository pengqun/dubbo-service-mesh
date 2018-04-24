cmake_minimum_required(VERSION 3.10)
project(mesh-agent-native C)

set(CMAKE_C_STANDARD 11)

include_directories("/usr/local/include")

set(SOURCE_FILES src/main.c src/etcd.c src/log.c src/util.c src/http_parser.c src/pool.c
        src/ae.c src/opt/ae_epoll.c src/opt/ae_kqueue.c src/opt/ae_select.c src/zmalloc.c src/anet.c)

add_executable(mesh-agent-native ${SOURCE_FILES})

