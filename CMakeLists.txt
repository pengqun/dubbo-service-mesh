cmake_minimum_required(VERSION 3.10)
project(mesh-agent-native C)

set(CMAKE_C_STANDARD 11)

include_directories("/usr/local/include")

add_executable(main.c etcd.c)
