# 🗺️ LVGL Offline Map And Navigation

![LVGL](https://img.shields.io/badge/LVGL-8%20%7C%209-2ea44f?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-匠芯创D133KunlunPI-2ea888?style=flat-square)
![Language](https://img.shields.io/badge/Language-C99-00599C?style=flat-square)
![Offline Map](https://img.shields.io/badge/Map-Offline%20Tiles-0A66C2?style=flat-square)
![Offline Navigation](https://img.shields.io/badge/Navigation-Offline%20Routing-8A2BE2?style=flat-square)
![RTOS](https://img.shields.io/badge/Target-Embedded%20HMI-orange?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

一个基于 LVGL 的离线地图显示和离线导航规划 Demo，面向嵌入式 HMI、仪表屏和离线导航界面。Demo 由两部分组成：

- 🧩 `lv_offline_map`：负责离线瓦片显示、拖动、缩放、车辆位置、轨迹绘制和轨迹跟随播放。
- 🧭 `lv_offline_nav`：负责读取离线路网文件、规划起终点路线、输出路线采样点、转向事件和导航提示。

`ui_map.c` 是地图页面接入示例，展示地图控件如何显示瓦片、承载路线轨迹和刷新导航状态。这个 Demo 的核心交互是：用户在地图上长按选点，点击确认后，系统以当前车辆位置为起点、选中位置为终点进行离线导航规划，并把规划结果绘制回地图。

## 🎬 Demo Video

[Watch the demo on Bilibili](https://www.bilibili.com/video/BV15B7K6pEPi/)

## ✨ Features

- 🧱 支持 LVGL 8 和 LVGL 9
- 🗺️ 支持离线瓦片目录加载
- 👆 支持地图拖动、缩放按钮和缩放动画
- 📍 支持 WGS84 E7 经纬度与地图视图坐标互转
- 🚗 支持车辆位置显示和方向标旋转
- 🛣️ 支持轨迹点写入、轨迹分段显示和自动跟随
- 🎯 支持按瓦片坐标或经纬度设置地图中心
- 💾 支持离线路网二进制文件加载
- 🧭 支持按起点和终点 E7 经纬度规划路线
- ✅ 支持地图长按选点后自动规划到目标点
- 📏 支持输出路线采样点、路线总里程和路线中点
- ↩️ 支持左转、右转、掉头等转向事件
- 🪧 支持根据已行驶里程查询当前导航提示

## 📁 Repository Layout

```text
.
├── lv_offline_map.c       # 离线地图显示组件实现
├── lv_offline_map.h       # 离线地图显示组件公开 API
├── lv_offline_nav.c       # 离线路网导航规划实现
├── lv_offline_nav.h       # 离线路网导航规划公开 API
├── ui_map.c               # 地图页面接入示例
├── ui_nav.c               # 导航规划和地图轨迹联动示例
├── SConscript             # RT-Thread / SCons 构建脚本
└── tools/                 # 可选的地图瓦片和路网数据处理脚本
```

如果你的工程不是 SCons 构建方式，只需要把 `lv_offline_map.c`、`lv_offline_map.h`、`lv_offline_nav.c`、`lv_offline_nav.h`、`ui_map.c` 和可选的 `ui_nav.c` 加入自己的构建系统，并把对应目录加入头文件搜索路径。

## ✅ Requirements

- C99
- LVGL 8.x 或 LVGL 9.x
- 文件系统支持，例如 SD 卡、Flash 文件系统或虚拟文件系统
- LVGL 图片解码支持，瓦片使用 JPG 时需要启用 JPG 解码器
- 可用的离线路网文件，例如 `road_graph.bin`
- 可选：RT-Thread / SCons，用于直接复用 `SConscript`

## 🧱 Tile Directory

组件按以下目录结构读取离线瓦片：

```text
{map_dir}/{zoom}/{x}/{y}/{tile_file_name}
```

示例：

```text
/sdcard/map/17/107073/53161/tile.jpg
```

如果系统使用 LVGL 文件系统盘符，路径可以写成：

```c
config.map_dir = "L:/sdcard/map/";
config.tile_file_name = "tile.jpg";
```

## 🧭 Road Graph File

导航规划库通过 LVGL 文件系统读取离线路网二进制文件，默认路径为：

```text
L:/sdcard/nav/road_graph.bin
```

也可以通过 `lv_offline_nav_config_t.graph_path` 覆盖：

```c
lv_offline_nav_config_t config;

lv_offline_nav_get_default_config(&config);
config.graph_path = "L:/sdcard/nav/road_graph.bin";
```

路网文件需要覆盖当前地图瓦片区域，否则路线规划会因为起点或终点无法吸附到有效路网节点而失败。

## 🚀 Quick Start

在应用初始化流程中创建地图页面，然后切换到 `ui_map.c` 提供的页面对象：

```c
#include "ui.h"

void app_create_map_page(void)
{
    lv_obj_t *screen;

    ui_mapCreate();

    screen = ui_mapGetScreen();
    if(screen == NULL) {
        return;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_screen_load(screen);
#else
    lv_scr_load(screen);
#endif
}
```

页面创建完成后，用户可以在地图区域长按选择目的地。`ui_map.c` 会把长按位置从屏幕坐标反算成 WGS84 E7 经纬度，显示选点标记和确认按钮；用户确认后自动触发离线导航规划。

地图路径、缩放范围、默认中心点、车辆默认位置和轨迹容量都在 `ui_map.c` 顶部的配置宏中设置。移植到自己的项目时，优先修改这些配置项：

- 地图可视区域宽高
- 离线瓦片根目录
- 瓦片文件名
- 最小和最大缩放级别
- 默认缩放级别
- 默认中心瓦片坐标
- 默认车辆经纬度
- 轨迹最大点数

导航路网路径、路线采样点数量、转向事件数量和临时内存申请策略在导航适配层中配置。移植时通常需要修改：

- 离线路网文件路径
- 最大路线采样点数量
- 最大转向事件数量
- 规划临时内存申请和释放回调
- 默认演示路线的起终点坐标

## 🧩 ui_map.c Example

`ui_map.c` 展示了一个完整地图页面的接入方式：

- 创建地图页面对象
- 配置 `lv_offline_map_config_t`
- 创建 `lv_offline_map` 控件
- 设置默认中心瓦片和车辆位置
- 处理地图刷新、拖动和长按事件
- 封装轨迹添加、清空、批量写入和跟随播放接口
- 显示导航提示、剩余里程和路线选点状态

页面启动示例：

```c
void app_start_map_demo(void)
{
    ui_mapCreate();
    ui_mapCenterLonLatE7(1140918595, 321578674, ui_mapGetZoom());
    ui_mapSetVehicleLocationE7(1140918595, 321578674, ui_mapGetZoom(), true);
    ui_mapSeedDemoTrack();
    ui_mapTrackStartFollow();
}
```

其中 E7 经纬度表示把经纬度放大 `10000000` 倍。例如 `114.0918595` 度写成 `1140918595`。

## 🎯 Map Pick Navigation

地图选点后的导航规划是这个 Demo 的主要功能。交互流程如下：

1. 👆 用户在地图可滚动区域长按。
2. 📍 `ui_map.c` 读取当前输入点坐标，并调用 `lv_offline_map_view_point_to_lonlat_e7` 把视图坐标转换为 WGS84 E7 经纬度。
3. ✅ 页面显示选点标记和确认按钮。
4. 🧭 用户点击确认后，`ui_mapConfirmPickedRoute` 调用 `ui_navPlanRouteFromCurrentLocation`。
5. 🚗 `ui_navPlanRouteFromCurrentLocation` 优先使用实时定位作为起点；没有实时定位时，使用当前虚拟车辆位置；仍不可用时回退到默认演示起点。
6. 💾 `ui_navPlanRoute` 调用 `lv_offline_nav_plan_route` 读取 `road_graph.bin` 并规划从当前位置到选中终点的路线。
7. 🛣️ 规划成功后，`ui_navRenderRouteToMap` 把路线采样点写入 `ui_mapTrackAddLonLatE7WithDistance`，地图轨迹层显示完整路线。
8. 🪧 地图进入导航显示态，显示转向提示、剩余里程，并根据真实定位或虚拟车辆位置更新车标。

核心调用链：

```text
长按地图
  -> ui_mapHandleLongPressRoute
  -> lv_offline_map_view_point_to_lonlat_e7
  -> ui_mapConfirmPickedRoute
  -> ui_navPlanRouteFromCurrentLocation
  -> ui_navPlanRoute
  -> lv_offline_nav_plan_route
  -> ui_navRenderRouteToMap
  -> ui_mapTrackAddLonLatE7WithDistance
```

确认按钮回调的关键逻辑可以简化理解为：

```c
static void ui_mapConfirmPickedRoute(lv_event_t *e)
{
    int end_lon_e7;
    int end_lat_e7;
    bool route_ready;

    (void)e;

    if(!ui_MapPickedEndValid) {
        return;
    }

    end_lon_e7 = ui_MapPickedEndLonE7;
    end_lat_e7 = ui_MapPickedEndLatE7;
    ui_MapPickedEndValid = false;
    ui_mapSetPickOverlayVisible(false);

    route_ready = ui_navPlanRouteFromCurrentLocation(end_lon_e7, end_lat_e7);
    if(route_ready) {
        ui_mapUpdateHint();
    } else {
        lv_label_set_text(ui_MapHintLabel, "No route");
        ui_mapSetNavigationActive(false);
    }
}
```

这意味着使用者不需要手动输入起点和终点坐标。只要地图页面、瓦片目录和 `road_graph.bin` 已配置好，用户在地图上选中目的地后，Demo 会自动完成路线规划、路线绘制和导航状态刷新。

## 🧭 Offline Navigation Example

如果需要绕过地图选点交互，也可以直接调用 `lv_offline_nav` 规划指定起终点。`lv_offline_nav` 输出路线点后，可以把路线点写入 `lv_offline_map` 的轨迹层，从而在地图上显示规划结果：

```c
#include "lv_offline_nav.h"

#define ROUTE_POINT_MAX 1024
#define TURN_EVENT_MAX 64

static lv_offline_nav_point_t route_points[ROUTE_POINT_MAX];
static lv_offline_nav_turn_event_t turn_events[TURN_EVENT_MAX];

void app_plan_route(void)
{
    lv_offline_nav_config_t config;
    lv_offline_nav_route_t route = {
        .points = route_points,
        .point_capacity = ROUTE_POINT_MAX,
        .turn_events = turn_events,
        .turn_event_capacity = TURN_EVENT_MAX,
    };
    uint32_t point_index;

    lv_offline_nav_get_default_config(&config);
    config.graph_path = "L:/sdcard/nav/road_graph.bin";

    if(!lv_offline_nav_plan_route(&config,
                                  1140918595, 321578674,
                                  1140844719, 321461771,
                                  &route)) {
        return;
    }

    ui_mapTrackClear();
    ui_mapTrackBeginBatch();
    for(point_index = 0; point_index < route.point_count; point_index++) {
        ui_mapTrackAddLonLatE7WithDistance(route.points[point_index].lon_e7,
                                           route.points[point_index].lat_e7,
                                           LV_OFFLINE_NAV_DEFAULT_ROUTE_ZOOM,
                                           route.points[point_index].distance_m);
    }
    ui_mapTrackEndBatch();
    ui_mapSetNavigationActive(true);
    ui_mapTrackStartFollow();
}
```

导航过程中可以根据已行驶里程查询当前提示：

```c
void app_update_guidance(const lv_offline_nav_route_t *route, uint32_t traveled_distance_m)
{
    lv_offline_nav_guidance_t guidance;

    if(lv_offline_nav_get_guidance(route, traveled_distance_m, &guidance)) {
        /* guidance.turn_type 表示下一次转向类型。 */
        /* guidance.distance_to_turn_m 表示距离下一次转向的距离。 */
        /* guidance.remaining_distance_m 表示剩余里程。 */
    }
}
```

## 🔌 Public APIs

地图显示组件的公开接口位于 `lv_offline_map.h`。

| API                                                | Description            |
| -------------------------------------------------- | ---------------------- |
| `lv_offline_map_get_default_config`                | 获取默认配置           |
| `lv_offline_map_create_with_config`                | 使用配置创建地图控件   |
| `lv_offline_map_set_config`                        | 更新地图配置           |
| `lv_offline_map_center_tile`                       | 按瓦片坐标居中         |
| `lv_offline_map_center_lonlat_e7`                  | 按 WGS84 E7 经纬度居中 |
| `lv_offline_map_set_vehicle_location_e7`           | 更新车辆位置           |
| `lv_offline_map_view_point_to_lonlat_e7`           | 地图视图坐标转经纬度   |
| `lv_offline_map_lonlat_e7_to_view_point`           | 经纬度转地图视图坐标   |
| `lv_offline_map_set_zoom`                          | 设置缩放级别           |
| `lv_offline_map_zoom_in`                           | 放大一级               |
| `lv_offline_map_zoom_out`                          | 缩小一级               |
| `lv_offline_map_track_clear`                       | 清空轨迹               |
| `lv_offline_map_track_add_lonlat_e7`               | 按经纬度添加轨迹点     |
| `lv_offline_map_track_add_lonlat_e7_with_distance` | 添加带累计里程的轨迹点 |
| `lv_offline_map_track_update_location_e7`          | 用实时定位更新轨迹投影 |
| `lv_offline_map_track_start_follow`                | 启动轨迹跟随播放       |
| `lv_offline_map_track_stop_follow`                 | 停止轨迹跟随播放       |

导航规划组件的公开接口位于 `lv_offline_nav.h`。

| API                                 | Description                    |
| ----------------------------------- | ------------------------------ |
| `lv_offline_nav_get_default_config` | 获取默认导航配置               |
| `lv_offline_nav_clear_route`        | 清空路线输出状态               |
| `lv_offline_nav_plan_route`         | 按起终点 E7 经纬度规划离线路线 |
| `lv_offline_nav_get_guidance`       | 根据已行驶里程查询当前导航提示 |

## 📍 Coordinate Notes

- 📐 经纬度接口使用 WGS84 E7 定点格式。
- 🧭 如果瓦片来自高德、腾讯等 GCJ-02 坐标系地图源，可以保持 `use_gcj02_tile = true`。
- 🌐 如果瓦片来自标准 Web Mercator / OSM 坐标系地图源，通常需要设置 `use_gcj02_tile = false`。
- 🧱 默认瓦片尺寸按 `256 x 256` 设计。
- 📍 离线导航的路网坐标和地图瓦片坐标系需要保持一致，否则路线显示会和瓦片底图偏移。

## ⚠️ Notes

- 当前 Demo 的离线路网数据来自 OSM PBF，底图瓦片来自腾讯地图，两者的数据源、道路几何、道路更新频率和坐标处理方式并不完全一致。
- 即使地图瓦片侧启用了 GCJ-02 适配，OSM 路网和腾讯瓦片之间仍可能存在道路偏移、路口不一致、道路缺失或道路等级差异。
- 因此，路线规划结果、轨迹贴合、转向提示和地图上的路线显示都可能出现不准的情况，尤其是在复杂路口、新建道路、封闭道路和非机动车道路场景下。
- 当前瓦片地图仅供参考和学习使用，请遵守腾讯地图相关服务条款，不建议把示例瓦片直接用于商业产品或公开分发。
- 如果需要更高精度，建议让路网数据和瓦片底图来自同一数据源，并在生成路网和瓦片时统一坐标系、裁剪范围、缩放级别和更新时间。
- 本 Demo 不适合作为安全关键场景的唯一导航依据。

## 📦 Assets And Data

地图瓦片、路网文件、图标、字体和其它图片资源可能有独立授权。开源仓库中建议只提供少量可合法分发的测试瓦片和测试路网，或者提供数据生成脚本，不要把受限地图数据直接提交到仓库。

## 📄 License

代码采用 MIT License。地图瓦片、路网数据、字体和图标等资源可能有独立授权，请按各自来源的许可条款使用。
