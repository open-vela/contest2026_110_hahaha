# qiban_ui

`qiban_ui` 是 `骑伴 AI 智能电动车中控屏` 的原生 OpenVela LVGL 应用。

它直接编进固件，在板端启动后周期读取
`/data/qiban_vehicle_state.json`，将速度、电量、续航、里程、导航和告警
渲染到屏幕上。

当前版本还会读取 `/data/qiban_map/state.json`，并采用五页面结构：

- 第 1 页：仪表参数页
- 第 2 页：全屏地图页
- 第 3 页：语音控制页
- 第 4 页：日期天气页
- 第 5 页：音乐播放页

三个页面支持左右滑动切换，也支持右上角箭头按钮切页。地图页现在优先使用
`LVGL-Offline-Map` 离线瓦片控件，默认从 `/data/qiban_offline_map/map/<zoom>/<x>/<y>/tile.png`
读取瓦片；离线路网规划默认读取 `/data/qiban_offline_map/nav/road_graph.bin`。
`/data/qiban_map/state.json` 继续用于驱动地图标题、中心点、缩放级别
和外部路线起终点。
仓库内已经带了一份可直接复用的示例 `road_graph.bin`，位于
`third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_demo/map_demo/assets/nav/road_graph.bin`。
离线瓦片目录当前仓库未内置，但如果板子联网，可直接在板端执行
`qiban_map_service offline-maptiler <key>` 下载长沙默认区域瓦片到 `/data/qiban_offline_map/map/`。

导航页还会读取 `/data/qiban_location_state.json`。当导航状态 active 时，
`qiban_ui` 会把当前位置经纬度同步到离线地图车标，并把导航起点 / 终点渲染为
轨迹线。地图页支持长按地图选终点，点击“确认路线”后直接用本地
`lv_offline_nav` 做离线路径规划；本地规划激活后，优先显示本地路线和转向提示。
旧的 `/data/qiban_map/current.png` 可继续由 `qiban_map_service` 生成，但地图页本身
不再依赖 PNG 是否下载成功。
没有接 GNSS 时，可用下面的命令模拟移动：

```bash
qiban_nav_service start 岳麓山
qiban_sensor_bridge location 112.938814 28.228209
qiban_sensor_bridge location 112.944000 28.226500
qiban_sensor_bridge location 112.952000 28.223800
qiban_sensor_bridge location 112.960500 28.220600
```

第 3 页当前提供：

- 默认只显示两个图标入口：键盘图标、麦克风图标
- 点击键盘图标后，展开 `lv_ime_pinyin` 拼音输入法和大键盘
- 在键盘里输入中文目的地后，按回车会直接触发
  `qiban_nav_service start <目的地>`
- 点击麦克风图标后，触发 `qiban_voice_service record 4`
- 键盘收起状态下，保留常用目的地快捷按钮，如 `清华大学`、`北京大学东门`、`软件园二期`

说明：

- 输入目的地时，既可以直接输入 `清华大学`，也可以输入 `导航到清华大学`
- 键盘和候选栏默认隐藏，只有点击键盘图标后才会展开
- 回车导航会自动剥离 `导航到 / 带我去 / 去` 这类前缀，再调用导航服务
- 若固件配置已打开 `CONFIG_LV_USE_IME_PINYIN=y` 和 `CONFIG_LV_FONT_SIMSUN_16_CJK=y`，
  第 3 页会显示拼音候选栏并正常渲染中文
- 语音入口当前负责触发录音链路，后续导航仍依赖已有 ASR / intent 桥接

第 5 页是音乐播放器界面，支持三种音源：

- **SD 卡**：本地音乐文件播放
- **WiFi**：网络流媒体播放
- **蓝牙**：蓝牙音频连接

音乐状态由 `/data/qiban_music_state.json` 驱动，格式参见
`shared/qiban_music_state.schema.json`。UI 显示歌曲标题、艺术家、
进度条、播放/暂停/上下曲控制、音量和音源切换按钮。

示例状态文件：

```json
{
  "title": "夜曲",
  "artist": "周杰伦",
  "source": "sdcard",
  "playing": 1,
  "duration_sec": 235,
  "position_sec": 45,
  "volume": 60,
  "track_index": 0,
  "track_count": 10
}
```

建议与 `qiban_vehicle_service serve` 一起使用，由后者持续刷新状态文件。
