# WRITE Command - Name Server Proxy Fix

## Root Cause Analysis

The WRITE command requires **bidirectional, interactive communication** between client and storage server, but the name server was treating it like a simple request-response command.

## The Problem Flow (Before Fix)

```
Client                 Name Server              Storage Server
  |                         |                          |
  |--- WRITE file.txt 0 --->|                          |
  |                         |--- WRITE file.txt 0 ---->|
  |                         |                          | (creates lock)
  |                         |<-- "Sentence locked" ----|
  |<-- "Sentence locked" ---|                          |
  |                         |                          |
  |                         | recv() returns 0         |
  |                         | (no more data)           |
  |                         |                          |
  |                         | close(storage_sock) ✗    |
  |                         | close(client_sock) ✗     |
  |                         | exit(0)                  |
  |                         X DEAD                     |
  |                                                     |
  |--- "0 Hello world" ---> X (nowhere to go!)        |
  |                                                     |
  | ❌ Connection closed                               |
```

**Why it broke:**
1. Name server forwards `WRITE file.txt 0` to storage server
2. Storage server sends back `"Sentence 0 locked..."`
3. Name server's simple relay loop:
   ```c
   while ((rcv = recv(storage_sock, relay, ...)) > 0) {
       send(client_sock, relay, ...);
   }
   ```
   This **only reads from storage → client**
4. When storage server waits for client input, it doesn't send anything
5. `recv()` returns 0 (storage not sending)
6. Loop exits → sockets closed → **client can't send words!**

## The Solution

WRITE needs **bidirectional proxying** - data must flow **both ways simultaneously**:

```c
// WRITE command needs bidirectional proxying for interactive session
if (strncmp(buf, "WRITE", 5) == 0) {
    proxy_bidirectional(client_sock, storage_sock);
    close(storage_sock);
    close(client_sock);
    exit(0);
}
```

## The Correct Flow (After Fix)

```
Client                 Name Server              Storage Server
  |                         |                          |
  |--- WRITE file.txt 0 --->|                          |
  |                         |--- WRITE file.txt 0 ---->|
  |                         |                          | (creates lock)
  |                         |<-- "Sentence locked" ----|
  |<-- "Sentence locked" ---|                          |
  |                         |                          |
  |                    [BIDIRECTIONAL PROXY ACTIVE]    |
  |                         |                          |
  |--- "0 Hello world" ---->|--- "0 Hello world" ----->|
  |                         |                          | (processes)
  |                         |<-- "Update applied" -----|
  |<-- "Update applied" ----|                          |
  |                         |                          |
  |--- "1 Nice!" ---------->|--- "1 Nice!" ----------->|
  |                         |                          | (processes)
  |                         |<-- "Update applied" -----|
  |<-- "Update applied" ----|                          |
  |                         |                          |
  |--- "ETIRW" ------------>|--- "ETIRW" ------------->|
  |                         |                          | (saves, removes lock)
  |                         |<-- "Write Successful" ---|
  |<-- "Write Successful" --|                          |
  |                         |                          | (closes connection)
  |                         | (detects close)          |
  |                         | exit(0)                  |
```

## What `proxy_bidirectional()` Does

```c
void proxy_bidirectional(int a_sock, int b_sock) {
    fd_set read_fds;
    // ... setup ...
    
    while (a_open || b_open) {
        FD_ZERO(&read_fds);
        if (a_open) FD_SET(a_sock, &read_fds);
        if (b_open) FD_SET(b_sock, &read_fds);
        
        select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        
        // Read from client → forward to storage
        if (a_open && FD_ISSET(a_sock, &read_fds)) {
            recv(a_sock, buf, ...);
            send(b_sock, buf, ...);
        }
        
        // Read from storage → forward to client
        if (b_open && FD_ISSET(b_sock, &read_fds)) {
            recv(b_sock, buf, ...);
            send(a_sock, buf, ...);
        }
    }
}
```

**Key feature:** Uses `select()` to monitor **both sockets simultaneously**, forwarding data in **both directions** until either side closes.

## Why Other Commands Don't Need This

- **READ**: Client sends command → storage responds with file → done ✓
- **CREATE**: Client sends command → storage responds with status → done ✓
- **DELETE**: Client sends command → storage responds with status → done ✓
- **INFO**: Client sends command → storage responds with info → done ✓
- **VIEW**: Name server aggregates from all servers → done ✓

**WRITE is special:** Client sends command → storage responds → **client sends more data → storage responds → repeat** → client ends session

## Testing

```bash
# Terminal 1
./name_server.out

# Terminal 2
./storage_server.out

# Terminal 3
./client.out
> WRITE test.txt 0
Sentence 0 locked. You may begin writing.
> 0 Hello world          ← Should work now!
Update applied successfully.
> 1 This is a test.
Update applied successfully.
> ETIRW
Write Successful!

> READ test.txt
Hello world This is a test.
```

## Summary of All Fixes

1. **Storage Server** (`server.c`): Removed premature `close()` after `write_to_file()`
2. **Name Server** (`server.c`): Added bidirectional proxy for WRITE command
3. **Client** (`write.c`): Proper buffer handling and null-termination
4. **Storage Write** (`write.c`): Strip newlines before comparing "ETIRW"

All three layers needed fixing for WRITE to work correctly! ✅
