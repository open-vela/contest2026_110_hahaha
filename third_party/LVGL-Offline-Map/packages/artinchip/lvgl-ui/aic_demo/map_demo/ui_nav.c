/*********************************************************
 * @file ui_nav.c
 * @author ^^^^^^^ ()
 * @brief 对接独立离线地图演示页面和可移植离线导航库
 * @version 1.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui.h"
#include "aic_osal.h"
#include "lv_offline_nav.h"
#ifdef RT_USING_FINSH
#include "finsh.h"
#endif

#define MAP_DEMO_NAV_GRAPH_PATH "L:/sdcard/nav/road_graph.bin" /* SD 卡内离线路网二进制文件路径 */
#define MAP_DEMO_NAV_ROUTE_ZOOM LV_OFFLINE_NAV_DEFAULT_ROUTE_ZOOM /* 路线点写入地图轨迹时使用的基准缩放级别 */
#define MAP_DEMO_NAV_ROUTE_RENDER_MAX LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS /* 独立地图演示地图轨迹层单次写入路线采样点上限 */
#define MAP_DEMO_NAV_TURN_EVENT_MAX LV_OFFLINE_NAV_DEFAULT_MAX_TURN_EVENTS /* 独立地图演示单条导航路线缓存的最大转向事件数量 */
#define MAP_DEMO_NAV_ARG_COUNT 5 /* shell 规划命令参数数量，命令名加起终点四个经纬度 */
#define MAP_DEMO_NAV_DEVIATE_ARG_COUNT 3 /* shell 偏航模拟命令参数数量，命令名加当前位置经纬度 */
#define MAP_DEMO_NAV_DEMO_ARG_MIN_COUNT 1 /* 默认路线命令最少参数数量，仅包含命令名 */
#define MAP_DEMO_NAV_DEMO_ARG_MAX_COUNT 2 /* 默认路线命令最多参数数量，命令名加路线序号 */
#define MAP_DEMO_NAV_DECIMAL_DEGREE_LIMIT 1000.0 /* shell 经纬度参数的小数度判断阈值，超过该值时按 E7 整数处理 */
#ifdef AIC_PSRAM_SW_EN
#define MAP_DEMO_NAV_MEM_REGION MEM_PSRAM_SW /* 路网规划临时缓存优先放入 PSRAM SW 堆，避免占用 LVGL 默认堆 */
#else
#define MAP_DEMO_NAV_MEM_REGION MEM_DEFAULT /* 未启用 PSRAM SW 时回退到系统默认堆，保证代码可编译 */
#endif

typedef struct {
    int start_lon_e7; /* 默认路线起点经度 E7 定点值，单位为 1e-7 度 */
    int start_lat_e7; /* 默认路线起点纬度 E7 定点值，单位为 1e-7 度 */
    int end_lon_e7; /* 默认路线终点经度 E7 定点值，单位为 1e-7 度 */
    int end_lat_e7; /* 默认路线终点纬度 E7 定点值，单位为 1e-7 度 */
    int wrong_lon_e7; /* 默认错误路口模拟点经度 E7 定点值，用于偏航重规划 */
    int wrong_lat_e7; /* 默认错误路口模拟点纬度 E7 定点值，用于偏航重规划 */
    const char *name; /* 默认路线名称，仅用于 shell 打印提示，指向静态字符串 */
} ui_nav_demo_route_t;

typedef struct {
    int start_lon_e7; /* shell 输入的起点经度 E7 定点值，单位为 1e-7 度 */
    int start_lat_e7; /* shell 输入的起点纬度 E7 定点值，单位为 1e-7 度 */
    int end_lon_e7; /* shell 输入的终点经度 E7 定点值，单位为 1e-7 度 */
    int end_lat_e7; /* shell 输入的终点纬度 E7 定点值，单位为 1e-7 度 */
    bool deviation; /* true 表示本次请求来自偏航模拟，起点为偏航后的当前位置 */
    bool pending; /* 是否存在等待 LVGL 线程执行的 shell 路线规划请求 */
} ui_nav_shell_route_request_t;

