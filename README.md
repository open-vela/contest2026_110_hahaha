
# 骑伴 AI 智能电动车中控屏

## 一、作品简介

`骑伴 AI 智能电动车中控屏` 面向电动车及两轮出行用户，基于 `Gemini-S1 (R528) + openvela + ai_agent` 打造一款 AI 智能中控显示终端，集成骑行仪表、导航提示、语音入口与主动安全提醒能力，解决传统车载仪表只能被动显示、缺乏智能交互和主动服务的问题。

当前版本以 `R528S3-Gemini-S1 + 2.8 寸 SPI 屏` 为目标硬件，已经完成板端状态服务和原生 `LVGL` 仪表盘的第一轮打通，并逐步接入车辆传感器、手机协同定位和云端 AI 服务。

当前仓库同时保留两条界面路径：

- `quickapp/qiban_ai_dashboard/`：第一阶段快应用原型层，用于早期页面验证
- `app/qiban_ui/`：当前板端默认启动的原生 `LVGL` 中控界面

最终交付仍将包含板端固件、硬件数据采集、`ai_agent`、自定义 Skill 与板载界面联动，而不仅是一套软件页面。

## 二、选题方向

本项目作品方向明确归类为 `AI 硬件产品创新`，当前第一阶段采用 `快应用` 作为前端实现形态。

这样做的原因是：

- 比赛交付和部署流程可以直接复用 openvela 快应用工具链
- 作品核心价值来自 `Gemini-S1` 板端运行、显示、语音入口和主动提醒能力
- 后续可以继续扩展到导航、日志总结、自定义 Skill 和 Agent 主动执行

## 三、目录结构

- `quickapp/qiban_ai_dashboard/`：骑伴 AI 中控快应用源码工程
- `app/qiban_ui/`：板端原生 `LVGL` 中控界面
- `app/qiban_vehicle_service/`：板端车辆状态服务骨架，用于模拟或接入真实骑行数据
- `app/qiban_nav_service/`：板端导航入口服务，用于命令行下发目的地并生成导航状态
- `app/qiban_voice_service/`：板端语音意图桥接骨架，用于把语音文本意图转发到导航与地图服务
- `app/qiban_ai_agent/`：板端规则 Agent 与 10 个骑行场景 Skill
- `app/qiban_gps_receiver/`：手机定位 HTTP 接收服务
- `app/qiban_music_service/`：本地/Wi-Fi 音乐播放服务
- `app/qiban_ota_service/`：应用与固件 OTA 服务
- `app/qiban_video_service/`：本地视频播放服务
- `app/qiban_weather_service/`：天气状态服务
- `app/qiban_wifi_bridge/`：板端与主机之间的 Wi-Fi 语音桥
- `server/`：异网联调用的主机侧中转服务骨架，已补 `Docker Compose + Caddy` 公网部署方案
- `board/contest_board/`：模板自带板级适配示例，当前保留作参考
- `shared/`：板端与前端对齐使用的状态协议与示例数据
- `logs/`：AI Coding 日志归档目录
- `skills/qiban-openvela-workflow/`：从项目开发中沉淀的构建、联调与交付 Skill
- `submission/`：按赛事模板整理的技术报告与辅助材料
- `contest2026_110_hahaha.xml`：比赛仓与 openvela 编译树的映射关系

当前目录通过 manifest 映射到：

- `packages/demos/contest2026_110_qiban_map_service`
- `packages/demos/contest2026_110_qiban_music_service`
- `packages/demos/contest2026_110_qiban_nav_service`
- `packages/demos/contest2026_110_qiban_ota_service`
- `packages/demos/contest2026_110_qiban_sensor_bridge`
- `packages/demos/contest2026_110_qiban_ui`
- `packages/demos/contest2026_110_qiban_vehicle_service`
- `packages/demos/contest2026_110_qiban_video_service`
- `packages/demos/contest2026_110_qiban_voice_service`
- `packages/demos/contest2026_110_qiban_weather_service`
- `packages/demos/contest2026_110_qiban_wifi_bridge`
- `packages/demos/contest2026_110_qiban_gps_receiver`
- `packages/demos/contest2026_110_qiban_ai_agent`
- `packages/apps/contest2026_110_qiban_ai_dashboard`

## 四、当前实现范围

当前板端 MVP 聚焦以下能力：

