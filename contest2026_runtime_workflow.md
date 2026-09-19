# Contest2026 项目运行时工作流程详解

> 生成时间：2026-08-05
> 项目：骑伴 AI 智能电动车中控屏（Team 110）
> 源码路径：`/home/xjx/contest2026_110_hahaha/`

---

## 目录

- [一、系统启动流程](#一系统启动流程)
- [二、qiban_vehicle_service 工作流程](#二qiban_vehicle_service-工作流程)
- [三、qiban_ui 工作流程](#三qiban_ui-工作流程)
- [四、用户发起导航流程](#四用户发起导航流程)
- [五、qiban_nav_service 执行流程](#五qiban_nav_service-执行流程)
- [六、qiban_map_service 执行流程](#六qiban_map_service-执行流程)
- [七、qiban_sensor_bridge 工作流程](#七qiban_sensor_bridge-工作流程)
- [八、qiban_voice_service 工作流程](#八qiban_voice_service-工作流程)
- [九、数据闭环总览](#九数据闭环总览)
- [十、文件系统总线协议](#十文件系统总线协议)
- [十一、关键设计模式](#十一关键设计模式)

---

## 一、系统启动流程

### 1.1 硬件上电

```
电源开启
  → Allwinner R528 双核 Cortex-A7 初始化
  → NuttX 内核启动 (nx_start)
  → 初始化调度器、内存管理、文件系统、设备驱动、网络协议栈
  → 挂载 /data 目录（YAFFS on NAND Flash）
  → 执行 /etc/init.d/rcS.nsh 启动脚本
```

### 1.2 自动启动服务

`enable_r528_qiban_mvp.py` 脚本往 `rcS.nsh` 注入了两条自动启动命令：

```bash
# /etc/init.d/rcS.nsh 中的关键部分
qiban_vehicle_service serve &   # 后台启动车辆遥测服务（数据生产者）
qiban_ui &                      # 后台启动 LVGL 仪表盘 UI（数据消费者）
```

这就是**一切的起点**——板子一上电，两个核心进程自动拉起，形成数据生产-消费闭环。

### 1.3 启动时序

```
T+0s    NuttX 内核完成初始化
T+1s    rcS.nsh 开始执行
T+2s    qiban_vehicle_service serve & 启动
        → 创建 /data/ 目录
        → 初始化默认状态 (speed=18, battery=78%)
        → 开始每秒写入 /data/qiban_vehicle_state.json
T+3s    qiban_ui & 启动
        → 初始化 LVGL 图形库
        → 初始化 LCD 帧缓冲 (/dev/lcd0)
        → 初始化触摸屏输入 (/dev/input0)
        → 创建 3 页 Tileview UI
        → 注册 1 秒定时器
        → 进入 LVGL 主循环
T+4s    UI 首次定时器触发
        → 读取 /data/qiban_vehicle_state.json
        → 仪表盘显示实时数据
```

---

## 二、qiban_vehicle_service 工作流程

**源码**：`app/qiban_vehicle_service/qiban_vehicle_service_main.c`（661 行）

### 2.1 启动入口

```
main(argc, argv)
  │
  ├── 命令: "once"     → 单次采集并打印
  ├── 命令: "monitor"  → 多次采集并打印（默认 10 次，1s 间隔）
  ├── 命令: "export"   → 单次采集并导出到文件
  └── 命令: "serve"    → 无限循环采集并持续写入文件 ← 启动脚本用的就是这个
```

### 2.2 serve 模式执行流程

```
main("serve")
  → qiban_state_init()              // 初始化默认状态
  → qiban_run_export_loop(state, "/data/qiban_vehicle_state.json", 1000ms, -1)
      │                              // -1 表示无限循环
      └── while(true) { ... }       // 永不停止
```

### 2.3 每轮循环详细流程（每 1000ms）

```
qiban_state_step(state, tick)
│
├── 步骤 1：生成 Mock 数据（基底值）
│     speed_kmh = speed_pattern[tick % 8]
│                   // speed_pattern = {18, 22, 27, 24, 31, 26, 19, 21}
│                   // 模拟电动车骑行中的速度波动
│     ride_duration_min += 1          // 每轮 +1 分钟
│     total_distance_km_x10 += speed / 12  // 累计里程
│     nav_remaining_m -= speed * 8    // 导航距离递减
│     if (tick % 4 == 0) battery -= 1 // 每 4 秒掉 1% 电量
│     remaining_range_km = battery / 2
│
├── 步骤 2：尝试读取文件输入（覆盖 Mock 值）
│     qiban_apply_file_inputs()
│       ├── 读 /data/qiban_inputs/speed_kmh         → 覆盖速度
│       ├── 读 /data/qiban_inputs/battery_percent    → 覆盖电量
│       ├── 读 /data/qiban_inputs/nav_remaining_m    → 覆盖导航距离
│       ├── 读 /data/qiban_inputs/ride_duration_min  → 覆盖骑行时长
│       └── 读 /data/qiban_inputs/total_distance_km_x10 → 覆盖里程
│     如果有任何文件读取成功 → provider_flags |= FILE_INPUT
│
├── 步骤 3：若电量文件不存在，尝试 GPADC 硬件读取
│     qiban_apply_gpadc_battery()
│       → open("/dev/adc0")
│       → ioctl(fd, ANIOC_TRIGGER, 0)   // 触发 ADC 采样
│       → read(fd, &sample, sizeof)      // 读取采样值
│       → battery_percent = adc_value * 100 / 4095
│       → provider_flags |= GPADC_INPUT
│
├── 步骤 4：计算告警标志
│     alert_overspeed   = (speed_kmh > 25)        // 超速告警
│     alert_low_battery = (battery_percent <= 20)  // 低电量告警
│     alert_fatigue     = (ride_duration_min >= 45)// 疲劳骑行告警
│
└── 步骤 5：标记数据来源
      if (FILE_INPUT && GPADC) source = "board:file-telemetry+gpadc"
      else if (FILE_INPUT)     source = "board:file-telemetry"
      else if (GPADC)          source = "board:gpadc+builtin_motion"
      else                     source = "mock:qiban_vehicle_service"

qiban_state_export("/data/qiban_vehicle_state.json")
│
├── fopen("/data/qiban_vehicle_state.json.tmp", "w")   // 先写临时文件
├── fprintf() 写入完整 JSON 内容
├── fclose()
└── rename(".tmp" → 正式文件)                          // 原子替换

usleep(1000000)  // 等待 1 秒，进入下一轮
```

### 2.4 输出的 JSON 格式

```json
{
  "schema_version": 1,
  "source": "mock:qiban_vehicle_service",
  "generated_at_epoch_s": 1722835200,
  "speed_kmh": 22,
  "battery_percent": 78,
  "remaining_range_km": 39,
  "ride_duration_min": 5,
  "total_distance_km_x10": 126,
  "nav_remaining_m": 4800,
  "alerts": {
    "overspeed": false,
    "low_battery": false,
    "fatigue": false
  }
}
```

---

## 三、qiban_ui 工作流程

**源码**：`app/qiban_ui/qiban_ui_main.c`（2081 行）

### 3.1 启动入口

```
main(argc, argv)
  │
  ├── 1. 检查 LVGL 是否已初始化
  │     if (lv_is_initialized()) → abort
  │
  ├── 2. 板级初始化（如需要）
  │     boardctl(BOARDIOC_INIT, 0)
  │
  ├── 3. 初始化状态结构体
  │     qiban_state_set_defaults(&ui.state)    // speed=18, battery=78%
  │     qiban_map_set_defaults(&ui.map_state)  // "waiting for map"
  │     qiban_nav_set_defaults(&ui.nav_state)  // "导航待命中"
  │     qiban_location_set_defaults(&ui.location_state)
  │
  ├── 4. 初始化 LVGL 图形库
  │     lv_init()
  │     lv_nuttx_dsc_init(&info)
  │       info.fb_path = "/dev/lcd0"           // LCD 帧缓冲设备
  │       info.input_path = "/dev/input0"      // 触摸屏输入设备
  │     lv_nuttx_init(&info, &result)
  │       → 初始化显示驱动
  │       → 初始化输入设备
  │
  ├── 5. 创建 3 页 UI 布局
  │     qiban_ui_create(&ui)
  │       → 创建 Tileview（3 页可滑动）
  │       → Page 0: 仪表盘（速度圆环 + 指标卡片 + 告警栏）
  │       → Page 1: 地图（PNG 图片 + 位置标记 + 缩放/平移控件）
  │       → Page 2: 语音（录音/键盘按钮 + 快捷目的地 + 输入框 + 拼音键盘）
  │
  ├── 6. 首次刷新 UI
  │     qiban_ui_refresh_labels(&ui)
  │
  ├── 7. 注册 1 秒定时器
  │     lv_timer_create(qiban_ui_timer_cb, 1000ms, &ui)
  │
  └── 8. 进入 LVGL 主循环（永不停止）
        while (1) {
          idle = lv_timer_handler();   // 处理 LVGL 事件、重绘
          usleep(idle * 1000);         // 空闲时休眠
        }
```

### 3.2 三页 UI 布局详解

```
┌────────────────────────────────────────────────────────────────┐
│                         Tileview                               │
│                                                                │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐ │
│  │    Page 0        │  │    Page 1        │  │   Page 2     │ │
│  │    仪表盘        │  │    地图          │  │   语音       │ │
│  │                  │  │                  │  │              │ │
│  │ ┌──────────────┐│  │ ┌──────────────┐│  │ ┌──────────┐ │ │
│  │ │  速度圆环    ││  │ │              ││  │ │ 🎤  ⌨   │ │ │
│  │ │  ┌──────┐   ││  │ │   高德静态   ││  │ │ [录音][键盘]│ │ │
│  │ │  │ 22   │   ││  │ │   地图 PNG   ││  │ ├──────────┤ │ │
│  │ │  │km/h  │   ││  │ │              ││  │ │ 快捷:    │ │ │
│  │ │  └──────┘   ││  │ │   [我] 红点  ││  │ │[清华][北大]│ │ │
│  │ ├──────────────┤│  │ │   位置标记   ││  │ │[软件园]   │ │ │
│  │ │电量 78%      ││  │ │              ││  │ ├──────────┤ │ │
│  │ │续航 39 km    ││  │ ├──────────────┤│  │ │[输入框]   │ │ │
│  │ │骑行 5 min    ││  │ │ [-][+] 缩放  ││  │ │          │ │ │
│  │ │里程 12.6 km  ││  │ │ [^][v] 平移  ││  │ │[拼音键盘] │ │ │
│  │ │导航 4800 m   ││  │ │ [R] 刷新    ││  │ │          │ │ │
│  │ ├──────────────┤│  │ └──────────────┘│  │ └──────────┘ │ │
│  │ │ ⚠ 告警栏    ││  │                  │  │              │ │
│  │ └──────────────┘│  │                  │  │              │ │
│  └──────────────────┘  └──────────────────┘  └──────────────┘ │
│                                                                │
│  [◄ 仪表盘]                          [地图 ►]                  │
└────────────────────────────────────────────────────────────────┘
```

### 3.3 定时器回调 `qiban_ui_timer_cb()` — 核心数据流

**这是整个 UI 的心脏**，每 1000ms 执行一次：

```
qiban_ui_timer_cb(timer)  @ 每 1000ms
│
├── 步骤 1：读取车辆状态
│     qiban_state_load_from_file(&ui->state)
│       → open("/data/qiban_vehicle_state.json")
│       → 读取文件内容到 buffer
│       → 自研轻量 JSON 解析器提取各字段:
│           qiban_json_extract_int(json, "speed_kmh", &state->speed_kmh)
│           qiban_json_extract_int(json, "battery_percent", &state->battery_percent)
│           qiban_json_extract_string(json, "source", state->source)
│           ...
│       → 写入 ui->state 结构体
│     若文件读取失败 → qiban_state_step_mock() 使用本地 Mock 数据
│       speed = {18, 21, 26, 29, 24, 19, 22, 27}[tick % 8]
│
├── 步骤 2：读取地图状态
│     qiban_map_load_from_file(&ui->map_state)
│       → 读 /data/qiban_map/state.json
│       → 提取 title, status, image_path, ready
│
├── 步骤 3：读取导航状态
│     qiban_nav_load_from_file(&ui->nav_state)
│       → 读 /data/qiban_nav_state.json
│       → 提取 destination, status, next_turn
│       → 提取 total_distance_m, remaining_distance_m, eta_minutes
│       → 提取 start/end longitude/latitude
│       → 提取 active 标志
│
├── 步骤 4：读取位置状态
│     qiban_location_load_from_file(&ui->location_state)
│       → 读 /data/qiban_location_state.json
│       → 提取 longitude, latitude, accuracy_m, valid
│
├── 步骤 5：导航状态变化检测
│     if (nav_state.active == true && nav_active_latched == false)
│       → 导航刚激活，自动跳转到地图页 (Page 1)
│       → qiban_ui_set_page(ui, 1, LV_ANIM_ON)
│     nav_active_latched = nav_state.active
│
├── 步骤 6：tick++
│
└── 步骤 7：刷新所有 UI 元素
      qiban_ui_refresh_labels(ui)
        │
        ├── 速度显示:  lv_label_set_text_fmt(speed_label, "%d km/h", state.speed_kmh)
        ├── 电量显示:  lv_label_set_text_fmt(battery_label, "电量    %d%%", state.battery_percent)
        ├── 续航显示:  lv_label_set_text_fmt(range_label, "续航    %d km", state.remaining_range_km)
        ├── 骑行显示:  lv_label_set_text_fmt(ride_label, "骑行    %d min", state.ride_duration_min)
        ├── 里程显示:  lv_label_set_text_fmt(distance_label, "里程    %d.%d km", ...)
        ├── 导航显示:  lv_label_set_text_fmt(nav_label, "导航    %d m", nav_remaining_m)
        ├── 告警更新:  qiban_ui_update_alerts(ui)
        │                → overspeed: 显示 "⚠ 注意：当前速度偏快"
        │                → low_battery: 显示 "⚠ 电量不足，请及时充电"
        │                → fatigue: 显示 "⚠ 骑行时间较长，注意休息"
        │                → 无告警: 隐藏告警面板
        ├── 地图刷新:  qiban_ui_refresh_map(ui)
        │                → 若 map_state.ready && image_path 有效
        │                  → lv_image_set_src(map_page_image, image_path)
        │                  → 显示图片，隐藏 placeholder
        │                → 否则显示 "Waiting for map"
        └── 位置标记:  qiban_ui_refresh_location_marker(ui)
                         → 若 location_state.valid
                           → 计算标记在地图上的位置
                           → 显示红点 + "我" 标签
                         → 否则隐藏标记
```

### 3.4 LVGL 事件回调

UI 上的每个可交互元素都注册了事件回调：

| 控件 | 事件 | 回调函数 | 行为 |
|------|------|----------|------|
| ◄ / ► 按钮 | CLICKED | `qiban_ui_prev/next_page_event_cb` | 切换页面 |
| 缩放 - 按钮 | CLICKED | `qiban_ui_map_zoom_event_cb("out")` | spawn `qiban_map_service zoom out` |
| 缩放 + 按钮 | CLICKED | `qiban_ui_map_zoom_event_cb("in")` | spawn `qiban_map_service zoom in` |
| 平移 ^/v/</> | CLICKED | `qiban_ui_map_pan_event_cb(direction)` | spawn `qiban_map_service pan <dir>` |
| 刷新 R 按钮 | CLICKED | `qiban_ui_map_refresh_event_cb` | spawn `qiban_map_service refresh` |
| 键盘按钮 | CLICKED | `qiban_ui_voice_icon_event_cb("keyboard")` | 显示/隐藏输入面板 |
| 录音按钮 | CLICKED | `qiban_ui_voice_icon_event_cb("record")` | spawn `qiban_voice_service record` |
| 快捷目的地 | CLICKED | `qiban_ui_voice_preset_event_cb("清华大学")` | 触发导航 |
| 键盘确认 | READY | `qiban_ui_voice_keyboard_event_cb` | 触发导航 |

---

## 四、用户发起导航流程

### 4.1 触发场景

**场景 A**：点击快捷按钮（如"清华大学"、"北大东门"、"软件园二期"）

```
用户点击 "清华大学" 按钮
  → LVGL 触发 LV_EVENT_CLICKED
  → qiban_ui_voice_preset_event_cb("清华大学")
  → qiban_ui_start_navigation(ui, "清华大学")
```

**场景 B**：在输入框输入文字后按确认键

```
用户通过拼音键盘输入 "导航到北京大学东门"
  → 按下确认键
  → qiban_ui_voice_keyboard_event_cb (检测到 READY 事件)
  → qiban_ui_start_navigation(ui, "导航到北京大学东门")
```

### 4.2 `qiban_ui_start_navigation()` 详细流程

```
qiban_ui_start_navigation(ui, "清华大学")
│
├── 步骤 1：提取目的地文本
│     qiban_ui_extract_destination("清华大学")
│       → 去除 "导航到"、"帮我导航到" 等前缀
│       → 去除首尾空白
│       → 得到纯净目的地: "清华大学"
│
├── 步骤 2：spawn 导航服务进程
│     argv = { "qiban_nav_service", "start", "清华大学", NULL }
│     posix_spawnp(&pid, "qiban_nav_service", NULL, NULL, argv, NULL)
│       → NuttX 创建新任务
│       → 在新任务中执行 qiban_nav_service_main("start", "清华大学")
│       → 立即返回（非阻塞）
│
├── 步骤 3：关闭语音输入面板
│     qiban_ui_set_voice_input_visible(ui, false)
│       → 隐藏 textarea_panel
│       → 隐藏 keyboard
│
├── 步骤 4：请求地图刷新
│     qiban_ui_request_map_refresh("正在刷新导航地图...")
│       → argv = { "qiban_map_service", "refresh", NULL }
│       → posix_spawnp(&pid, "qiban_map_service", ...)
│       → 显示地图加载状态
│
└── 步骤 5：跳转到地图页
      qiban_ui_set_page(ui, 1, LV_ANIM_ON)
        → lv_tileview_set_tile(tileview, map_tile, LV_ANIM_ON)
        → 带动画滑动到地图页
```

---

## 五、qiban_nav_service 执行流程

**源码**：`app/qiban_nav_service/qiban_nav_service_main.c`（1129 行）

### 5.1 启动入口

```
main(argc, argv)
  │
  ├── 命令: "list"                → 列出所有内置目的地
  ├── 命令: "start <目的地>"      → 启动导航
  ├── 命令: "start <目的地> <lon> <lat>" → 启动导航（指定坐标）
  ├── 命令: "status"              → 显示当前导航状态
  └── 命令: "clear"               → 清除导航
```

### 5.2 start 命令详细流程

```
main("start", "清华大学")
│
├── 步骤 1：初始化默认状态
│     qiban_state_set_defaults(&state)
│       → start_lon/lat = (116.481485, 39.990464)  // 默认起点（北京中关村）
│
├── 步骤 2：设置导航激活状态
│     state.active = true
│     state.destination = "清华大学"
│     state.status = "路线规划中"
│
├── 步骤 3：查找目的地
│     qiban_find_destination("清华大学")
│       → 在 g_qiban_destinations[] 数组中线性搜索
│       → 匹配成功:
│           {
│             name: "清华大学",
│             longitude: 116.326667,
│             latitude: 40.003056,
│             total_distance_m: 6200,
│             eta_minutes: 15,
│             next_turn: "前方 200 米左转进入清华东路"
│           }
│
│     若未找到（如用户输入"中关村"）:
│       → qiban_geocode_destination("中关村", &lon, &lat)
│           → 构建高德地理编码 URL:
│             http://restapi.amap.com/v3/geocode/geo
│               ?key=6dcf54a0b06c091e3764140865088543
│               &city=北京
│               &address=中关村
│           → curl GET 请求
│           → 解析返回 JSON 提取 geocodes[0].location
│           → 拆分经纬度
│       → qiban_estimate_distance_m(start_lon, start_lat, end_lon, end_lat)
│           → 曼哈顿距离估算:
│             dx = abs(end_lon - start_lon) * 90000  // 90km/经度
│             dy = abs(end_lat - start_lat) * 111000  // 111km/纬度
│             distance = dx + dy
│
├── 步骤 4：填充导航状态
│     state.end_longitude = 116.326667
│     state.end_latitude = 40.003056
│     state.total_distance_m = 6200
│     state.remaining_distance_m = 6200
│     state.eta_minutes = 15
│     state.next_turn = "前方 200 米左转进入清华东路"
│     state.status = "路线已同步到地图"
│
├── 步骤 5：写入导航状态文件
│     qiban_write_state(&state)
│       → 原子写入 /data/qiban_nav_state.json
│
├── 步骤 6：写入导航距离到输入文件
│     qiban_write_int_atomic("/data/qiban_inputs/nav_remaining_m", 6200)
│       → vehicle_service 下一轮会读取这个值
│
├── 步骤 7：同步路线到地图
│     posix_spawnp("qiban_map_service", "route",
│                   "116.481485", "39.990464",   // 起点坐标
│                   "116.326667", "40.003056",   // 终点坐标
│                   "14", "清华大学")             // 缩放级别 + 标题
│
├── 步骤 8：写入位置状态
│     qiban_write_location_state(116.481485, 39.990464)
│       → 原子写入 /data/qiban_location_state.json
│
└── 步骤 9：打印结果
      "导航已启动: 清华大学"
      "距离: 6200 米, 预计 15 分钟"
      "下一步: 前方 200 米左转进入清华东路"
```

### 5.3 内置目的地列表

| 目的地 | 经度 | 纬度 | 距离 | ETA | 下一步提示 |
|--------|------|------|------|-----|-----------|
| 软件园二期 | 116.508789 | 39.984674 | 4800m | 12min | 前方 180 米右转进入学院路 |
| 软件园一期 | 116.497233 | 39.987641 | 3500m | 9min | 前方 100 米直行 |
| 中关村壹号 | 116.485122 | 39.983215 | 1200m | 4min | 前方 50 米左转 |
| 清华大学 | 116.326667 | 40.003056 | 6200m | 15min | 前方 200 米左转进入清华东路 |
| 北京大学东门 | 116.312544 | 39.992836 | 5800m | 14min | 前方 150 米右转 |
| 西二旗地铁站 | 116.307822 | 40.052789 | 8900m | 22min | 前方 300 米靠右行驶 |
| 上地地铁站 | 116.312456 | 40.033567 | 7200m | 18min | 前方 200 米左转 |
| 五道口 | 116.338956 | 39.992233 | 4500m | 11min | 前方 100 米直行 |
| 北京南站 | 116.385478 | 39.865234 | 22000m | 55min | 前方 500 米靠左行驶 |

---

## 六、qiban_map_service 执行流程

**源码**：`app/qiban_map_service/qiban_map_service_main.c`（1451 行）

### 6.1 启动入口

```
main(argc, argv)
  │
  ├── 命令: "fetch <url>"                          → 下载任意 URL 图片
  ├── 命令: "amap <lon> <lat> [zoom] [title]"      → 获取高德静态地图
  ├── 命令: "route <start_lon> <start_lat> <end_lon> <end_lat>" → 获取路线地图
  ├── 命令: "pan <direction>"                       → 平移地图
  ├── 命令: "zoom <in|out>"                         → 缩放地图
  ├── 命令: "refresh"                               → 刷新当前地图
  ├── 命令: "publish <path>"                        → 发布地图状态
  ├── 命令: "status"                                → 显示当前状态
  └── 命令: "clear"                                 → 清除地图缓存
```

### 6.2 route 命令详细流程（被 nav_service 调用）

```
main("route", "116.481485", "39.990464", "116.326667", "40.003056", "14", "清华大学")
│
├── 步骤 1：获取渲染锁
│     尝试创建 /data/qiban_map/render.lock（排他文件）
│     若已存在 → 说明另一个 map_service 正在渲染，退出
│
├── 步骤 2：计算地图中心点和参数
│     center_lon = (start_lon + end_lon) / 2 = 116.404
│     center_lat = (start_lat + end_lat) / 2 = 39.997
│     zoom = 14
│
├── 步骤 3：构建高德静态地图 API URL
│     http://restapi.amap.com/v3/staticmap
│       ?key=6dcf54a0b06c091e3764140865088543
│       &size=240*320                          // 适配 2.8" SPI 屏
│       &zoom=14
│       &center=116.404,39.997
│       &markers=mid,0xFF0000,S:116.481485,39.990464    // 起点红标
│       &markers=mid,0x00FF00,E:116.326667,40.003056    // 终点绿标
│       &path=10,0x0000ff,1,0.8:116.481485,39.990464;116.326667,40.003056
│                                                  // 蓝色路线
│
├── 步骤 4：HTTP 下载 PNG 图片
│     curl GET 请求
│       → 写入 /data/qiban_map/current.png
│
├── 步骤 5：保存会话状态
│     /data/qiban_map/session.json
│       {
│         "center_lon": 116.404,
│         "center_lat": 39.997,
│         "zoom": 14,
│         "route_active": true,
│         "route_start_lon": 116.481485,
│         "route_start_lat": 39.990464,
│         "route_end_lon": 116.326667,
│         "route_end_lat": 40.003056,
│         "title": "清华大学"
│       }
│
├── 步骤 6：发布地图状态
│     /data/qiban_map/state.json
│       {
│         "title": "清华大学",
│         "status": "route",
│         "image_path": "/data/qiban_map/current.png",
│         "updated_at": 1722835200,
│         "ready": 1
│       }
│
├── 步骤 7：写入位置信息
│     /data/qiban_location_state.json
│       {
│         "longitude": 116.481485,
│         "latitude": 39.990464,
│         "accuracy_m": 30
│       }
│
└── 步骤 8：释放渲染锁
      unlink("/data/qiban_map/render.lock")
```

### 6.3 pan/zoom 命令流程

```
main("pan", "up")
  → 读取 /data/qiban_map/session.json（当前中心点、缩放级别）
  → 根据 zoom 级别计算偏移量:
      zoom 14: delta = 0.005 度
      zoom 16: delta = 0.001 度
      zoom 18: delta = 0.0003 度
  → center_lat += delta（上移）
  → 更新 session.json
  → 重新请求高德静态地图
  → 更新 state.json

main("zoom", "in")
  → 读取 session.json
  → zoom = min(zoom + 1, 18)
  → 重新请求高德静态地图
  → 更新 session.json 和 state.json
```

---

## 七、qiban_sensor_bridge 工作流程

**源码**：`app/qiban_sensor_bridge/qiban_sensor_bridge_main.c`（523 行）

### 7.1 启动入口

```
main(argc, argv)
  │
  ├── 命令: "serve [device|-]"           → 持续监听 UART/stdin
  ├── 命令: "write <key> <value>"        → 写入单个值
  └── 命令: "location <lon> <lat>"       → 写入位置信息
```

### 7.2 serve 模式流程

```
main("serve", "/dev/ttyS1")
│
├── 步骤 1：确保目录存在
│     mkdir("/data", 0777)
│     mkdir("/data/qiban_inputs", 0777)
│
├── 步骤 2：打开输入设备
│     fd = open("/dev/ttyS1", O_RDONLY)   // UART 串口
│     stream = fdopen(fd, "r")
│     // 或者用 stdin（当参数为 "-" 时）
│
└── 步骤 3：逐行读取处理
      qiban_serve_stream(stream, "/dev/ttyS1")
        while (fgets(line, 256, stream) != NULL) {
          qiban_process_line(line)
        }
```

### 7.3 行处理流程

```
qiban_process_line("speed_kmh=25")
│
├── 步骤 1：去除注释和空白
│     if (line starts with '#') → 跳过
│     qiban_trim(line)
│
├── 步骤 2：拆分键值对
│     separator = strchr(line, '=')
│     key = "speed_kmh"
│     value = "25"
│
├── 步骤 3：查找映射
│     qiban_find_mapping("speed_kmh")
│       → 在 g_qiban_input_mappings[] 中查找
│       → 找到: { key: "speed_kmh", path: "/data/qiban_inputs/speed_kmh",
│                  minimum: 0, maximum: 120 }
│
├── 步骤 4：验证值范围
│     value = 25, minimum = 0, maximum = 120 → 通过
│
└── 步骤 5：原子写入文件
      qiban_write_input_value(mapping, 25)
        → fopen("/data/qiban_inputs/speed_kmh.tmp", "w")
        → fprintf(fp, "25\n")
        → fclose(fp)
        → rename(.tmp → "/data/qiban_inputs/speed_kmh")
        → printf("updated speed_kmh=25 -> /data/qiban_inputs/speed_kmh")
```

### 7.4 支持的输入键值

| 键名 | 文件路径 | 范围 | 说明 |
|------|----------|------|------|
| `speed_kmh` | `/data/qiban_inputs/speed_kmh` | 0-120 | 速度 (km/h) |
| `battery_percent` | `/data/qiban_inputs/battery_percent` | 0-100 | 电量 (%) |
| `nav_remaining_m` | `/data/qiban_inputs/nav_remaining_m` | 0-999999 | 导航剩余距离 (m) |
| `ride_duration_min` | `/data/qiban_inputs/ride_duration_min` | 0-1440 | 骑行时长 (min) |
| `total_distance_km_x10` | `/data/qiban_inputs/total_distance_km_x10` | 0-999999 | 总里程 ×10 |

### 7.5 外部数据输入示例

通过 UART 发送（或 echo 管道）：

```bash
# 从外部设备通过 UART 发送
echo "speed_kmh=25" > /dev/ttyS1
echo "battery_percent=60" > /dev/ttyS1

# 或通过 stdin 管道
echo -e "speed_kmh=25\nbattery_percent=60" | qiban_sensor_bridge serve -

# 或直接写入
qiban_sensor_bridge write speed_kmh 25
qiban_sensor_bridge location 116.481485 39.990464 30
```

---

## 八、qiban_voice_service 工作流程

**源码**：`app/qiban_voice_service/qiban_voice_service_main.c`（1847+ 行）

### 8.1 启动入口

```
main(argc, argv)
  │
  ├── 命令: "intent <text>"        → 解析中文语音意图
  ├── 命令: "examples"             → 显示示例意图
  ├── 命令: "aliases"              → 显示语音别名
  ├── 命令: "listen [seconds]"     → 录音
  ├── 命令: "speak <text>"         → TTS 播报
  ├── 命令: "record [seconds]"     → 录制 PCM
  ├── 命令: "play <pcm>"           → 播放 PCM 文件
  ├── 命令: "import-asr <text>"    → 导入 ASR 结果
  ├── 命令: "import-tts <pcm>"     → 导入 TTS 音频
  ├── 命令: "announce-nav"         → 播报当前导航信息
  ├── 命令: "server-config <url> <device_id>" → 配置中继服务器
  ├── 命令: "submit-asr"           → 提交 ASR 任务到服务器
  ├── 命令: "poll-asr <job_id>"    → 轮询 ASR 结果
  └── 命令: "poll-tts <job_id>"    → 轮询 TTS 结果
```

### 8.2 意图解析流程

```
qiban_voice_service intent "导航到清华大学"
│
├── 步骤 1：预处理文本
│     去除首尾空白
│     转换为小写（用于匹配）
│
├── 步骤 2：匹配意图模式
│     模式列表:
│       "导航到*"          → NAVIGATE
│       "帮我导航到*"      → NAVIGATE
│       "带我去*"          → NAVIGATE
│       "取消导航"         → CANCEL_NAV
│       "停止导航"         → CANCEL_NAV
│       "放大地图"         → ZOOM_IN
│       "缩小地图"         → ZOOM_OUT
│       "刷新地图"         → REFRESH_MAP
│
│     匹配 "导航到*" → 提取目的地 "清华大学"
│
├── 步骤 3：匹配语音别名
│     qiban_find_alias("清华大学")
│       → 在别名表中查找
│       → 返回规范名称和坐标
│
├── 步骤 4：执行意图
│     NAVIGATE → posix_spawnp("qiban_nav_service", "start", "清华大学")
│     CANCEL_NAV → posix_spawnp("qiban_nav_service", "clear")
│     ZOOM_IN → posix_spawnp("qiban_map_service", "zoom", "in")
│     ZOOM_OUT → posix_spawnp("qiban_map_service", "zoom", "out")
│     REFRESH_MAP → posix_spawnp("qiban_map_service", "refresh")
│
└── 步骤 5：输出结果
      "意图: NAVIGATE"
      "目的地: 清华大学"
      "已启动导航服务"
```

### 8.3 ASR/TTS 管线流程

```
[录音阶段]
用户点击 🎤 录音按钮
  → qiban_voice_service record 3
    → 创建 nxrecorder 命令脚本
    → 执行: nxrecorder -t pcm -d /dev/audio-pcm0 -r 16000 -b 16 -c 1 /tmp/qiban_record.pcm
    → 录制 3 秒音频
    → 返回 /tmp/qiban_record.pcm 路径

[ASR 阶段]
qiban_voice_service submit-asr
  → 读取 /tmp/qiban_record.pcm
  → POST /api/asr/jobs (multipart/form-data)
      file: qiban_record.pcm
      metadata: { device_id: "gemini-s1", format: "pcm16k16bit" }
  → 返回 job_id

qiban_voice_service poll-asr <job_id>
  → GET /api/jobs/<job_id>
  → 若 status == "completed":
      → 下载 result-file
      → 得到识别文本: "导航到清华大学"
  → 若 status == "pending":
      → 继续轮询

[意图执行阶段]
qiban_voice_service import-asr "导航到清华大学"
  → 执行意图解析流程（同上）
  → 启动导航

[TTS 播报阶段]
qiban_voice_service announce-nav
  → 读取 /data/qiban_nav_state.json
  → 生成播报文本:
    "导航已启动，目的地清华大学，距离 6200 米，预计 15 分钟到达"
  → POST /api/tts/jobs
      text: "导航已启动，目的地清华大学..."
      voice: "zh-CN-XiaoxiaoNeural"
  → 轮询结果
  → 下载 PCM 文件
  → nxplayer 播放
```

---

## 九、数据闭环总览

### 9.1 系统级数据流图

```
                         ┌─────────────────────────────────────┐
                         │          外部世界                     │
                         │  (UART 传感器 / GPS 模块 / 手机)      │
                         └──────────┬──────────────────────────┘
                                    │ UART / CLI
                                    ↓
                         ┌─────────────────────────────────────┐
                         │       qiban_sensor_bridge            │
                         │       (按需启动 / 常驻监听)           │
                         │                                      │
                         │  解析 key=value 行                   │
                         │  验证范围 → 原子写入文件               │
                         └──────────┬──────────────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ↓               ↓               ↓
         /data/qiban_inputs/   /data/qiban_     /data/qiban_
         speed_kmh             location_        location_
         battery_percent       state.json       state.json
         nav_remaining_m
         ride_duration_min
         total_distance_km_x10
                    │
                    ↓
┌──────────────────────────────────────────────────────────────┐
│                qiban_vehicle_service                          │
│                (每秒循环，自动启动)                             │
│                                                              │
│  1. Mock 数据生成（基底值）                                    │
│  2. 读取 /data/qiban_inputs/* 文件输入（覆盖）                 │
│  3. 读取 /dev/adc0 GPADC 硬件输入（覆盖）                     │
│  4. 计算告警标志                                              │
│  5. 原子写入 /data/qiban_vehicle_state.json                   │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ↓
┌──────────────────────────────────────────────────────────────┐
│                     qiban_ui                                  │
│                     (每秒轮询，自动启动)                       │
│                                                              │
│  1. 读 /data/qiban_vehicle_state.json → 仪表盘数据            │
│  2. 读 /data/qiban_map/state.json     → 地图状态              │
│  3. 读 /data/qiban_nav_state.json     → 导航信息              │
│  4. 读 /data/qiban_location_state.json → 位置标记             │
│  5. 更新所有 LVGL 控件                                        │
│  6. 检测导航状态变化 → 自动跳转地图页                          │
│                                                              │
│  用户交互:                                                    │
│    点击快捷按钮 → spawn qiban_nav_service                      │
│    点击地图控件 → spawn qiban_map_service                      │
│    点击录音按钮 → spawn qiban_voice_service                    │
└──────────────────────────┬──────────────────────────────────┘
                           │ posix_spawnp (按需)
              ┌────────────┼────────────┐
              ↓            ↓            ↓
┌──────────────────┐ ┌──────────────┐ ┌──────────────────┐
│ qiban_nav_service│ │qiban_map_    │ │qiban_voice_      │
│ (按需启动)       │ │service       │ │service           │
│                  │ │(按需启动)    │ │(按需启动)         │
│ 解析目的地       │ │              │ │                  │
│ 高德地理编码     │ │ 高德静态地图  │ │ 意图解析         │
│ 距离/ETA 计算    │ │ API 下载     │ │ ASR/TTS 管线     │
│       ↓          │ │       ↓      │ │       ↓          │
│ nav_state.json   │ │ map/state.   │ │ spawn nav_       │
│ nav_inputs/      │ │ json         │ │ service          │
│ location_state   │ │ current.png  │ │ spawn map_       │
│ .json            │ │ session.json │ │ service          │
└──────────────────┘ │ location_    │ └──────────────────┘
                     │ state.json   │
                     └──────────────┘
```

### 9.2 单次导航的完整时序

```
T+0.000s  用户点击 "清华大学" 按钮
T+0.001s  UI spawn qiban_nav_service start 清华大学
T+0.002s  UI spawn qiban_map_service refresh
T+0.003s  UI 跳转到地图页，显示 "正在刷新导航地图..."

T+0.100s  qiban_nav_service 启动
T+0.101s  查找内置目的地 "清华大学" → 命中
T+0.102s  写入 /data/qiban_nav_state.json (active=true, distance=6200m)
T+0.103s  写入 /data/qiban_inputs/nav_remaining_m (6200)
T+0.104s  spawn qiban_map_service route 116.481 39.990 116.326 40.003 14 清华大学
T+0.105s  写入 /data/qiban_location_state.json (lon=116.481, lat=39.990)
T+0.106s  qiban_nav_service 退出

T+0.200s  qiban_map_service refresh 启动
T+0.201s  读取 session.json → 当前中心和缩放
T+0.202s  构建高德静态地图 URL
T+0.500s  HTTP 下载地图 PNG → /data/qiban_map/current.png
T+0.501s  写入 /data/qiban_map/state.json (ready=1)
T+0.502s  qiban_map_service 退出

T+0.300s  qiban_map_service route 启动（被 nav_service spawn）
T+0.301s  构建带路线的高德静态地图 URL（含 markers + path）
T+0.800s  HTTP 下载路线地图 PNG → /data/qiban_map/current.png
T+0.801s  更新 /data/qiban_map/state.json
T+0.802s  更新 /data/qiban_location_state.json
T+0.803s  qiban_map_service 退出

T+1.000s  qiban_vehicle_service 下一轮循环
T+1.001s  读取 /data/qiban_inputs/nav_remaining_m (6200)
T+1.002s  写入 /data/qiban_vehicle_state.json (nav_remaining_m=6200)

T+1.000s  qiban_ui 定时器触发
T+1.001s  读取 /data/qiban_nav_state.json → active=true
T+1.002s  检测到导航激活 → 自动跳转地图页
T+1.003s  读取 /data/qiban_map/state.json → ready=1, image_path 有效
T+1.004s  显示路线地图 PNG
T+1.005s  显示位置标记（红点 + "我"）
T+1.006s  更新导航距离显示: "导航 6200 m"

T+2.000s  vehicle_service 下一轮
T+2.001s  speed = 22 (pattern[1])
T+2.002s  nav_remaining = 6200 - 22*8 = 6024
T+2.003s  更新 vehicle_state.json

T+2.000s  UI 定时器触发
T+2.001s  更新速度: "22 km/h"
T+2.002s  更新导航: "6024 m"

...持续循环，导航距离逐渐减少...
```

---

## 十、文件系统总线协议

### 10.1 文件清单

| 文件路径 | 生产者 | 消费者 | 更新频率 | 格式 |
|----------|--------|--------|----------|------|
| `/data/qiban_vehicle_state.json` | vehicle_service | UI | 每秒 | JSON |
| `/data/qiban_map/state.json` | map_service | UI | 按需 | JSON |
| `/data/qiban_map/current.png` | map_service | UI | 按需 | PNG |
| `/data/qiban_map/session.json` | map_service | map_service | 按需 | JSON |
| `/data/qiban_map/render.lock` | map_service | map_service | 按需 | 空文件 |
| `/data/qiban_nav_state.json` | nav_service | UI, vehicle_service | 按需 | JSON |
| `/data/qiban_nav_request.json` | voice_service | nav_service | 按需 | JSON |
| `/data/qiban_location_state.json` | sensor_bridge / nav_service / map_service | UI | 按需 | JSON |
| `/data/qiban_inputs/speed_kmh` | sensor_bridge | vehicle_service | 按需 | 纯整数 |
| `/data/qiban_inputs/battery_percent` | sensor_bridge | vehicle_service | 按需 | 纯整数 |
| `/data/qiban_inputs/nav_remaining_m` | nav_service / sensor_bridge | vehicle_service | 按需 | 纯整数 |
| `/data/qiban_inputs/ride_duration_min` | sensor_bridge | vehicle_service | 按需 | 纯整数 |
| `/data/qiban_inputs/total_distance_km_x10` | sensor_bridge | vehicle_service | 按需 | 纯整数 |

### 10.2 JSON Schema 版本

所有 JSON 文件都包含：
- `schema_version`: 当前为 1
- `source`: 数据来源标识
- `generated_at_epoch_s`: Unix 时间戳

详细 Schema 定义位于 `shared/` 目录下的 JSON Schema 文件。

---

## 十一、关键设计模式

### 11.1 原子文件写入模式

**所有写操作都遵循此模式**，防止消费者读到半截数据：

```
写入流程:
  1. fopen("data.json.tmp", "w")    // 创建临时文件
  2. fprintf() 写入完整内容
  3. fclose()
  4. rename("data.json.tmp", "data.json")  // 原子替换

为什么安全:
  - rename() 在 POSIX 中是原子操作
  - 消费者要么读到旧文件，要么读到新文件，不会读到一半
  - 即使写入过程中断电，旧文件仍然完整
```

### 11.2 多源数据融合模式

```
vehicle_service 的数据优先级:
  1. 文件输入 (/data/qiban_inputs/*)     ← 最高优先级
  2. GPADC 硬件输入 (/dev/adc0)          ← 次优先级
  3. Mock 模拟数据                        ← 兜底

source 字段标记数据来源:
  "mock:qiban_vehicle_service"            ← 纯 Mock
  "board:file-telemetry"                  ← 有文件输入
  "board:gpadc+builtin_motion"            ← 有 ADC 输入
  "board:file-telemetry+gpadc"            ← 两者都有
```

### 11.3 进程间通信模式

```
通信方式: posix_spawnp() — 非阻塞进程创建

UI → nav_service:   posix_spawnp("qiban_nav_service", "start", "清华大学")
UI → map_service:   posix_spawnp("qiban_map_service", "refresh")
UI → voice_service: posix_spawnp("qiban_voice_service", "record", "3")
nav_service → map_service: posix_spawnp("qiban_map_service", "route", ...)

特点:
  - 非阻塞: spawn 后立即返回，不等待子进程
  - 一次性: 子进程执行完就退出，不常驻
  - 通过文件系统传递结果: 子进程写 JSON，父进程下次定时器读取
```

### 11.4 定时轮询模式

```
UI 和 vehicle_service 都使用 1 秒定时器轮询:

vehicle_service: 每秒写一次 JSON（生产者）
UI:              每秒读一次 JSON（消费者）

为什么不用事件驱动:
  - NuttX 的文件系统不支持 inotify
  - 轮询 1 秒对于电动车仪表盘足够实时
  - 实现简单，无需复杂的 IPC 机制
```

### 11.5 自研轻量 JSON 解析器

```
所有 C 服务都实现了自己的 JSON 解析器，不依赖外部库:

qiban_json_find_key(json, "speed_kmh")     → 找到 "speed_kmh" 位置
qiban_json_extract_int(json, "speed_kmh", &value)  → 提取整数值
qiban_json_extract_string(json, "source", buffer)  → 提取字符串

为什么不用 cJSON:
  - 减少固件体积
  - 减少内存分配
  - 只需要读取，不需要构建 JSON
  - 对于固定格式的 JSON 足够可靠
```

---

## 附录：快速命令参考

```bash
# 在 NSH Shell 中手动操作

# 查看车辆状态
cat /data/qiban_vehicle_state.json

# 手动写入传感器数据
qiban_sensor_bridge write speed_kmh 30
qiban_sensor_bridge write battery_percent 50

# 手动启动导航
qiban_nav_service start 软件园二期
qiban_nav_service status
qiban_nav_service clear

# 手动操作地图
qiban_map_service amap 116.481485 39.990464 14 "当前位置"
qiban_map_service zoom in
qiban_map_service pan up
qiban_map_service refresh
qiban_map_service status

# 手动语音操作
qiban_voice_service intent "导航到清华大学"
qiban_voice_service announce-nav
qiban_voice_service record 3

# 查看导航状态
cat /data/qiban_nav_state.json

# 查看地图状态
cat /data/qiban_map/state.json

# 查看位置状态
cat /data/qiban_location_state.json
```

---

> 本文档基于 `/home/xjx/contest2026_110_hahaha/` 源码的实际执行路径分析生成。
> 每个流程步骤均可在对应源码文件中找到精确的代码行号。
