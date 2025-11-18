# Path Consistency Fix - All Storage Operations

## Problem Identified

Your storage server had **inconsistent file paths** across different operations:

```
READ:   ./storage/storage1/files/file1.txt  ✓
CREATE: ./storage/file1.txt                 ✗  (WRONG!)
INFO:   ./storage/file1.txt                 ✗  (WRONG!)
WRITE:  ./storage/storage1/files/file1.txt  ✓
VIEW:   scans ./storage/                    ✗  (WRONG!)
```

This meant:
- Files created with CREATE weren't found by READ
- INFO/STREAM/EXECUTE couldn't find files
- VIEW showed wrong directory contents
- Registration sent empty file lists

## Files Updated

All operations now use consistent per-server paths: `./storage/storage{N}/files/`

### ✅ Fixed Files:

1. **server.c**
   - `create_file()`: Now uses `storage/storage{id}/files/`
   - `build_file_list()`: Scans correct per-server directory
   
2. **info.c**
   - `file_info()`: Updated path to per-server directory

3. **stream.c**
   - `stream_file()`: Updated path to per-server directory

4. **execute.c**
   - `execute_file()`: Updated path to per-server directory

5. **delete.c**
   - `delete_from_storage()`: Updated path to per-server directory

6. **view.c**
   - `list_files()`: Now scans per-server `files/` directory

7. **write.c**
   - Lock functions: Updated to use per-server directory
   - `is_locked()`, `create_lock()`, `remove_lock()`

## Result: Consistent File Paths

```
Storage Server 1 (ID=1):
  ./storage/storage1/files/
    ├── file1.txt
    ├── file2.txt
    └── file1.txt.0.lock (write lock)

Storage Server 2 (ID=2):
  ./storage/storage2/files/
    ├── fileA.txt
    └── fileB.txt
```

All operations (READ, WRITE, CREATE, DELETE, INFO, STREAM, EXECUTE, VIEW) now work on the **same directory**.

## Concurrency Safety (from previous fix)

With the fork-per-client model:
- ✅ Multiple clients can READ the same file concurrently (safe)
- ✅ Multiple clients can READ different files concurrently (safe)
- ✅ WRITE operations use lock files (`.lock`) to prevent conflicts
- ✅ Each client runs in its own process - isolated and safe
- ✅ OS handles concurrent file I/O correctly

## Testing Checklist

```bash
# Start name server + storage servers
./name_server.out
./storage_server.out  # Gets ID 1, uses storage/storage1/files/
./storage_server.out  # Gets ID 2, uses storage/storage2/files/

# Test from client
./client.out
> CREATE test.txt          # Creates in storage1 or storage2
> READ test.txt            # Reads from same location
> INFO test.txt            # Shows info from same location
> WRITE test.txt 0         # Writes to same location
> VIEW                     # Shows all files from all servers
> DELETE test.txt          # Deletes from correct location
```

All operations should now work consistently! ✅