static const ui_nav_demo_route_t ui_NavDemoRoutes[] = {
    {1140918595, 321578674, 1140844719, 321461771, 1140923149, 321576721, "campus south"},
    {1140804560, 321604165, 1140837705, 321446755, 1140804044, 321604165, "north to south"},
    {1140921917, 321591338, 1140868214, 321446838, 1140922567, 321585964, "long turn route"},
    {1140916830, 321574632, 1140849841, 321533356, 1140920917, 321571018, "short video route"},
}; /* 设备端拍摄视频使用的默认路线和错误路口点，坐标来自当前 road_graph.bin 可达性筛选 */

static ui_nav_shell_route_request_t ui_NavShellRouteRequest; /* shell 路线规划请求缓存，由 lv_async_call 切到 LVGL 线程消费 */
static int ui_NavCurrentLonE7; /* 最近一次真实定位经度 E7 定点值，单位为 1e-7 度，优先用于地图长按规划起点 */
static int ui_NavCurrentLatE7; /* 最近一次真实定位纬度 E7 定点值，单位为 1e-7 度，优先用于地图长按规划起点 */
static bool ui_NavCurrentLocationValid; /* true 表示已经收到可用于路线规划起点的实时定位 */
static int ui_NavLastEndLonE7; /* 最近一次成功导航的终点经度 E7 定点值，供偏航重规划沿用 */
static int ui_NavLastEndLatE7; /* 最近一次成功导航的终点纬度 E7 定点值，供偏航重规划沿用 */
static bool ui_NavLastEndValid; /* true 表示已经有可用于偏航重规划的历史终点 */
static lv_offline_nav_point_t ui_NavRoutePoints[MAP_DEMO_NAV_ROUTE_RENDER_MAX]; /* 当前导航路线采样点缓存，供地图轨迹层渲染 */
static lv_offline_nav_turn_event_t ui_NavTurnEvents[MAP_DEMO_NAV_TURN_EVENT_MAX]; /* 当前导航路线转向提示缓存 */
static lv_offline_nav_route_t ui_NavRoute = {
    ui_NavRoutePoints,
    MAP_DEMO_NAV_ROUTE_RENDER_MAX,
    0U,
    ui_NavTurnEvents,
    MAP_DEMO_NAV_TURN_EVENT_MAX,
    0U,
    0U,
    {0, 0, 0U},
    false,
}; /* 当前导航路线结果，所有权属于独立地图演示导航适配层 */

/*********************************************************
 * @brief 在 LVGL 线程执行 shell 提交的离线路线规划
 * @param user_data LVGL 异步调用参数，当前未使用
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navApplyShellRoute(void *user_data);

/*********************************************************
 * @brief 获取当前路线渲染应使用的地图缩放级别
 * @return int 当前地图缩放级别，地图未初始化时回退到导航默认缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navGetCurrentRouteZoom(void);

/*********************************************************
 * @brief 通过独立地图演示指定堆申请导航临时缓存
 * @param size 需要申请的字节数，必须大于 0
 * @param user_data 内存回调上下文，当前未使用
 * @return void* 申请成功的缓存指针，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void *ui_navAllocMemory(size_t size, void *user_data)
{
    void *buffer; /* 申请到的导航临时缓存，优先来自 PSRAM SW 堆 */

    (void)user_data;
    if(size == 0U) {
        return NULL;
    }

    buffer = aicos_malloc(MAP_DEMO_NAV_MEM_REGION, size);

    return buffer;
}

