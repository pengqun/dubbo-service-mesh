#include <stdio.h>
#include <stdlib.h>
#include "etcd.h"

int main() {
    printf("Hello, World!\n");

    etcd_init("127.0.0.1", 2379, 0);

    etcd_set("my_key", "my_value", 3600, 0);

    char *value = malloc(1000);

    etcd_get("my_key", &value, NULL);

    printf("value: %s", value);

    return 0;
}