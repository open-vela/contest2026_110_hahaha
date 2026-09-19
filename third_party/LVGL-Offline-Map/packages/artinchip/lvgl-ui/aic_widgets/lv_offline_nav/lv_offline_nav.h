/*********************************************************
 * @file lv_offline_nav.h
 * @author ^^^^^^^ ()
 * @brief 提供可移植的离线路网路径规划接口
 * @version 1.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#ifndef LV_OFFLINE_NAV_H
#define LV_OFFLINE_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LV_OFFLINE_NAV_DEFAULT_MAX_NODES 5000U /* 默认允许加载的最大路网节点数量 */
#define LV_OFFLINE_NAV_DEFAULT_MAX_EDGES 9000U /* 默认允许加载的最大有向边数量 */
#define LV_OFFLINE_NAV_DEFAULT_MAX_ROUTE_POINTS 1024U /* 默认允许保存的最大路线节点数量 */
#define LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS 1024U /* 默认输出的最大路线采样点数量 */
#define LV_OFFLINE_NAV_DEFAULT_MAX_TURN_EVENTS 64U /* 默认输出的最大转向事件数量 */
#define LV_OFFLINE_NAV_DEFAULT_ROUTE_ZOOM 17 /* 默认建议的路线渲染瓦片缩放级别 */

typedef void *(*lv_offline_nav_malloc_cb_t)(size_t size, void *user_data);
typedef void (*lv_offline_nav_free_cb_t)(void *buffer, void *user_data);

typedef enum {
    LV_OFFLINE_NAV_TURN_NONE = 0, /* 当前没有需要提示的转向动作 */
    LV_OFFLINE_NAV_TURN_LEFT, /* 下一次导航动作是左转 */
    LV_OFFLINE_NAV_TURN_RIGHT, /* 下一次导航动作是右转 */
    LV_OFFLINE_NAV_TURN_UTURN /* 下一次导航动作是掉头 */
} lv_offline_nav_turn_type_t;

typedef struct {
    int32_t lon_e7; /* 经纬度点的经度 E7 定点值，单位为 1e-7 度 */
    int32_t lat_e7; /* 经纬度点的纬度 E7 定点值，单位为 1e-7 度 */
    uint32_t distance_m; /* 路线点距路线起点的累计距离，单位为米 */
} lv_offline_nav_point_t;

typedef struct {
    uint32_t distance_m; /* 转向点距路线起点的累计距离，单位为米 */
    lv_offline_nav_turn_type_t type; /* 转向动作类型，用于调用者显示图标或语音提示 */
} lv_offline_nav_turn_event_t;

typedef struct {
    lv_offline_nav_turn_type_t turn_type; /* 下一次转向动作类型 */
    uint32_t distance_to_turn_m; /* 当前位置到下一次转向点的距离，单位为米 */
    uint32_t total_distance_m; /* 当前导航路线总里程，单位为米 */
    uint32_t traveled_distance_m; /* 当前已行驶里程，单位为米 */
    uint32_t remaining_distance_m; /* 当前剩余里程，单位为米 */
} lv_offline_nav_guidance_t;

typedef struct {
    const char *graph_path; /* 离线路网二进制文件路径，使用 LVGL 文件系统路径 */
    uint32_t max_nodes; /* 单个路网文件允许加载的最大节点数量 */
    uint32_t max_edges; /* 单个路网文件允许加载的最大有向边数量 */
    uint32_t max_route_points; /* Dijkstra 回溯阶段允许保存的最大路线节点数量 */
    uint32_t max_render_points; /* 输出到路线采样点数组的最大点数量 */
    uint16_t max_turn_events; /* 输出到转向事件数组的最大事件数量 */
    double turn_min_degree; /* 触发左右转提示的最小转向角度，单位为度 */
    double uturn_min_degree; /* 触发掉头提示的最小转向角度，单位为度 */
    lv_offline_nav_malloc_cb_t malloc_cb; /* 规划临时缓存申请回调，允许为空并回退到 LVGL 堆 */
    lv_offline_nav_free_cb_t free_cb; /* 规划临时缓存释放回调，允许为空并回退到 LVGL 堆 */
    void *mem_user_data; /* 传递给内存回调的用户上下文，库内不接管所有权 */
} lv_offline_nav_config_t;

typedef struct {
    lv_offline_nav_point_t *points; /* 调用者提供的路线采样点输出数组，库内不接管所有权 */
    uint32_t point_capacity; /* 路线采样点输出数组容量 */
    uint32_t point_count; /* 本次规划实际输出的路线采样点数量 */
    lv_offline_nav_turn_event_t *turn_events; /* 调用者提供的转向事件输出数组，允许为空 */
    uint16_t turn_event_capacity; /* 转向事件输出数组容量 */
    uint16_t turn_event_count; /* 本次规划实际输出的转向事件数量 */
    uint32_t total_distance_m; /* 本次规划路线总里程，单位为米 */
    lv_offline_nav_point_t center_point; /* 建议调用者用于居中地图的路线中段点 */
    bool has_center_point; /* true 表示 center_point 为有效路线点 */
} lv_offline_nav_route_t;

/*********************************************************
 * @brief 获取离线导航库默认配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_nav_get_default_config(lv_offline_nav_config_t *config);

/*********************************************************
 * @brief 清空路线输出状态但保留调用者提供的数组指针和容量
 * @param route 路线输出对象指针，允许为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_nav_clear_route(lv_offline_nav_route_t *route);

/*********************************************************
 * @brief 按起终点 E7 经纬度规划离线路线
 * @param config 导航库配置指针，允许为空，空值使用默认配置
 * @param start_lon_e7 起点经度 E7 定点值，单位为 1e-7 度
 * @param start_lat_e7 起点纬度 E7 定点值，单位为 1e-7 度
 * @param end_lon_e7 终点经度 E7 定点值，单位为 1e-7 度
 * @param end_lat_e7 终点纬度 E7 定点值，单位为 1e-7 度
 * @param route 输出路线对象，不能为空，points 和容量必须有效
 * @return bool true 表示路线规划成功，false 表示路网不可用、终点不可达或输出容量不足
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_nav_plan_route(const lv_offline_nav_config_t *config, int32_t start_lon_e7,
                               int32_t start_lat_e7, int32_t end_lon_e7, int32_t end_lat_e7,
                               lv_offline_nav_route_t *route);

/*********************************************************
 * @brief 根据已行驶里程查询当前路线导航提示
 * @param route 已规划成功的路线对象，不能为空
 * @param traveled_distance_m 当前已行驶里程，单位为米
 * @param guidance 输出导航提示状态，不能为空
 * @return bool true 表示存在有效导航路线，false 表示当前没有可用路线
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_nav_get_guidance(const lv_offline_nav_route_t *route, uint32_t traveled_distance_m,
                                 lv_offline_nav_guidance_t *guidance);

#ifdef __cplusplus
}
#endif

#endif