1. 仪表盘主页
2. 速度 / 电量 / 里程显示
3. 导航文本提示
4. 全屏地图独立页
5. 语音意图、录放音与 ASR/TTS 中转入口
6. 超速 / 低电量主动提醒
7. 语音意图到导航服务桥接
8. 天气、音乐和本地视频页面
9. 板端规则 Agent 与 10 个骑行场景 Skill 源码

当前未完成或未完成真机验收：

1. 真实整车 UART/BMS/GNSS 协议接入
2. Agent、GPS、Wi-Fi、OTA 和视频模块的默认固件启用与整链路验收
3. `/data/agent/skills/` 运行时 Skill 形态
4. release.rpk、长稳、功耗、时延与准确率测试

## 五、运行方式

### 1. 拉取完整工程

以比赛仓为入口初始化工作区：

```bash
repo init -u https://github.com/open-vela/contest2026_110_hahaha \
  -b dev-ai-contest-2026 -m contest2026_110_hahaha.xml
repo sync -c -j8
```

### 2. 编译 openvela 基线镜像

本项目当前使用 `R528S3-Gemini-S1` 的 `nsh_minidisplay` 配置，适配 `2.8 寸 SPI 屏`：

干净同步后的 openvela 工作区先启用本项目的板端 demo 和开机启动链路：

```bash
python3 contest2026_110_hahaha/tools/openvela/enable_r528_qiban_mvp.py --openvela-root .
```

```bash
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ distclean -j8
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ -j8
```

如需手动确认配置，可执行：

```bash
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ menuconfig
```

关键配置项应包含：

```text
LVX_USE_DEMO_CONTEST2026_110_QIBAN_MAP_SERVICE
LVX_USE_DEMO_CONTEST2026_110_QIBAN_NAV_SERVICE
LVX_USE_DEMO_CONTEST2026_110_QIBAN_SENSOR_BRIDGE
LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI
LVX_USE_DEMO_CONTEST2026_110_QIBAN_VEHICLE_SERVICE
LVX_USE_DEMO_CONTEST2026_110_QIBAN_VOICE_SERVICE
LV_FONT_SIMSUN_16_CJK
LV_USE_IME_PINYIN
NETINIT_NOMAC
NETINIT_SWMAC
DRIVERS_GPADC_CTL_NUM=1
```

蓝牙用户态入口当前不启用 `BT_START`：本仓库缺少 `bt_start` 依赖的蓝牙头文件/组件，强制启用会导致 `bluetooth.h` 编译失败。启动脚本已改为仅在 `CONFIG_BT_START` 存在时调用 `bt_start`，避免继续启动不存在的 `bluetoothd`。

### 3. 构建快应用原型

进入快应用工程目录：

```bash
cd contest2026_110_hahaha/quickapp/qiban_ai_dashboard
npm install
npm run prepare:release-sign
npm run release
```

最终提交使用 `release.rpk`，不是 `debug.rpk`。

### 4. 原生板端界面启动

当前默认启动链路已经切到板端原生界面：

1. 外部采集器可选地通过 `qiban_sensor_bridge` 写入 `/data/qiban_inputs/`
2. `qiban_map_service` 可选地通过 `Wi-Fi + HTTP` 下载 PNG 地图到 `/data/qiban_map/`，同时维护地图中心点、缩放和路线状态
3. `qiban_nav_service` 可选地写入 `/data/qiban_nav_state.json` 并触发路线图同步
4. `rcS.nsh` 启动 `qiban_vehicle_service serve`
5. `rcS.nsh` 启动 `qiban_ui`
6. `qiban_vehicle_service` 聚合模拟值、`/data/qiban_inputs/` 和可选 `GPADC`
7. `qiban_ui` 周期读取 `/data/qiban_vehicle_state.json`、`/data/qiban_map/state.json` 与 `/data/qiban_nav_state.json`
8. `qiban_ui` 读取 `/data/qiban_location_state.json`，在地图页用离线地图车标显示骑手当前位置
9. `qiban_ui` 地图页默认读取 `/data/qiban_offline_map/map/<zoom>/<x>/<y>/tile.png` 和 `/data/qiban_offline_map/nav/road_graph.bin`，支持长按选终点后本地离线路径规划
10. `qiban_ui` 提供仪表、离线地图、语音、天气、音乐和本地视频六个页面
11. 串口仍可手动执行调试命令：

