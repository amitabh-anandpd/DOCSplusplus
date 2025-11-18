# WRITE Command Fix

## Problem Identified

The WRITE command wasn't ending when typing `ETIRW` because:

1. **Client side (`client/write.c`)**:
   - `recv()` wasn't null-terminating buffers
   - No return value checking on `recv()`
   - Could read garbage data or partial data

2. **Server side (`storage_server/write.c`)**:
   - `read()` return value wasn't checked
   - Buffer wasn't explicitly null-terminated
   - Newline characters from `fgets()` weren't stripped before comparison
   - `strncmp(recv_buf, "ETIRW", 5)` would fail if `recv_buf` had `"ETIRW\n"` or `"ETIRW\r\n"`

## What Was Fixed

### Client Side Changes (`src/client/write.c`)

```c
// Before:
recv(sock, buf, sizeof(buf), 0);          // No null-term, no check
printf("%s", buf);                        // Could print garbage

// After:
memset(buf, 0, sizeof(buf));
r = recv(sock, buf, sizeof(buf) - 1, 0); // Leave room for \0
if (r > 0) {
    buf[r] = '\0';                        // Explicit null-termination
    printf("%s", buf);                    // Safe to print
}
```

Added:
- ✅ Buffer initialization with `memset()`
- ✅ Return value checking for `recv()`
- ✅ Explicit null-termination
- ✅ User prompt (`> `) for better UX
- ✅ Connection closed detection

### Server Side Changes (`src/storage_server/write.c`)

```c
// Before:
char recv_buf[1024] = {0};
read(client_sock, recv_buf, sizeof(recv_buf));
if (strncmp(recv_buf, "ETIRW", 5) == 0)  // Fails if input is "ETIRW\n"

// After:
char recv_buf[1024];
memset(recv_buf, 0, sizeof(recv_buf));
ssize_t n = read(client_sock, recv_buf, sizeof(recv_buf) - 1);

if (n <= 0) {
    remove_lock(filename, sentence_num);  // Clean up on disconnect
    return;
}

recv_buf[n] = '\0';

// Strip newlines before comparison
recv_buf[strcspn(recv_buf, "\n")] = '\0';
recv_buf[strcspn(recv_buf, "\r")] = '\0';

if (strncmp(recv_buf, "ETIRW", 5) == 0)  // Now works correctly!
```

Added:
- ✅ Return value checking for `read()`
- ✅ Explicit null-termination
- ✅ Newline stripping before ETIRW comparison
- ✅ Proper cleanup if client disconnects mid-write

## How WRITE Now Works

```
Client                          Storage Server
  |                                    |
  |------ WRITE file.txt 0 ---------->|
  |                                    | (checks lock, creates .lock)
  |<--- "Sentence 0 locked..." --------|
  |                                    |
  |                                    |
> | 0 Hello world                      |
  |------ "0 Hello world\n" ---------->|
  |                                    | (parses, updates sentence)
  |<--- "Update applied..." -----------|
  |                                    |
> | 1 This is awesome                  |
  |------ "1 This is awesome\n" ------>|
  |<--- "Update applied..." -----------|
  |                                    |
> | ETIRW                              |
  |------ "ETIRW\n" ------------------>|
  |                                    | Strip "\n" → "ETIRW"
  |                                    | Matches! Save file
  |                                    | Remove .lock file
  |<--- "Write Successful!" -----------|
  |                                    |
```

## Testing Checklist

```bash
# Terminal 1: Name Server
./name_server.out

# Terminal 2: Storage Server
./storage_server.out

# Terminal 3: Client - Test WRITE
./client.out
> WRITE test.txt 0
Sentence 0 locked. You may begin writing.
> 0 Hello world
Update applied successfully.
> 1 This is a test.
Update applied successfully.
> ETIRW
Write Successful!

# Verify file was written
> READ test.txt
Hello world This is a test.

# Verify lock was removed
$ ls storage/storage1/files/
test.txt    # ✓ (no .lock file)
```

## Edge Cases Handled

✅ **Client disconnects mid-write**: Lock file is removed  
✅ **Empty input**: Handled gracefully  
✅ **Network errors**: Detected and cleaned up  
✅ **Newline variations**: `\n`, `\r\n`, `\r` all stripped  
✅ **Buffer safety**: Always null-terminated, no overflows  

## Result

The WRITE command now:
- ✅ Correctly detects `ETIRW` to end the session
- ✅ Saves the file properly
- ✅ Removes lock files after completion
- ✅ Handles disconnections gracefully
- ✅ Provides better user experience with prompts
