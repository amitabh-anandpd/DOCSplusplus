CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm
INCLUDE = -Iinclude

CLIENT_SRC = $(wildcard src/client/*.c)
SERVER_SRC = $(wildcard src/storage_server/*.c)
NAME_SRC = $(wildcard src/name_server/*.c)

CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
NAME_OBJ = $(NAME_SRC:.c=.o)

all: clean client.out storage_server.out name_server.out

client.out: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJ)

storage_server.out: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJ)

name_server.out: $(NAME_OBJ)
	$(CC) $(CFLAGS) -o $@ $(NAME_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f src/client/*.o src/storage_server/*.o src/name_server/*.o client.out server.out name_server.out
