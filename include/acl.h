#ifndef ACL_H
#define ACL_H

#include <time.h>

// Metadata structure for files
typedef struct {
    char owner[64];
    time_t created_time;
    time_t last_accessed;
    char read_users[512];   // comma-separated list of users with read access
    char write_users[512];  // comma-separated list of users with write access
} FileMetadata;

// Function prototypes
int create_metadata_file(const char *filename, const char *owner);
int read_metadata_file(const char *filename, FileMetadata *meta);
int update_metadata_file(const char *filename, FileMetadata *meta);
int check_read_access(const char *filename, const char *username);
int check_write_access(const char *filename, const char *username);
int add_read_access(const char *filename, const char *username);
int add_write_access(const char *filename, const char *username);
int remove_all_access(const char *filename, const char *username);

#endif // ACL_H
