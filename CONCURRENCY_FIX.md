# Concurrency Fix - Storage Server

## Problem Identified

Your original storage server was **single-threaded** and could only handle **one client at a time**:

```
Client 1 connects → Server handles Client 1
Client 2 tries to connect → BLOCKED (waiting)
Client 3 tries to connect → BLOCKED (waiting)
...
Client 1 finishes → Server now accepts Client 2
```

### Why This Was Critical

1. **WRITE operations block everyone**: When a client uses WRITE (interactive multi-round-trip), the server is stuck in the `while(1)` loop. All other clients wait.

2. **VIEW aggregation fails**: The name server tries to connect to ALL storage servers simultaneously to aggregate results. If any storage server is busy, VIEW fails or times out.

3. **Poor scalability**: No concurrent access means terrible performance.

## Solution Applied

Changed to **process-per-client** model (matching your name server design):

```
                    ┌─────────────┐
                    │   Parent    │
                    │   Process   │
                    │  (accept)   │
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
    ┌───▼───┐         ┌───▼───┐         ┌───▼───┐
    │Child 1│         │Child 2│         │Child 3│
    │handle │         │handle │         │handle │
    │Client1│         │Client2│         │Client3│
    └───────┘         └───────┘         └───────┘
```

### Changes Made

1. **Added fork() after accept()**:
   - Parent process continues accepting new connections
   - Each child process handles one client independently

2. **Added SIGCHLD handler**:
   - Reaps zombie processes to prevent resource leaks
   - Uses `waitpid(-1, NULL, WNOHANG)` in signal handler

3. **Fixed VIEW path**:
   - Changed from `storage/storageN/files/` to `STORAGE_DIR/`
   - Now consistent with other operations

## Benefits

✅ **Multiple clients simultaneously**: Each gets their own process
✅ **WRITE doesn't block others**: Interactive sessions run in parallel
✅ **VIEW aggregation works**: Name server can query all storage servers at once
✅ **Better resource isolation**: One client crash doesn't affect others
✅ **Matches name server pattern**: Consistent architecture

## Testing

Start servers and test with multiple clients:

```bash
# Terminal 1: Name server
./name_server.out

# Terminal 2: Storage server 1
./storage_server.out

# Terminal 3: Storage server 2  
./storage_server.out

# Terminal 4: Client 1 - Start a WRITE (stays open)
./client.out
> WRITE file1.txt 0
> 0 Hello world

# Terminal 5: Client 2 - Should work immediately (not blocked!)
./client.out
> VIEW
> READ file2.txt

# Terminal 6: Client 3 - Also works
./client.out
> INFO file3.txt
```

All clients can work concurrently now! 🎉

## Alternative Approaches (Not Implemented)

- **Threading**: Use pthread for each client (more lightweight but needs mutex for shared state)
- **select/poll/epoll**: Single-threaded event-driven (complex but most efficient)
- **Thread pool**: Pre-create worker threads (good for high load)

For your use case, **fork-per-client is simple, correct, and sufficient**.
