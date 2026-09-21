# TempDrop — System Design

## 1. Purpose

TempDrop is a self-hosted ephemeral sharing application providing two functions:

1. Pastebin for short-lived text.
2. Temporary file storage with direct download URLs.

The application targets an Ubuntu Server host with 4 vCPU and 3 GB RAM and is designed for at least 20 concurrent users.

## 2. Design goals

- Persistent state across process crashes/restarts.
- Never delete an object before its configured expiry deadline.
- No expiry state held only in RAM.
- File size limit configurable without recompilation.
- Simple browser UI with drag-and-drop.
- Direct HTTP downloads compatible with `wget`.
- Nginx termination/reverse proxy.
- Small operational footprint.
- Deterministic recovery after abnormal termination.

## 3. High-level architecture

```text
                    Internet
                       |
                 HTTPS / HTTP
                       |
                +--------------+
                |    Nginx     |
                | TLS + proxy  |
                +------+-------+
                       |
              /api/* /file/*
                       |
              +--------v--------+
              |  TempDrop C++   |
              |  HTTP service   |
              +---+---------+---+
                  |         |
                  |         +----------------+
                  |                          |
            +-----v------+             +-----v------+
            |   SQLite   |             | Filesystem |
            | metadata   |             | file blobs |
            +------------+             +------------+
```

The browser-facing static assets are served by Nginx. Dynamic API and download requests are proxied to the C++ service.

## 4. Components

### 4.1 Nginx

Responsibilities:

- TLS termination.
- Static frontend delivery.
- Reverse proxy.
- Request body limit.
- Connection/request timeout policy.
- Optional future rate limiting/authentication.

Nginx is not responsible for object expiry.

### 4.2 C++ application

Modules:

- `config.*`: runtime configuration.
- `database.*`: SQLite persistence.
- `storage.*`: filesystem paths, filename sanitization, startup reconciliation.
- `cleanup.*`: periodic expiry processing.
- `api.*`: HTTP routes and request/response handling.
- `main.cpp`: lifecycle and service startup.

### 4.3 SQLite

SQLite is the authoritative metadata store.

It uses:

- WAL journaling.
- `synchronous=FULL`.
- foreign key enforcement.
- busy timeout.

Schema:

```sql
files(
    id TEXT PRIMARY KEY,
    original_name TEXT NOT NULL,
    stored_name TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
)

pastes(
    id TEXT PRIMARY KEY,
    content TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
)
```

Expiry indexes are used to make cleanup efficient.

### 4.4 Filesystem

```text
/srv/tempdrop/
├── tempdrop.db
├── files/
└── tmp/
```

`tmp/` contains interrupted uploads only. A successfully committed file is placed in `files/`.

## 5. Upload transaction

The upload sequence is:

```text
HTTP multipart request
       |
       v
Validate request size
       |
       v
Validate/sanitize filename
       |
       v
Generate cryptographically unpredictable ID
       |
       v
Write <id>.uploading to tmp/
       |
       v
Flush/close
       |
       v
Atomic rename into files/
       |
       v
Insert metadata into SQLite
       |
       v
Return download URL
```

The application does not expose the temporary pathname.

### Failure cases

#### Crash before rename

A `.uploading` file remains in `tmp/`.

Startup removes it.

#### Crash after rename but before DB insert

A committed-looking file exists without metadata.

Startup reconciliation removes it.

#### Crash after DB insert

The database record and final file both exist.

Normal cleanup handles expiry.

## 6. Expiration model

Every object receives an absolute expiry timestamp:

```text
expires_at = created_at + configured_lifetime
```

The configured lifetime is read from `config.json`.

Deletion condition:

```text
current_time >= expires_at
```

The cleanup worker runs periodically, but the database timestamp remains the source of truth.

Therefore:

```text
process crashes
      |
      v
restart
      |
      v
read persistent expires_at
      |
      v
delete only if deadline has passed
```

Changing the cleanup interval cannot cause early deletion.

## 7. Direct file downloads

The public form is:

```text
/file/<stored-name>
```

The stored name contains a random ID prefix:

```text
<random-id>_<sanitized-original-name>
```

Example:

```text
/file/0f3a...9c2_report.pdf
```

This prevents filename collisions while retaining a human-readable filename.

