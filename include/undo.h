#ifndef UNDO_H
#define UNDO_H

void undo_last_change(int client_sock, const char* filename, const char* username);

#endif