
#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <time.h>

#define MAX_CHECKPOINT_TAG 128

// Function prototypes for checkpoint operations
int checkpoint_create(int client_sock, const char *filename, const char *tag, 
                      const char *username, int storage_id);
int checkpoint_view(int client_sock, const char *filename, const char *tag, 
                   const char *username, int storage_id);
int checkpoint_revert(int client_sock, const char *filename, const char *tag, 
                     const char *username, int storage_id);
int checkpoint_list(int client_sock, const char *filename, 
                   const char *username, int storage_id);

#endif // CHECKPOINT_H