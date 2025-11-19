# Docs++ Authentication System

## Overview
The Docs++ distributed file system now includes a comprehensive authentication and access control system.

## Authentication Flow

### 1. Login Process
When a client starts, it will:
1. Prompt for username and password
2. Send authentication request to the name server immediately
3. Name server validates credentials against `storage/users.txt`
4. If authentication fails, client prompts to retry
5. If authentication succeeds, client proceeds to command mode

### 2. Pre-configured Users
Located in `storage/users.txt`:
- `admin:admin123`
- `alice:password1`
- `bob:password2`
- `user1:pass1`

You can add more users by editing this file with format: `username:password`

## Testing the Authentication

### Test 1: Failed Login
```bash
# Start name server
./name_server.out

# In another terminal, start storage server
./storage_server.out

# In another terminal, start client
./client.out

# Try wrong credentials
Username: wronguser
Password: wrongpass
# Result: ✗ Authentication failed. Invalid username or password.
# Prompts to try again
```

### Test 2: Successful Login
```bash
./client.out

# Enter valid credentials
Username: admin
Password: admin123
# Result: ✓ Authentication successful! Welcome, admin!
# Proceeds to command prompt
```

## Access Control Features

### File Ownership
- Every file created has an owner (the user who created it)
- Owner always has full read and write access
- Owner can grant/revoke access to other users

### Commands

#### Create a file (as admin)
```
CREATE myfile.txt
```

#### Grant read access to another user
```
ADDACCESS -R myfile.txt alice
```

#### Grant write access to another user
```
ADDACCESS -W myfile.txt bob
```

#### Revoke all access from a user
```
REMACCESS myfile.txt alice
```

#### View file metadata and permissions
```
INFO myfile.txt
```
Shows: owner, creation time, read access list, write access list

### Access Control on Commands
- **READ**: Requires read access
- **WRITE**: Requires write access  
- **DELETE**: Requires write access (only owner/write access users)
- **INFO**: Requires read access
- **STREAM**: Requires read access
- **EXEC**: Requires read access
- **VIEW**: No access control (lists all files)

### Testing Access Control

#### Scenario 1: Owner creates and manages file
```bash
# Login as admin
Username: admin
Password: admin123

# Create a file
CREATE testfile.txt

# Write to it
WRITE testfile.txt 0
# (enter text and send ETIRW to finish)

# Check info
INFO testfile.txt
# Shows admin as owner, with read and write access
```

#### Scenario 2: Grant access to another user
```bash
# As admin, grant read access to alice
ADDACCESS -R testfile.txt alice

# Grant write access to bob
ADDACCESS -W testfile.txt bob
```

#### Scenario 3: Another user tries to access
```bash
# In a new terminal, start another client
./client.out

# Login as alice
Username: alice
Password: password1

# Try to read the file (should work)
READ testfile.txt

# Try to write to the file (should fail - no write access)
WRITE testfile.txt 0
# Error: Access denied. You do not have write permission

# Try to delete the file (should fail - no write access)
DELETE testfile.txt
# Error: Access denied. You do not have permission to delete
```

#### Scenario 4: User with write access
```bash
# Login as bob
Username: bob
Password: password2

# Try to write (should work - bob has write access)
WRITE testfile.txt 0
# (enter text and send ETIRW)

# Try to delete (should work - bob has write access)
DELETE testfile.txt
# Success
```

## Metadata Files
Each file in `storage/storage<N>/files/` has a corresponding metadata file in `storage/storage<N>/meta/` with:
- Owner username
- Creation timestamp
- Last access timestamp  
- Read access list (comma-separated usernames)
- Write access list (comma-separated usernames)

Example metadata file (`testfile.txt.meta`):
```
OWNER:admin
CREATED:1700000000
LAST_ACCESS:1700000100
READ_USERS:admin,alice
WRITE_USERS:admin,bob
```

## Security Notes
- Credentials are transmitted with each command (in production, use TLS/SSL)
- File owner cannot have their access revoked
- Only the file owner can grant or revoke access
- Authentication is required before any file operations
- Invalid credentials cause immediate rejection at login

## Quick Start Commands
```bash
# Terminal 1: Name Server
./name_server.out

# Terminal 2: Storage Server
./storage_server.out

# Terminal 3: Client
./client.out
# Login with admin:admin123

# Create and test access control
CREATE test.txt
WRITE test.txt 0
INFO test.txt
ADDACCESS -R test.txt alice
ADDACCESS -W test.txt bob
```