The server validates that the path contains no `/`, `\`, or `..` traversal components.

## 8. API

### Health

```http
GET /api/health
```

### Create paste

```http
POST /api/paste
Content-Type: text/plain

hello
```

### List pastes

```http
GET /api/pastes
```

### Get paste

```http
GET /api/paste/<id>
```

### Delete paste

```http
DELETE /api/paste/<id>
```

### Upload file

```http
POST /api/files
Content-Type: multipart/form-data
```

### List files

```http
GET /api/files
```

### Download

```http
GET /file/<stored-name>
```

## 9. Concurrency

The service uses four HTTP worker threads by default.

SQLite is compiled/opened in serialized mode and guarded by a process-level mutex around database operations.

Expected workload:

- 20+ simultaneous users.
- Small paste requests.
- File uploads up to the configured limit.
- Direct file downloads.

For this workload, the dominant resource is expected to be network/disk I/O rather than CPU.

## 10. Memory model

The current upload path buffers an individual multipart request in the HTTP framework before processing.

With a 20 MB limit and 20 simultaneous maximum-size uploads, the payload scale is roughly:

```text
20 × 20 MB = 400 MB
```

This is within a 3 GB RAM allocation, but it is deliberately not an unlimited-memory design.

If the deployment later needs hundreds of concurrent uploads or substantially larger files, the upload path should be changed to a streaming multipart parser.

## 11. Security boundaries

Current application protections:

- Filename sanitization.
- Path traversal rejection.
- Random stored identifiers.
- Nginx body-size limit.
- systemd `NoNewPrivileges`.
- `ProtectSystem=strict`.
- `ProtectHome=true`.
- Restricted writable directory.
- Non-root service account.

Not included by default:

- User authentication.
- Per-user quotas.
- Virus scanning.
- Content inspection.
- Abuse/rate limiting.

For public Internet exposure, place authentication/rate limiting in front of the service.

## 12. Deployment

```text
/usr/local/bin/tempdrop
/etc/tempdrop/config.json
/etc/systemd/system/tempdrop.service
/var/www/tempdrop/
/etc/nginx/sites-available/tempdrop
/srv/tempdrop/
```

The systemd service runs as an unprivileged `tempdrop` account.

## 13. Operational recovery

After a crash:

1. systemd restarts the service.
2. SQLite WAL recovers committed transactions.
3. Temporary upload artifacts are removed.
4. Unreferenced final files are reconciled.
5. Expired records are deleted.
6. Valid unexpired records remain accessible.

No in-memory scheduler state is required to recover object lifetime.

## 14. Configuration

The main operational parameters are:

```json
{
  "limits": {
    "max_file_size_mb": 20,
    "max_paste_size_kb": 512,
    "file_expiry_hours": 24,
    "paste_expiry_hours": 24
  }
}
```

The file-size limit and expiry periods can therefore be changed without recompiling.

## 15. Resource sizing

Target host:

```text
CPU: 4 vCPU
RAM: 3 GB
```

Application configuration:

```text
HTTP workers: 4
MemoryMax: 2800 MB
File limit: 20 MB
Cleanup interval: 60 seconds
```

The remaining memory is intentionally left for Ubuntu, Nginx, SQLite page cache and filesystem cache.

## 16. Design trade-offs

### SQLite vs PostgreSQL

SQLite is sufficient because the workload is small and metadata operations are simple. It also makes the application self-contained and reduces operational complexity.

### Filesystem vs BLOBs in SQLite

Files are stored in the filesystem because large binary objects are better handled by the filesystem for this use case. SQLite stores metadata only.

### Nginx vs application TLS

Nginx handles TLS so the C++ application can remain an internal HTTP service bound to `127.0.0.1`.

### In-memory expiry timer vs persistent timestamps

Persistent timestamps are mandatory for crash correctness. The timer is only a cleanup accelerator.

## 17. Future extensions

The architecture can later add:

- Authentication.
- Per-user namespaces.
- Upload quotas.
- Rate limiting.
- Password-protected files.
- One-time download links.
- Content hashing/deduplication.
- Streaming multipart uploads.
- ClamAV integration.
- Prometheus metrics.
- Admin dashboard.
- S3-compatible object storage.
