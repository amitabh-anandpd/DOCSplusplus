#ifndef VIEW_H
#define VIEW_H

// In include/view.h
void list_files(int client_sock, int show_all, int show_long, const char* username);
void count_file(const char* path, int* words, int* chars);

#endif