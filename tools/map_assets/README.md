# map_assets

用于给 `app/qiban_ui` 准备离线地图资产。

从 2026-09-02 这次联调开始，`qiban_ui` 默认离线资源路径已经切到
`/data/qiban_offline_map/`，不再强依赖 `SD` 卡。

当前仓库已自带：

- `road_graph.bin`
  位置：
  `third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_demo/map_demo/assets/nav/road_graph.bin`
- 对应覆盖范围：
  `113.6788629,31.3779464` 到 `116.1567956,32.7086657`

当前仓库未自带：

- 离线瓦片目录 `map/<zoom>/<x>/<y>/tile.png`

## 1. 下载示例瓦片

Linux / macOS 可用：

```bash
export MAPTILER_KEY=你的_key
python3 /home/xjx/contest2026_110_hahaha/tools/map_assets/download_maptiler_tiles.py \
  --output-root /tmp/qiban_tiles
```

默认会下载一小块长沙区域的 `z8` 到 `z18` 瓦片，目录结构已经是：

```text
/tmp/qiban_tiles/<zoom>/<x>/<y>/tile.png
```

如果你要扩大范围，可显式传：

```bash
python3 /home/xjx/contest2026_110_hahaha/tools/map_assets/download_maptiler_tiles.py \
  --output-root /tmp/qiban_tiles \
  --min-lon 113.9 --min-lat 31.9 \
  --max-lon 114.3 --max-lat 32.3 \
  --zoom 15 16 17 18
```

## 2. 整理到 SD 卡目录

```bash
bash /home/xjx/contest2026_110_hahaha/tools/map_assets/stage_offline_assets.sh \
  --sdcard-root /你的SD卡挂载点 \
  --tile-root /tmp/qiban_tiles
```

执行后目标目录应为：

```text
/你的SD卡挂载点/map/<zoom>/<x>/<y>/tile.png
/你的SD卡挂载点/nav/road_graph.bin
```

## 3. 直接在板端下载

如果板子联网但 `SD` 卡不可用，可直接在 `nsh` 里执行：

先在宿主机启动一个最小 HTTP 服务提供 `road_graph.bin`：

```bash
bash /home/xjx/contest2026_110_hahaha/tools/map_assets/serve_road_graph.sh 18080
```

然后在板子上执行：

```bash
qiban_map_service offline-graph http://<server_ip>/road_graph.bin
qiban_map_service offline-maptiler <maptiler_key>
```

离线资源会落到：

```text
/data/qiban_offline_map/map/<zoom>/<x>/<y>/tile.png
/data/qiban_offline_map/nav/road_graph.bin
```

如果板子和宿主机不在同一网段，或者 `nsh` 会把长 URL 拆成两条命令，
优先走本地文件路径：

```bash
adb push road_graph.bin /data/qiban_offline_map/nav/inbox/road_graph.bin
qiban_map_service offline-graph /data/qiban_offline_map/nav/inbox/road_graph.bin
```

从 2026-09-02 起，`qiban_map_service offline-graph` 已支持直接传板端本地路径，
不必强制走 HTTP。

## 4. 当前 qiban_ui 默认区域

`qiban_ui` 现在默认中心点已调整到长沙中心区：

- 经度：`112.9388140`
- 纬度：`28.2282090`
- 缩放：`17`

默认下载脚本和 `qiban_map_service offline-maptiler` 的默认区域也已经同步切到长沙，
默认缩放层级为 `z8` 到 `z18`。
如果你要验证离线路径规划，还需要把 `nav/road_graph.bin` 换成长沙对应区域生成的版本。

## 5. 如果以后换成你自己的城市

需要同时替换两类数据：

1. `map/<zoom>/<x>/<y>/tile.png`
2. `nav/road_graph.bin`

`road_graph.bin` 可用上游脚本生成：

```bash
python3 /home/xjx/contest2026_110_hahaha/third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_demo/map_demo/tools/convert_osm_roads_to_graph.py \
  --input 你的区域.osm.pbf \
  --output /tmp/road_graph.bin \
  --meta /tmp/road_graph.json \
  --osmium "osmium"
```

地图和路网必须是同一地理区域，否则地图能显示，但离线路径规划会失败。