/*********************************************************
 * @brief 释放通过独立地图演示指定堆申请的导航临时缓存
 * @param buffer 需要释放的缓存指针，允许为空
 * @param user_data 内存回调上下文，当前未使用
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navFreeMemory(void *buffer, void *user_data)
{
    (void)user_data;
    if(buffer == NULL) {
        return;
    }

    aicos_free(MAP_DEMO_NAV_MEM_REGION, buffer);
}

/*********************************************************
 * @brief 填充 独立地图演示离线导航库配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navFillOfflineConfig(lv_offline_nav_config_t *config)
{
    if(config == NULL) {
        return;
    }

    lv_offline_nav_get_default_config(config);
    config->graph_path = MAP_DEMO_NAV_GRAPH_PATH;
    config->max_render_points = MAP_DEMO_NAV_ROUTE_RENDER_MAX;
    config->max_turn_events = MAP_DEMO_NAV_TURN_EVENT_MAX;
    config->malloc_cb = ui_navAllocMemory;
    config->free_cb = ui_navFreeMemory;
    config->mem_user_data = NULL;
}

/*********************************************************
 * @brief 将 shell 输入的经纬度转换为 E7 定点值
 * @param text shell 参数字符串，不能为空，支持十进制度或 E7 整数
 * @param value_e7 输出 E7 定点经纬度，不能为空
 * @return bool true 表示解析成功，false 表示参数为空或格式非法
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool ui_navParseLonLatArg(const char *text, int *value_e7)
{
    char *end_ptr; /* strtod 解析结束位置，用于判断输入是否完整消费 */
    double value; /* shell 输入的经纬度数值，可能是十进制度或 E7 整数 */

    if(text == NULL || value_e7 == NULL) {
        return false;
    }

    end_ptr = NULL;
    value = strtod(text, &end_ptr);
    if(end_ptr == text || *end_ptr != '\0') {
        return false;
    }

    if(value > -MAP_DEMO_NAV_DECIMAL_DEGREE_LIMIT && value < MAP_DEMO_NAV_DECIMAL_DEGREE_LIMIT) {
        value *= 10000000.0;
    }

    *value_e7 = (int)value;

    return true;
}

/*********************************************************
 * @brief 解析默认路线序号参数
 * @param text shell 序号参数，允许为空，空值默认选择 1 号路线
 * @param route_index 输出默认路线数组索引，不能为空
 * @return bool true 表示序号有效，false 表示参数非法或超出路线数量
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool ui_navParseDemoRouteIndex(const char *text, uint32_t *route_index)
{
    char *end_ptr; /* strtol 解析结束位置，用于确认序号参数是否完整消费 */
    long value; /* shell 输入的默认路线序号，用户按 1 开始计数 */
    uint32_t route_count; /* 默认路线数组内可用路线数量 */

    if(route_index == NULL) {
        return false;
    }

    route_count = (uint32_t)(sizeof(ui_NavDemoRoutes) / sizeof(ui_NavDemoRoutes[0]));
    if(text == NULL) {
        *route_index = 0U;
        return route_count > 0U;
    }

    end_ptr = NULL;
    value = strtol(text, &end_ptr, 10);
    if(end_ptr == text || *end_ptr != '\0' || value <= 0 || (uint32_t)value > route_count) {
        return false;
    }

    *route_index = (uint32_t)value - 1U;

    return true;
}

/*********************************************************
 * @brief 打印默认导航路线列表
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navPrintDemoRoutes(void)
{
    uint32_t route_index; /* 当前打印的默认路线数组索引 */
    uint32_t route_count; /* 默认路线数组内可用路线数量 */
    const ui_nav_demo_route_t *route; /* 当前打印的默认路线配置 */

    route_count = (uint32_t)(sizeof(ui_NavDemoRoutes) / sizeof(ui_NavDemoRoutes[0]));
    printf("Default demo routes:\n");
    for(route_index = 0U; route_index < route_count; route_index++) {
        route = &ui_NavDemoRoutes[route_index];
        printf("  %lu: %s\n", (unsigned long)(route_index + 1U), route->name);
        printf("     map_nav_demo %lu\n", (unsigned long)(route_index + 1U));
        printf("     map_wrong_turn %lu\n", (unsigned long)(route_index + 1U));
    }
}

