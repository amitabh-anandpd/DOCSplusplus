#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <pwd.h>
#define NAME_SERVER_PORT 8080
#define STORAGE_SERVER_PORT 8081
#define STORAGE_DIR "./storage"
#define STORAGE_SERVER_IP "127.0.0.1"
#define NAME_SERVER_IP "172.19.82.9"

int get_storage_id(void);

#endif