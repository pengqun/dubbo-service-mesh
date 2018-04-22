cmake_minimum_required(VERSION 3.10)
project(mesh-agent-native C)

set(CMAKE_C_STANDARD 11)

include_directories("/usr/local/include")

set(SOURCE_FILES src/main.c src/etcd.c src/log.c)

add_executable(mesh-agent-native ${SOURCE_FILES})