```bash
qiban_sensor_bridge serve /dev/ttyS1
qiban_sensor_bridge write speed_kmh 26
qiban_sensor_bridge location 116.481485 39.990464
qiban_sensor_bridge location 116.488000 39.988800
qiban_sensor_bridge location 116.498000 39.986300
qiban_sensor_bridge location 116.508789 39.984674
qiban_map_service amap 116.481485 39.990464 15 "Campus Route"
qiban_map_service fetch http://example.com/map.png "Campus Route"
qiban_map_service publish /data/demo_map.png "Demo Route"
qiban_map_service offline-graph http://<server_ip>/road_graph.bin
qiban_map_service offline-maptiler <maptiler_key>
qiban_map_service offline-maptiler-area <maptiler_key> 114.06 32.11 114.11 32.17 16 17
qiban_nav_service list
qiban_nav_service start 软件园二期
qiban_nav_service start 北京大学东门
qiban_nav_service start 任意目的地 116.520481 39.986412
qiban_nav_service status
qiban_voice_service examples
qiban_voice_service aliases
qiban_voice_service intent 导航到清华大学
qiban_voice_service intent 取消导航
qiban_map_service status
qiban_vehicle_service once
qiban_vehicle_service monitor
qiban_vehicle_service export /data/qiban_vehicle_state.json
qiban_vehicle_service serve
qiban_ui
```

如果需要保留快应用路线调试，再走下面的部署流程。

### 5. 快应用部署到开发板

1. 解压 `release.rpk`
2. 将应用目录复制到 `vendor/allwinnertech/lichee/board/common/data/UDISK/app/`
3. 将中文字体复制到 `vendor/allwinnertech/lichee/board/common/data/UDISK/font/`
4. 进入打包环境：

```bash
cd vendor/allwinnertech/lichee/
source envsetup.sh
lunch_nuttx
# 选择 2: r528s3-gemini-s1
pack
```

5. 烧录镜像后进入 `nsh`
6. 启动应用：

```bash
qiban_vehicle_service once
qiban_vehicle_service monitor
qiban_vehicle_service export /data/qiban_vehicle_state.json
qiban_vehicle_service serve
vapp hap://app/com.openvela.contest2026.team110.qibanai
```

建议的调试顺序是：

1. 先在 `nsh` 中运行 `qiban_vehicle_service`，确认板端状态服务能输出数据
2. 再启动快应用界面
3. 最后再把板端状态服务接到 `ai_agent` 和自定义 Skill

当前 `qiban_voice_service` 已提供最小桥接骨架，可先用文本模拟语音识别结果，
将“导航到 / 取消导航 / 放大地图 / 缩小地图 / 刷新地图”等意图转发给现有
`qiban_nav_service` 与 `qiban_map_service`。

当前快应用会优先读取板端导出的 `/data/qiban_vehicle_state.json`；如果当前环境暂时读不到该文件，则自动回退到内置模拟状态，便于界面联调不中断。

建议真机联调时优先使用 `qiban_vehicle_service serve`，让板端周期性刷新状态文件，快应用才能持续读到实时变化。
当前 `qiban_vehicle_service` 会优先尝试读取 `/dev/adc0` 作为电量输入；如果 ADC 设备尚未就绪，则自动回退到模拟电量。
当前 `qiban_vehicle_service` 还支持从 `/data/qiban_inputs/` 读取外部采集器写入的整数值，便于后续把 UART / BMS / GNSS 解析程序无缝接入现有状态流。
当前 `qiban_sensor_bridge` 已经提供第一版桥接程序，可读取 `key=value` 文本流并原子写入这些输入文件。
当前 `qiban_map_service` 已经提供第一版地图服务，可通过 `curl` 下载 PNG，
或把本地 PNG 发布到 `/data/qiban_map/current.png` 供 `qiban_ui` 显示。

### 6. 板端音频自检

仓库已补一套板端音频排查脚本，位置在 [`tools/audio/`](/home/xjx/contest2026_110_hahaha/tools/audio)。

板端最小排查顺序：

```bash
sh /data/board_audio_probe.nsh
nxplayer
nxrecorder
```

其中：

