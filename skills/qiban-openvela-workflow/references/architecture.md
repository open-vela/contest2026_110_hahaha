# Qiban Architecture Reference

## Runtime data flow

```text
UART / test input -> qiban_sensor_bridge -> /data/qiban_inputs/*
                                         -> /data/qiban_location_state.json

/data/qiban_inputs/* + GPADC + fallback motion
  -> qiban_vehicle_service -> /data/qiban_vehicle_state.json

location + destination -> qiban_nav_service -> /data/qiban_nav_state.json
                                          -> qiban_map_service

vehicle + location + nav + map + weather + music + voice + video state
  -> qiban_ui -> /dev/lcd0 and /dev/input0
```

State publishers should write a temporary file and rename it atomically. Consumers must preserve a valid previous/default state when a file is absent or malformed.

## Source areas

| Path | Responsibility |
|---|---|
| `app/qiban_vehicle_service` | Vehicle telemetry aggregation and basic alerts |
| `app/qiban_sensor_bridge` | UART/text telemetry and test input |
| `app/qiban_gps_receiver` | Phone GPS HTTP receiver |
| `app/qiban_nav_service` | Destination resolution and navigation state |
| `app/qiban_map_service` | Online maps, offline tiles, and road graph staging |
| `app/qiban_ui` | Native LVGL dashboard and offline map UI |
| `app/qiban_voice_service` | Intent bridge and ASR/TTS job exchange |
| `app/qiban_ai_agent` | Rule-based skills and action command files |
| `app/qiban_music_service` | Local/Wi-Fi PCM playback; Bluetooth remains a stub |
| `app/qiban_video_service` | CedarX-backed local video path when configured |
| `app/qiban_weather_service` | Open-Meteo state producer |
| `app/qiban_ota_service` | Application/firmware update workflow |
| `server` | FastAPI ASR/TTS, telemetry, and OTA relay |

## Completion vocabulary

- **Source implemented**: code and build metadata exist.
- **Configured**: the Kconfig symbol is enabled in the target configuration.
- **Built**: the entry point appears in the current build output or map.
- **Board verified**: a recorded device command demonstrates the behavior.

Never collapse these states into a single "completed" label.
