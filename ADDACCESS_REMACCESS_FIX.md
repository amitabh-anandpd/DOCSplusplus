# ADDACCESS/REMACCESS Fix for Multi-Device Deployment

## Problem

The ADDACCESS and REMACCESS commands were only updating the Name Server's in-memory hashmap and not forwarding the changes to the Storage Server's `.meta` files. This caused:

1. **Changes lost on restart**: After restarting the Name Server, ACL changes were gone
2. **Inconsistent state**: Direct queries to Storage Server showed old ACL data
3. **Multi-device failures**: Commands failed when NM and SS were on different devices

## Root Cause

In `src/name_server/server.c`, ADDACCESS and REMACCESS were handled in the **parent process** (lines 705-820) and only modified the NM's hashmap:

```c
// OLD CODE (REMOVED):
else if (strncmp(peek_command, "ADDACCESS ", 10) == 0) {
    // Only updated meta->read_users and meta->write_users in hashmap
    // Did NOT forward to storage server
    FileMeta *meta = find_filemeta(filename);
    strcat(meta->read_users, target_user);  // In-memory only!
    send(client_sock, response, strlen(response), 0);
    close(client_sock);
    continue;  // Did not forward to SS
}
```

## Solution

### 1. Name Server Changes (`src/name_server/server.c`)

**Removed** the parent process handlers for ADDACCESS and REMACCESS (lines 705-820). These commands now fall through to the child process where they are:

1. Authenticated (username/password check)
2. Routed to the appropriate storage server (like CREATE, DELETE, etc.)
3. Forwarded with authentication headers

### 2. Storage Server Changes (`src/storage_server/server.c`)

**Added** command handlers for ADDACCESS and REMACCESS:

```c
else if (strncmp(buffer, "ADDACCESS ", 10) == 0) {
    // Parse: ADDACCESS -R|-W <filename> <target_username>
    char flag[8], filename[256], target_user[64];
    
    if (sscanf(buffer + 10, "%s %s %s", flag, filename, target_user) == 3) {
        FileMetadata meta;
        if (read_metadata_file(filename, &meta) == 0) {
            if (strcmp(meta.owner, username) == 0) {
                if (strcmp(flag, "-R") == 0) {
                    add_read_access(filename, target_user);
                } else if (strcmp(flag, "-W") == 0) {
                    add_write_access(filename, target_user);
                }
                // Updates .meta file on disk!
            }
        }
    }
}

else if (strncmp(buffer, "REMACCESS ", 10) == 0) {
    // Parse: REMACCESS <filename> <target_username>
    char filename[256], target_user[64];
    
    if (sscanf(buffer + 10, "%s %s", filename, target_user) == 2) {
        FileMetadata meta;
        if (read_metadata_file(filename, &meta) == 0) {
            if (strcmp(meta.owner, username) == 0) {
                remove_all_access(filename, target_user);
                // Updates .meta file on disk!
            }
        }
    }
}
```

## Benefits

1. **ACL changes persist**: Changes written to `.meta` files survive restarts
2. **Single source of truth**: Storage Server `.meta` files are authoritative
3. **Multi-device support**: Commands work correctly when NM and SS are on different devices
4. **Consistent behavior**: ADDACCESS/REMACCESS now work like CREATE, DELETE, READ, WRITE

## Testing

To verify the fix works:

### Single Device Test

```bash
# Terminal 1: Start NM
./name_server.out

# Terminal 2: Start SS
./storage_server.out 127.0.0.1 9000

# Terminal 3: Test ADDACCESS
./client.out 127.0.0.1 9000
> LOGIN admin admin123
> CREATE testfile.txt
> ADDACCESS -R testfile.txt alice
> INFO testfile.txt  # Should show alice in READ_USERS

# Verify persistence
> QUIT

# Restart NM and SS, then reconnect
./client.out 127.0.0.1 9000
> LOGIN admin admin123
> INFO testfile.txt  # Should STILL show alice in READ_USERS
```

### Multi-Device Test

```bash
# Device A (NM): ./name_server.out
# Device B (SS): ./storage_server.out <NM_IP> 9000
# Device A (Client): ./client.out <NM_IP> 9000

# Same commands as above - should work identically
```

## Files Changed

1. **src/name_server/server.c**
   - Removed parent process handlers for ADDACCESS/REMACCESS (lines 705-820)
   - Commands now forwarded to SS via child process

2. **src/storage_server/server.c**
   - Added ADDACCESS handler (calls `add_read_access` or `add_write_access`)
   - Added REMACCESS handler (calls `remove_all_access`)
   - Uses existing ACL functions from `acl.c` to update `.meta` files

## Existing ACL Functions (No Changes Needed)

The ACL implementation in `src/storage_server/acl.c` already had the correct functions:

- `add_read_access(filename, username)` - Adds user to READ_USERS in .meta file
- `add_write_access(filename, username)` - Adds user to WRITE_USERS in .meta file
- `remove_all_access(filename, username)` - Removes user from both lists in .meta file

These functions read the `.meta` file, update it, and write it back - exactly what we need!

## Command Flow (After Fix)

```
Client ──ADDACCESS──> Name Server ──authenticate──> Child Process ──forward──> Storage Server
                                                                                      │
                                                                                      ▼
                                                                              Update .meta file
                                                                                      │
                                                                                      ▼
                                                                              Response ──> Client
```

## Related Issues Fixed

This fix also resolves the following issues:
- ADDACCESS/REMACCESS not working in multi-device setup ✓
- ACL changes lost after NM restart ✓
- Inconsistent ACL state between NM hashmap and SS .meta files ✓

## Next Steps

After testing ADDACCESS/REMACCESS, verify these commands also work in multi-device setup:
- INFO (for existing files) - Should already work (fetches from SS)
- STREAM - Should already work (forwarded like READ/WRITE)