/*********************************************************
 * @brief 打印离线路径规划 shell 命令用法
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navPrintShellUsage(void)
{
    printf("Usage:\n");
    printf("  map_nav <start_lon> <start_lat> <end_lon> <end_lat>\n");
    printf("  map_deviate <current_lon> <current_lat>\n");
    printf("  map_nav_demo [route_index]\n");
    printf("  map_wrong_turn [route_index]\n");
    printf("Example decimal degrees:\n");
    printf("  map_nav 114.0773448 32.1487874 114.0867824 32.1390308\n");
    printf("  map_deviate 114.0705000 32.1450000\n");
    printf("Example E7 integers:\n");
    printf("  map_nav 1140773448 321487874 1140867824 321390308\n");
    printf("  map_deviate 1140705000 321450000\n");
    ui_navPrintDemoRoutes();
}

/*********************************************************
 * @brief 获取当前路线渲染应使用的地图缩放级别
 * @return int 当前地图缩放级别，地图未初始化时回退到导航默认缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navGetCurrentRouteZoom(void)
{
    int zoom; /* 当前地图缩放级别，用于重规划后保持用户当前地图层级 */

    zoom = ui_mapGetZoom();
    if(zoom <= 0) {
        zoom = MAP_DEMO_NAV_ROUTE_ZOOM;
    }

    return zoom;
}

/*********************************************************
 * @brief 将离线导航库输出路线渲染到独立地图演示页面
 * @param route 离线导航库输出路线指针，不能为空
 * @return bool true 表示路线已写入地图轨迹层，false 表示地图不可用或路线为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool ui_navRenderRouteToMap(const lv_offline_nav_route_t *route)
{
    uint32_t point_index; /* 当前写入地图轨迹层的路线采样点索引 */
    int route_zoom; /* 本次路线写入地图轨迹层使用的当前地图缩放级别 */
    bool rendered; /* 路线写入结果，true 表示至少一个轨迹点写入成功 */

    if(route == NULL || route->point_count == 0U || route->points == NULL || !route->has_center_point) {
        return false;
    }

    route_zoom = ui_navGetCurrentRouteZoom();
    if(!ui_mapCenterLonLatE7(route->center_point.lon_e7, route->center_point.lat_e7, route_zoom)) {
        return false;
    }

    rendered = false;
    ui_mapTrackBeginBatch();
    ui_mapTrackClear();
    for(point_index = 0U; point_index < route->point_count; point_index++) {
        if(ui_mapTrackAddLonLatE7WithDistance(route->points[point_index].lon_e7,
                                              route->points[point_index].lat_e7, route_zoom,
                                              route->points[point_index].distance_m)) {
            rendered = true;
        }
    }
    ui_mapTrackEndBatch();

    return rendered;
}

