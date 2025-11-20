[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# Docs++

A minimal distributed file system with:
- Name Server (NM): central index, metadata & access control.
- Storage Servers (SS): store files + per-file .meta.
- Clients: issue commands (auth required).

## Features
- Multi-device deployment (NM, SS, Clients on different hosts)
- File operations: CREATE, READ, WRITE (sentence-based), DELETE
- Metadata: OWNER, CREATED, LAST_MODIFIED, LAST_ACCESS, READ/WRITE ACLs
- Access control: ADDACCESS, REMACCESS
- Streaming: STREAM <file> (direct SS fetch after LOCATE)
- Location: LOCATE <file> returns SS_IP / SS_PORT
- Info: INFO <file> returns metadata + storage location
- Undo last sentence write: UNDO <file>
- Checkpoints (versioning): CHECKPOINT / VIEWCHECKPOINT / REVERT / LISTCHECKPOINTS

## Process Roles
### Name Server
- Listens on a public IP (default port 8080)
- Accepts SS registrations
- Maintains in-memory file index (filename → storage server list + metadata snapshot)
- Routes READ/WRITE/DELETE (or supplies SS location for LOCATE/STREAM)
- Answers INFO using stored metadata or refreshed from SS

### Storage Server
- Listens on port: BASE_PORT (e.g. 8081) + server_id
- On startup: registers with NM and reports actual listening port
- Stores: files/, meta/ (one .meta per file)
- Updates LAST_MODIFIED / LAST_ACCESS on WRITE / READ
- Enforces owner for ACL changes

### Client
- Knows NM_IP (env NAME_SERVER_IP or compiled default)
- Authenticates (USER/PASS)
- Uses:
  - Direct NM commands (INFO, CREATE, DELETE, ADDACCESS, REMACCESS)
  - LOCATE then STREAM direct to SS
  - READ/WRITE proxied via NM

## Command Summary (Client → NM unless noted)
| Command | Purpose |
|---------|---------|
| VIEW [-a|-l|-al] | List files (optional flags for all/long) |
| CREATE <file> | Create empty file (initialize metadata) |
| READ <file> <sentence_num> | Read one sentence |
| WRITE <file> <sentence_num> | Interactive write / edit sentence |
| DELETE <file> | Delete file |
| INFO <file> | Show metadata + storage location |
| STREAM <file> | Stream full file (uses LOCATE then direct SS) |
| LOCATE <file> | Get SS_IP / SS_PORT |
| ADDACCESS -R|-W <file> <user> | Grant read or write access |
| REMACCESS <file> <user> | Revoke user access (not owner) |
| UNDO <file> | Undo last WRITE sentence change |
| CHECKPOINT <file> <tag> | Create tagged checkpoint |
| VIEWCHECKPOINT <file> <tag> | View checkpoint content |
| REVERT <file> <tag> | Restore file content from checkpoint |
| LISTCHECKPOINTS <file> | List all checkpoints for file |
| MENU or HELP | Show command menu again |
| EXIT / QUIT | Leave client |

## Metadata File Format (storage_server/meta/<filename>.meta)
```
OWNER:admin
CREATED:<unix_ts>
LAST_MODIFIED:<unix_ts>
LAST_ACCESS:<unix_ts>
READ_USERS:comma,separated
WRITE_USERS:comma,separated
```

## Build
```bash
make all
```

Produces:
- name_server.out
- storage_server.out
- client.out

## Run (Example)
On NM host:
```bash
./name_server.out
```
On SS host (server_id = 1):
```bash
NAME_SERVER_IP=<nm_ip> ./storage_server.out 1
```
On Client host:
```bash
NAME_SERVER_IP=<nm_ip> ./client.out
```

## Checkpoint & Undo System
| Command | Description |
|---------|-------------|
| UNDO <file> | Reverts the last WRITE sentence change if available |
| CHECKPOINT <file> <tag> | Saves a snapshot of the entire file under a tag |
| VIEWCHECKPOINT <file> <tag> | Streams the content of the checkpoint |
| REVERT <file> <tag> | Restores file from a checkpoint |
| LISTCHECKPOINTS <file> | Lists all checkpoints (tag, timestamp, size, creator) |

Checkpoint files stored at: `storage/storageX/checkpoints/`
Naming: `<sanitized_filename>_<tag>.ckpt` with companion `.meta` (timestamp, creator).

## Typical Session
```
USER:admin
PASS:admin123
CREATE notes.txt
WRITE notes.txt 0
UNDO notes.txt
CHECKPOINT notes.txt base
WRITE notes.txt 1
LISTCHECKPOINTS notes.txt
VIEWCHECKPOINT notes.txt base
REVERT notes.txt base
STREAM notes.txt
```

## Environment Variables
- NAME_SERVER_IP: override default NM IP for client & SS startup.

## Logging
- Name Server: storage/nameserver.log (DEBUG only here).
- Terminal shows INFO/WARN/ERROR.

## Error Cases
- “Could not find storage server…” → file not indexed (create via client or ensure SS registered before NM restart).
- Stale metadata after WRITE → ensure refresh logic active (NM fetches INFO from SS).
- Connection refused → port mismatch (SS must report actual listening port).

## Extensibility
- Add replication: track multiple ss_ids per file.
- Add TTL-based metadata refresh.
- Add bulk re-index on NM restart.

## Security Notes
- Plaintext auth (USER/PASS) – replace with hashed credentials for production.
- No encryption – use TLS for secure environments.

## Cleanup
To reset:
```bash
rm -rf storage/storage*/files/* storage/storage*/meta/*
```

## License
Internal academic project (adjust as needed).

## Troubleshooting Quick Checklist
1. STREAM fails → Check LOCATE response contains SS_IP / SS_PORT.
2. INFO stale → Confirm WRITE updated .meta; NM refreshed via INFO path.
3. ADDACCESS no effect → Ensure command routed to SS (now persists in .meta).
4. Port mismatch → Verify SS reported listening port = BASE + id.

## Minimal Code Touch Points for Multi-Device
- NM registration: uses source IP of SS (no reliance on reported IP).
- SS reports actual listening port.
- Client parses SS_IP / SS_PORT from NM (LOCATE / INFO).

## Disclaimer
Prototype. Not hardened for production workloads.