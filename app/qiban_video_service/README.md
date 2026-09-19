# qiban_video_service

Local video playback service for the Qiban AI dashboard. Uses the R528 VE (Video Engine) hardware decoder via libcedarx for MP4/H.264/H.265 playback.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│              qiban_video_service                         │
│                                                          │
│  ┌──────────┐    ┌──────────────────────────────────┐   │
│  │ File     │    │  libcedarx XPlayer                │   │
│  │ Scanner  │    │  ┌────────┐  ┌────────┐          │   │
│  │ /data/   │    │  │ Parser │  │ Video  │          │   │
│  │ videos/  │    │  │ (MP4/  │  │ Decoder│          │   │
│  │          │    │  │  MKV)  │  │ (VE)   │          │   │
│  └──────────┘    │  └───┬────┘  └───┬────┘          │   │
│                  │      │           │                │   │
│                  │  ┌───┴───────────┴────┐          │   │
│                  │  │  Audio Decoder     │          │   │
│                  │  │  (AAC/MP3)         │          │   │
│                  │  └────────┬───────────┘          │   │
│                  └───────────┼──────────────────────┘   │
│                              │                           │
│                  ┌───────────┼───────────┐               │
│                  │           │           │               │
│              ┌───┴───┐  ┌───┴───┐  ┌───┴───┐           │
│              │ DE    │  │ Audio │  │ State │           │
│              │ Layer │  │ Out   │  │ JSON  │           │
│              │(LCD)  │  │(SPK)  │  │       │           │
│              └───────┘  └───────┘  └───────┘           │
└─────────────────────────────────────────────────────────┘
```

## Commands

```bash
qiban_video_service list           # List video files
qiban_video_service play 0         # Play first video
qiban_video_service play /data/videos/test.mp4
qiban_video_service pause          # Pause
qiban_video_service resume         # Resume
qiban_video_service stop           # Stop
qiban_video_service seek 120       # Seek to 2:00
qiban_video_service scan           # Rescan directory
qiban_video_service status         # Print state JSON
```

## Supported Formats

| Container | Video Codec | Audio Codec |
|-----------|------------|-------------|
| MP4 | H.264, H.265, MPEG4 | AAC, MP3 |
| MKV | H.264, H.265 | AAC, MP3, FLAC |
| AVI | MPEG4, H.264 | MP3 |
| FLV | H.264 | AAC |
| TS | H.264, H.265 | AAC |
| WebM | VP8 | Vorbis |

## Video Directory

Place video files in `/data/videos/` or `/mnt/sdcard/videos/`.

## Dependencies

- `CONFIG_MULTIMEDIA_LIBCEDARX=y` — R528 multimedia framework
- `CONFIG_DRIVERS_DISP2_SUNXI=y` — Display Engine for video output
- `CONFIG_AW_AUDIO_CODEC=y` — Audio codec support

## State File

`/data/qiban_video_state.json`:

```json
{
  "state": "playing",
  "current_index": 0,
  "current_name": "test.mp4",
  "position_sec": 45,
  "duration_sec": 235,
  "file_count": 3,
  "error_msg": ""
}
```

## Hardware Decoding

The R528 VE (Video Engine) provides hardware-accelerated decoding:

| Codec | Resolution | FPS | CPU Usage |
|-------|-----------|-----|-----------|
| H.264 | 1080p | 60fps | <5% |
| H.265 | 1080p | 60fps | <5% |
| MPEG4 | 1080p | 30fps | <5% |
| VP8 | 1080p | 30fps | <10% |

Without VE hardware (software decoding), CPU usage would be >80% for 1080p.