/*********************************************************
 * @brief 提交 shell 路线请求到 LVGL 线程执行
 * @param request shell 解析后的路线请求指针，不能为空
 * @param command_name shell 命令名，用于打印提交结果，不能为空
 * @return int 0 表示请求已提交，负数表示异步提交失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navSubmitShellRoute(const ui_nav_shell_route_request_t *request, const char *command_name)
{
    if(request == NULL || command_name == NULL) {
        return -1;
    }

    ui_NavShellRouteRequest = *request;
    ui_NavShellRouteRequest.pending = true;
    if(lv_async_call(ui_navApplyShellRoute, NULL) != LV_RESULT_OK) {
        ui_NavShellRouteRequest.pending = false;
        printf("%s: submit async route failed\n", command_name);
        return -1;
    }

    printf("%s: route request submitted\n", command_name);

    return 0;
}

/*********************************************************
 * @brief 在 LVGL 线程执行 shell 提交的离线路线规划
 * @param user_data LVGL 异步调用参数，当前未使用
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_navApplyShellRoute(void *user_data)
{
    ui_nav_shell_route_request_t request; /* 本次从 shell 缓存中取出的路线规划请求 */
    bool route_ready; /* 路线规划结果，true 表示已在地图上重绘并启动跟随 */

    (void)user_data;

    if(!ui_NavShellRouteRequest.pending) {
        return;
    }

    request = ui_NavShellRouteRequest;
    ui_NavShellRouteRequest.pending = false;
    route_ready = ui_navPlanRoute(request.start_lon_e7, request.start_lat_e7,
                                  request.end_lon_e7, request.end_lat_e7);
    if(route_ready) {
        ui_mapSetNavigationActive(true);
        ui_mapTrackStartFollow();
        if(request.deviation) {
            printf("map_deviate: reroute ready, follow restarted\n");
        } else {
            printf("map_nav: route ready, follow started\n");
        }
    } else {
        ui_mapSetNavigationActive(false);
        if(request.deviation) {
            printf("map_deviate: reroute plan failed\n");
        } else {
            printf("map_nav: route plan failed\n");
        }
    }
}

/*********************************************************
 * @brief 按 E7 经纬度规划并显示导航路线
 * @param start_lon_e7 起点经度 E7 定点值，单位为 1e-7 度
 * @param start_lat_e7 起点纬度 E7 定点值，单位为 1e-7 度
 * @param end_lon_e7 终点经度 E7 定点值，单位为 1e-7 度
 * @param end_lat_e7 终点纬度 E7 定点值，单位为 1e-7 度
 * @return bool true 表示路线规划并渲染成功，false 表示路网不可用或终点不可达
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navPlanRoute(int start_lon_e7, int start_lat_e7, int end_lon_e7, int end_lat_e7)
{
    lv_offline_nav_config_t config; /* 离线导航库配置，基于默认配置覆盖 独立地图演示参数 */
    bool route_ready; /* 路线规划和渲染结果，true 表示地图轨迹层已写入路线 */

    ui_navFillOfflineConfig(&config);
    route_ready = lv_offline_nav_plan_route(&config, start_lon_e7, start_lat_e7, end_lon_e7, end_lat_e7,
                                            &ui_NavRoute);
    if(route_ready) {
        route_ready = ui_navRenderRouteToMap(&ui_NavRoute);
    }
    if(!route_ready) {
        lv_offline_nav_clear_route(&ui_NavRoute);
    } else {
        ui_NavLastEndLonE7 = end_lon_e7;
        ui_NavLastEndLatE7 = end_lat_e7;
        ui_NavLastEndValid = true;
    }

    return route_ready;
}

/*********************************************************
 * @brief 使用真实定位或虚拟车辆位置作为起点规划到指定终点
 * @param end_lon_e7 终点经度 E7 定点值，单位为 1e-7 度
 * @param end_lat_e7 终点纬度 E7 定点值，单位为 1e-7 度
 * @return bool true 表示路线规划并渲染成功，false 表示终点不可达或路网不可用
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navPlanRouteFromCurrentLocation(int end_lon_e7, int end_lat_e7)
{
    int start_lon_e7; /* 本次规划使用的起点经度 E7 定点值，优先来自真实定位 */
    int start_lat_e7; /* 本次规划使用的起点纬度 E7 定点值，优先来自真实定位 */
    bool use_real_location; /* true 表示本次规划起点来自真实定位，false 表示来自虚拟车辆位置 */
    bool route_ready; /* 从当前位置规划到长按终点的结果，true 表示路线已写入地图 */

    use_real_location = ui_NavCurrentLocationValid;
    if(use_real_location) {
        start_lon_e7 = ui_NavCurrentLonE7;
        start_lat_e7 = ui_NavCurrentLatE7;
    } else if(!ui_mapGetVirtualLocationE7(&start_lon_e7, &start_lat_e7)) {
        /* 没有真实 GPS 且尚未启动过虚拟跟随时，从默认演示路线起点开始规划。 */
        start_lon_e7 = ui_NavDemoRoutes[0].start_lon_e7;
        start_lat_e7 = ui_NavDemoRoutes[0].start_lat_e7;
    }

    route_ready = ui_navPlanRoute(start_lon_e7, start_lat_e7, end_lon_e7, end_lat_e7);
    if(route_ready) {
        ui_mapSetNavigationActive(true);
        if(use_real_location) {
            /* 真实定位模式下不启动模拟跟随，直接把车标投影到最近一次 GPS 位置。 */
            (void)ui_mapUpdateLocationE7(start_lon_e7, start_lat_e7, ui_navGetCurrentRouteZoom(), NULL);
        } else {
            /* 无 GPS 调试模式下用虚拟车辆继续推进，用户拖图不会改变车辆经纬度。 */
            ui_mapTrackStartFollow();
        }
    }

    return route_ready;
}

