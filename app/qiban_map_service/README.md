# qiban_map_service

`qiban_map_service` 是 `骑伴 AI 智能电动车中控屏` 的板端地图缓存服务。

它负责两类事情：

- 通过 `Wi-Fi + HTTP` 下载 PNG 地图图片到 `/data/qiban_map/current.png`
- 或把本地已有 PNG 发布到同一路径，供 `qiban_ui` 的地图图片区显示
- 通过板端联网下载离线瓦片到 `/data/qiban_offline_map/map/`
- 通过板端联网下载离线路网到 `/data/qiban_offline_map/nav/road_graph.bin`
- 或把板端本地已有的 `road_graph.bin` 复制到同一路径

同时它会维护 `/data/qiban_map/state.json`，让 `qiban_ui` 能读取地图标题、
状态文本和图片路径。

示例：

```bash
qiban_map_service amap 112.938814 28.228209 17 "Changsha Center"
qiban_map_service fetch http://example.com/map.png "Campus Route"
qiban_map_service publish /data/demo_map.png "Demo Route"
qiban_map_service offline-graph http://<server_ip>/road_graph.bin
qiban_map_service offline-graph /data/qiban_offline_map/nav/inbox/road_graph.bin
qiban_map_service offline-maptiler <maptiler_key>
qiban_map_service offline-maptiler-area <maptiler_key> 112.88 28.17 113.02 28.31 8 17
qiban_map_service status
qiban_map_service clear
```

使用高德在线地图前，在板端环境中设置 `QIBAN_AMAP_KEY`。`amap` 子命令会按
`经度 纬度 缩放级别` 拉取静态 PNG，并写入 `/data/qiban_map/current.png`。

```bash
set QIBAN_AMAP_KEY <your-amap-key>
```

如果 `SD` 卡在 2026-09-02 这次联调中仍无法识别，可直接走板端离线下载：

- `offline-graph <bin_url_or_path>`：下载或复制 `road_graph.bin`
- `offline-maptiler <key>`：下载当前长沙默认区域 `z8-z18` 的离线瓦片
- `offline-maptiler-area ...`：下载自定义经纬度范围的离线瓦片

如果串口/`nsh` 会把长 URL 拆断，优先使用本地路径：

```bash
adb push road_graph.bin /data/qiban_offline_map/nav/inbox/road_graph.bin
qiban_map_service offline-graph /data/qiban_offline_map/nav/inbox/road_graph.bin
```

当前 `qiban_ui` 已改为默认从 `/data/qiban_offline_map/` 读取离线资源，不再依赖
`L:/sdcard/...`。

注意：如果地图页默认区域切到长沙，离线路径规划也需要配套的长沙 `road_graph.bin`。
仓库自带的示例路网若不是长沙区域，只能用于显示流程验证，不能保证本地规划正确。
