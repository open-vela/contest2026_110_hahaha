# qiban_music_service

Music playback service for the Qiban AI dashboard. Supports three audio sources: SD card local files, WiFi HTTP streaming, and Bluetooth A2DP.

## Architecture

```
┌─────────────────────────────────────────────────┐
│              qiban_music_service                 │
│                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │ SD Card   │  │ WiFi     │  │ Bluetooth│       │
│  │ WAV/PCM   │  │ HTTP     │  │ A2DP     │       │
│  │ /data/    │  │ Stream   │  │ (stub)   │       │
│  │ music/    │  │          │  │          │       │
│  └─────┬─────┘  └────┬─────┘  └────┬─────┘       │
│        │              │              │            │
│        └──────┬───────┴──────┬───────┘            │
│               ▼              ▼                    │
│         ┌──────────┐  ┌───────────┐               │
│         │ WAV      │  │ Audio     │               │
│         │ Parser   │  │ Device    │               │
│         └──────────┘  │ /dev/audio│               │
│                       │ /pcm0p    │               │
│                       └───────────┘               │
│                                                  │
│  State: /data/qiban_music_state.json             │
│  UI reads this file every 1 second               │
└─────────────────────────────────────────────────┘
```

## Commands

```bash
# Start as daemon
qiban_music_service &

# Playback control
qiban_music_service play
qiban_music_service pause
qiban_music_service stop
qiban_music_service next
qiban_music_service prev
qiban_music_service seek 120
qiban_music_service track 3

# Source selection
qiban_music_service source sdcard
qiban_music_service source wifi
qiban_music_service source bluetooth

# Volume & info
qiban_music_service volume 80
qiban_music_service scan
qiban_music_service list
qiban_music_service status
```

## Audio Sources

### SD Card

- Music directory: `/data/music/` or `/mnt/sdcard/music/`
- Supported formats: WAV (with header), raw PCM (16kHz/16bit/mono)
- Auto-scans directory on startup and on `scan` command
- Tracks sorted alphabetically by filename
- Auto-advances to next track when current finishes

### WiFi Streaming

- HTTP streaming from a configurable URL
- Default URL: `http://10.0.0.1:8080/stream`
- Configure via `QIBAN_MUSIC_WIFI_URL` environment variable
- Assumes 16kHz/16bit/mono PCM stream (after HTTP header)
- Server should send raw PCM data with HTTP/1.0 response

### Bluetooth

- Stub implementation (requires NuttX Bluetooth stack)
- Needs `CONFIG_BLUETOOTH=y` and A2DP profile support
- For contest demo, use SD card or WiFi source

## State File

`/data/qiban_music_state.json` — read by qiban_ui every second:

```json
{
  "title": "夜曲",
  "artist": "周杰伦",
  "album": "十一月的萧邦",
  "source": "sdcard",
  "playing": 1,
  "duration_sec": 235,
  "position_sec": 45,
  "volume": 60,
  "track_index": 0,
  "track_count": 10
}
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `QIBAN_MUSIC_WIFI_URL` | `http://10.0.0.1:8080/stream` | WiFi stream URL |

## Building WAV Test Files

```bash
# Generate a 10-second 16kHz 16-bit mono tone (on host)
sox -n -r 16000 -b 16 -c 1 test.wav synth 10 sine 440

# Copy to device
adb push test.wav /data/music/
```