/*********************************************************
 * @brief 获取最近一次实时定位坐标
 * @param lon_e7 输出当前经度 E7 定点值，允许为空
 * @param lat_e7 输出当前纬度 E7 定点值，允许为空
 * @return bool true 表示存在可用定位，false 表示尚未收到实时定位
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navGetCurrentLocationE7(int *lon_e7, int *lat_e7)
{
    if(!ui_NavCurrentLocationValid) {
        return false;
    }

    if(lon_e7 != NULL) {
        *lon_e7 = ui_NavCurrentLonE7;
    }
    if(lat_e7 != NULL) {
        *lat_e7 = ui_NavCurrentLatE7;
    }

    return true;
}

/*********************************************************
 * @brief 规划一条位于当前离线地图范围内的演示导航路线
 * @return bool true 表示演示路线规划成功，false 表示路网不可用或示例终点不可达
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navPlanDemoRoute(void)
{
    bool route_ready; /* 演示导航路线规划结果，true 表示地图轨迹层已写入路线 */

    route_ready = ui_navPlanRoute(ui_NavDemoRoutes[0].start_lon_e7, ui_NavDemoRoutes[0].start_lat_e7,
                                  ui_NavDemoRoutes[0].end_lon_e7, ui_NavDemoRoutes[0].end_lat_e7);

    return route_ready;
}

/*********************************************************
 * @brief 使用实时定位经纬度更新当前导航位置
 * @param lon_e7 实时定位经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 实时定位纬度 E7 定点值，单位为 1e-7 度
 * @param result 输出定位投影结果，允许为空
 * @return bool true 表示定位已投影到当前路线并更新导航状态，false 表示当前没有可用路线或定位无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navUpdateLocationE7(int lon_e7, int lat_e7, ui_nav_location_result_t *result)
{
    bool updated; /* 实时定位更新结果，true 表示车标和导航提示已同步到当前定位 */

    ui_NavCurrentLonE7 = lon_e7;
    ui_NavCurrentLatE7 = lat_e7;
    ui_NavCurrentLocationValid = true;
    updated = ui_mapUpdateLocationE7(lon_e7, lat_e7, ui_navGetCurrentRouteZoom(), result);

    return updated;
}

/*********************************************************
 * @brief 查询当前导航提示状态
 * @param traveled_distance_m 当前已行驶里程，单位为米
 * @param guidance 输出导航提示状态，不能为空
 * @return bool true 表示存在有效导航路线，false 表示当前没有导航路线
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navGetGuidance(uint32_t traveled_distance_m, lv_offline_nav_guidance_t *guidance)
{
    bool has_guidance; /* true 表示当前存在有效导航路线 */

    has_guidance = lv_offline_nav_get_guidance(&ui_NavRoute, traveled_distance_m, guidance);

    return has_guidance;
}

