# qiban_ota_service

Application-layer OTA (Over-The-Air) update service for the Qiban AI dashboard.

## Overview

This service provides **application-level OTA updates** — it can replace the 9 C service binaries, QuickApp RPK, and AI Agent skills without reflashing the entire firmware.  Firmware-level OTA (kernel/driver updates) is handled separately via the bootloader's `ota.zip` mechanism.

## Architecture

```
┌─────────────────┐     HTTP      ┌─────────────────┐
│  Relay Server    │◄────────────►│  qiban_ota_service│
│  (FastAPI)       │              │  (this service)   │
│                  │              │                    │
│  /api/ota/check  │              │  1. check update   │
│  /api/ota/app/*  │              │  2. download       │
│                  │              │  3. verify SHA256  │
└─────────────────┘              │  4. stop services  │
                                  │  5. replace bins   │
                                  │  6. restart svcs   │
                                  └────────────────────┘
```

## Commands

```bash
# Start as background daemon (polls every 300s)
qiban_ota_service &

# One-shot version check
qiban_ota_service check

# Print current OTA status
qiban_ota_service status

# Apply a local OTA package
qiban_ota_service apply /data/app_ota/download.zip

# Rollback to previous version
qiban_ota_service rollback
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `QIBAN_OTA_SERVER` | `10.0.0.1` | Relay server IP address |
| `QIBAN_OTA_PORT` | `8787` | Relay server port |
| `QIBAN_OTA_INTERVAL` | `300` | Poll interval in seconds |

## File Layout

```
/data/app_ota/
├── version.json          # Current installed version
├── download.zip          # Last downloaded OTA package
├── staging/              # Temporary extraction directory
├── backup/               # Previous version binaries
└── status.json           # OTA state (written to /data/ota/)

/data/ota/
├── status.json           # OTA status (readable by UI/agent)
└── ota.zip               # Firmware OTA package (bootloader reads this)
```

## OTA Status JSON

```json
{
  "state": "idle",
  "progress_pct": 0,
  "current_version": "1.0.0",
  "target_version": "",
  "error_msg": "",
  "last_check_time": "2026-08-06T10:00:00Z"
}
```

States: `idle`, `checking`, `downloading`, `verifying`, `applying`, `done`, `failed`, `rollback`

## Server API

The service communicates with the relay server via these endpoints:

- `GET /api/ota/check?device_id=...&app_version=...` — Check for updates
- `GET /api/ota/app/download?version=...` — Download app package
- `POST /api/ota/device/{id}/status` — Report device version

## Build

Enabled via Kconfig symbol `CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE`.

The `enable_r528_qiban_mvp.py` script enables it automatically.
