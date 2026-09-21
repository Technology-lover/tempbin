# TempDrop

Production-oriented temporary pastebin and file-drop service.

## Features

- C++20 backend
- SQLite durable metadata
- 24-hour persistent expiry
- Crash/restart recovery
- Atomic file commits
- Configurable file limit without recompilation
- Drag-and-drop uploads
- `wget`/curl-compatible download URLs
- Nginx reverse proxy
- systemd service hardening
- Four-worker configuration for the target 4-vCPU host
- Pastebin with recent pastes and remaining lifetime
- Minimal Astra-inspired dark UI

## Architecture

```text
Internet
   |
 Nginx :443/:80
   |\
   | \__ /api/* and /file/* -> 127.0.0.1:8080
   |
 static UI
             C++ TempDrop
                 |
        +--------+---------+
        |                  |
     SQLite             /srv/tempdrop
     metadata            /files
                         /tmp
```
## How to run

'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release'

'cmake --build build -j$(nproc)'

'.build/tempdrop .config.json'

## Reliability model

The expiration deadline is stored as `expires_at` in SQLite. The cleanup thread is only a convenience for timely deletion; it is not the source of truth.

On every startup:

1. SQLite is opened with WAL and `synchronous=FULL`.
2. Temporary `.uploading` files are removed.
3. Files in the committed directory that are not represented by SQLite are removed.
4. The cleanup worker scans `expires_at <= current_time`.

An application crash therefore cannot reset the lifetime of an object.

## Ubuntu installation

```bash
sudo ./deploy/ubuntu-install.sh
```

Edit `/etc/tempdrop/config.json`.

For example:

```json
"max_file_size_mb": 50
```

Then restart:

```bash
sudo systemctl restart tempdrop
```

No recompilation is required for limit changes.

## TLS

Put the site behind HTTPS. For a public deployment, use a real certificate (Certbot/Let's Encrypt or your existing TLS proxy).

After TLS is configured, downloads work as:

```bash
wget https://your-domain.example/file/<stored-name>
```

## Operations

```bash
systemctl status tempdrop
journalctl -u tempdrop -f
sudo systemctl restart tempdrop
sudo nginx -t
```

Database:

```text
/srv/tempdrop/tempdrop.db
```

Files:

```text
/srv/tempdrop/files
```

## Important production note

This version intentionally has no authentication. Anyone who can reach the service can upload, paste and download. If the service is exposed to the public Internet, add authentication/rate limiting at Nginx or a trusted access proxy before treating it as an unrestricted public drop box.