#ifdef RT_USING_FINSH
/*********************************************************
 * @brief shell 命令入口，按起终点经纬度重新离线规划并绘制路线
 * @param argc shell 参数数量
 * @param argv shell 参数数组，argv[1..4] 分别为起点经纬度和终点经纬度
 * @return int 0 表示请求已提交，负数表示参数错误或异步提交失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navShellRoute(int argc, char **argv)
{
    ui_nav_shell_route_request_t request; /* shell 解析后的路线规划请求，随后提交到 LVGL 线程 */
    int submit_result; /* shell 请求提交结果，0 表示已进入 LVGL 异步队列 */

    if(argc != MAP_DEMO_NAV_ARG_COUNT) {
        ui_navPrintShellUsage();
        return -1;
    }

    if(!ui_navParseLonLatArg(argv[1], &request.start_lon_e7) ||
       !ui_navParseLonLatArg(argv[2], &request.start_lat_e7) ||
       !ui_navParseLonLatArg(argv[3], &request.end_lon_e7) ||
       !ui_navParseLonLatArg(argv[4], &request.end_lat_e7)) {
        ui_navPrintShellUsage();
        return -1;
    }

    request.deviation = false;
    request.pending = false;
    submit_result = ui_navSubmitShellRoute(&request, "map_nav");

    return submit_result;
}

MSH_CMD_EXPORT_ALIAS(ui_navShellRoute, map_nav, offline map route plan);

/*********************************************************
 * @brief shell 命令入口，选择默认路线重新离线规划并进入导航
 * @param argc shell 参数数量，允许只有命令名或命令名加路线序号
 * @param argv shell 参数数组，argv[1] 可选为默认路线序号
 * @return int 0 表示请求已提交，负数表示参数错误或异步提交失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navShellDemoRoute(int argc, char **argv)
{
    ui_nav_shell_route_request_t request; /* 默认路线规划请求，随后提交到 LVGL 线程执行 */
    const ui_nav_demo_route_t *route; /* 用户选择的默认路线配置 */
    uint32_t route_index; /* 默认路线数组索引，由 shell 序号转换得到 */
    int submit_result; /* shell 请求提交结果，0 表示已进入 LVGL 异步队列 */

    if(argc < MAP_DEMO_NAV_DEMO_ARG_MIN_COUNT || argc > MAP_DEMO_NAV_DEMO_ARG_MAX_COUNT) {
        ui_navPrintShellUsage();
        return -1;
    }

    if(!ui_navParseDemoRouteIndex(argc == MAP_DEMO_NAV_DEMO_ARG_MAX_COUNT ? argv[1] : NULL, &route_index)) {
        ui_navPrintShellUsage();
        return -1;
    }

    route = &ui_NavDemoRoutes[route_index];
    request.start_lon_e7 = route->start_lon_e7;
    request.start_lat_e7 = route->start_lat_e7;
    request.end_lon_e7 = route->end_lon_e7;
    request.end_lat_e7 = route->end_lat_e7;
    request.deviation = false;
    request.pending = false;
    printf("map_nav_demo: route %lu %s\n", (unsigned long)(route_index + 1U), route->name);
    submit_result = ui_navSubmitShellRoute(&request, "map_nav_demo");

    return submit_result;
}

MSH_CMD_EXPORT_ALIAS(ui_navShellDemoRoute, map_nav_demo, select default map route);