- [`board_audio_probe.nsh`](/home/xjx/contest2026_110_hahaha/tools/audio/board_audio_probe.nsh)：列设备节点、音频工具、mixer 和建议命令
- [`nxplayer_tone_1k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxplayer_tone_1k.cmd)：喇叭蜂鸣测试
- [`nxrecorder_mic_16k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxrecorder_mic_16k.cmd)：16k 单声道原始录音
- [`nxplayer_play_16k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxplayer_play_16k.cmd)：回放刚录下来的 PCM

详细步骤见 [`tools/audio/README.md`](/home/xjx/contest2026_110_hahaha/tools/audio/README.md)。

当前板端数据流已经明确为：

```text
UART / 外部采集器
  -> qiban_sensor_bridge
  -> /data/qiban_inputs/*
  -> qiban_vehicle_service
  -> /data/qiban_vehicle_state.json

GNSS / 手机定位 / 测试数据
  -> qiban_sensor_bridge location
  -> /data/qiban_location_state.json

Wi-Fi / HTTP PNG
  -> qiban_map_service
  -> /data/qiban_map/current.png + /data/qiban_map/state.json

CLI / 后续语音意图
  -> qiban_nav_service
  -> /data/qiban_nav_request.json + /data/qiban_nav_state.json
  -> qiban_map_service route ...

  -> qiban_ui / qiban_ai_dashboard
```

## 六、硬件与技术方案

### 目标硬件

- 开发板：`润芯微 Gemini-S1`
- SoC：`全志 R528 双核 Cortex-A7`
- 当前屏幕：`2.8 寸 SPI 屏`
- 预留扩展：`Wi-Fi / 蓝牙 / 麦克风 / UART / GPIO / ADC / PCM`

### 技术栈

- 硬件底座：`Gemini-S1 (R528) + openvela`
- 设备侧数据采集：`GPIO / ADC / UART / SPI`
- Agent 能力：`ai_agent + 自定义 Skill`
- UI：第一阶段 `QuickApp`，后续可扩展 `LVGL` 原生界面
- 语音与智能体：`ai_agent + 云端 ASR / LLM / TTS`
- 提醒逻辑：`本地规则引擎 + 自定义 Skill`

### 系统分层

为保证最终作品是“烧录到硬件里并在板端主动运行”的 AI 硬件产品，系统按以下层次实现：

1. **固件与板级层**
   - 使用 `nsh_minidisplay` 编译 openvela 固件
   - 通过 `pack` 打包并烧录到 `Gemini-S1`
   - 根据需要补充板级配置、外设初始化和驱动适配

2. **设备能力层**
   - 采集车速、电量、里程、GNSS 或手机协同定位数据
   - 通过 `GPIO / ADC / UART` 接入传感器或整车控制器
   - 将数据提供给本地规则和 Agent 使用

3. **Agent 与 Skill 层**
   - 编译并运行 `ai_agent`
   - 在 `/data/agent/skills/` 下提供自定义 Skill
   - 实现超速、低电量、疲劳骑行等“主动+执行”场景

4. **交互展示层**
   - 当前阶段先用快应用实现中控界面
   - 后续可根据真机性能和交互需求切换或补充 LVGL 原生页面

## 七、AI Coding 使用说明

本项目使用 AI 辅助完成以下工作：

- 拆解比赛要求、提交要求和仓库结构
- 对齐 `R528S3-Gemini-S1 + nsh_minidisplay + 2.8 寸 SPI 屏` 的板级流程
- 生成快应用 MVP 工程骨架
- 规划项目分阶段功能与实现路线
- 记录环境问题、编译问题与修复过程

16 个项目相关 Codex 会话已经由 `tools/ai_logs/export_codex_sessions.py` 从原始 rollout 导出到 `logs/xjx/`，清单和 Token 统计见 `logs/xjx/manifest.json`。项目同时沉淀了 `skills/qiban-openvela-workflow/`，用于复用 R528 构建、板端联调、离线地图部署和赛事交付审计流程。

## 八、当前仓库状态

当前仓库已经完成：

1. 比赛模板 README 替换为项目 README
2. `hello_quickapp` 替换为 `qiban_ai_dashboard`
3. `hello_app` 升级为板端 `qiban_vehicle_service`
4. 建立了骑伴 AI 中控快应用的多页面 MVP 骨架
5. 将首页、导航页、语音页统一到同一份车辆状态模拟模型
6. 实现板端 `qiban_vehicle_service`，可输出模拟/ADC 混合状态并导出 JSON
7. 新增板端原生 `qiban_ui`，开机后可直接接管屏幕显示中控数据
8. 将 `nsh_minidisplay` 默认启动链路从 `lvgldemo` 切换为 `qiban_ui`
9. 新增 `qiban_sensor_bridge`，把外部串口/文本 telemetry 接到 `/data/qiban_inputs/`
10. 补充 `shared/qiban_vehicle_state.schema.json`，将板端导出与界面消费统一到同一份状态协议
11. 明确了作品最终是“固件 + 硬件采集 + ai_agent + Skill + 板端界面”的组合架构

提交版本的具体完成度、已验证证据和未完成项见 `submission/技术报告.pdf` 与 `submission/提交检查表.md`。