/*********************************************************
 * @brief shell 命令入口，模拟走错路后从当前位置到原终点重新规划
 * @param argc shell 参数数量
 * @param argv shell 参数数组，argv[1..2] 分别为偏航后的当前位置经纬度
 * @return int 0 表示请求已提交，负数表示参数错误、没有历史终点或异步提交失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navShellDeviation(int argc, char **argv)
{
    ui_nav_shell_route_request_t request; /* 偏航模拟请求，起点来自当前错误道路位置，终点沿用上一次导航终点 */
    int submit_result; /* 偏航重规划请求提交结果，0 表示已进入 LVGL 异步队列 */

    if(argc != MAP_DEMO_NAV_DEVIATE_ARG_COUNT) {
        ui_navPrintShellUsage();
        return -1;
    }

    if(!ui_NavLastEndValid) {
        printf("map_deviate: no active destination, run map_nav first\n");
        return -1;
    }

    if(!ui_navParseLonLatArg(argv[1], &request.start_lon_e7) ||
       !ui_navParseLonLatArg(argv[2], &request.start_lat_e7)) {
        ui_navPrintShellUsage();
        return -1;
    }

    request.end_lon_e7 = ui_NavLastEndLonE7;
    request.end_lat_e7 = ui_NavLastEndLatE7;
    request.deviation = true;
    request.pending = false;
    submit_result = ui_navSubmitShellRoute(&request, "map_deviate");

    return submit_result;
}

MSH_CMD_EXPORT_ALIAS(ui_navShellDeviation, map_deviate, simulate map route deviation);

/*********************************************************
 * @brief shell 命令入口，模拟在十字路口走错路后重新规划到默认路线终点
 * @param argc shell 参数数量，允许只有命令名或命令名加路线序号
 * @param argv shell 参数数组，argv[1] 可选为默认路线序号
 * @return int 0 表示请求已提交，负数表示参数错误或异步提交失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navShellWrongTurn(int argc, char **argv)
{
    ui_nav_shell_route_request_t request; /* 错路模拟重规划请求，起点为预置错误路口点 */
    const ui_nav_demo_route_t *route; /* 用户选择的默认路线配置 */
    uint32_t route_index; /* 默认路线数组索引，由 shell 序号转换得到 */
    int submit_result; /* shell 请求提交结果，0 表示已进入 LVGL 异步队列 */

    if(argc < MAP_DEMO_NAV_DEMO_ARG_MIN_COUNT || argc > MAP_DEMO_NAV_DEMO_ARG_MAX_COUNT) {
        ui_navPrintShellUsage();
        return -1;
    }

    if(!ui_navParseDemoRouteIndex(argc == MAP_DEMO_NAV_DEMO_ARG_MAX_COUNT ? argv[1] : NULL, &route_index)) {
        ui_navPrintShellUsage();
        return -1;
    }

    route = &ui_NavDemoRoutes[route_index];
    request.start_lon_e7 = route->wrong_lon_e7;
    request.start_lat_e7 = route->wrong_lat_e7;
    request.end_lon_e7 = route->end_lon_e7;
    request.end_lat_e7 = route->end_lat_e7;
    request.deviation = true;
    request.pending = false;
    ui_NavLastEndLonE7 = route->end_lon_e7;
    ui_NavLastEndLatE7 = route->end_lat_e7;
    ui_NavLastEndValid = true;
    printf("map_wrong_turn: route %lu %s\n", (unsigned long)(route_index + 1U), route->name);
    submit_result = ui_navSubmitShellRoute(&request, "map_wrong_turn");

    return submit_result;
}

MSH_CMD_EXPORT_ALIAS(ui_navShellWrongTurn, map_wrong_turn, simulate wrong turn reroute);

/*********************************************************
 * @brief shell 命令入口，打印当前内置的默认导航路线
 * @param argc shell 参数数量，仅允许命令名
 * @param argv shell 参数数组，当前未使用
 * @return int 0 表示路线列表已打印，负数表示参数错误
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int ui_navShellRoutes(int argc, char **argv)
{
    (void)argv;

    if(argc != MAP_DEMO_NAV_DEMO_ARG_MIN_COUNT) {
        ui_navPrintShellUsage();
        return -1;
    }

    ui_navPrintDemoRoutes();

    return 0;
}

MSH_CMD_EXPORT_ALIAS(ui_navShellRoutes, map_nav_routes, list default map routes);
#endif
