/*********************************************************
 * @file lv_offline_map.h
 * @author ^^^^^^^ ()
 * @brief 提供可移植的 LVGL 离线瓦片地图控件接口
 * @version 1.0
 * @date 2026-06-18
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#ifndef LV_OFFLINE_MAP_H
#define LV_OFFLINE_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#  if __has_include(<lvgl/lvgl.h>)
#    include <lvgl/lvgl.h>
#  else
#    include "lvgl.h"
#  endif
#else
#  include "lvgl.h"
#endif

#define LV_OFFLINE_MAP_DEFAULT_TILE_SIZE 256 /* 默认离线瓦片边长，单位为像素 */
#define LV_OFFLINE_MAP_DEFAULT_VIEW_WIDTH 480 /* 默认地图控件可视宽度，单位为像素 */
#define LV_OFFLINE_MAP_DEFAULT_VIEW_HEIGHT 228 /* 默认地图控件可视高度，单位为像素 */
#define LV_OFFLINE_MAP_DEFAULT_TRACK_MAX_POINTS 1024 /* 默认轨迹缓存点数量上限 */
#define LV_OFFLINE_MAP_DEFAULT_ZOOM_ANIM_TIME_MS 180 /* 默认缩放切换动画时长，单位为毫秒 */

typedef struct {
    const char *map_dir; /* 离线瓦片根目录，目录结构为 map_dir/zoom/x/y/tile_file_name */
    const char *tile_file_name; /* 单张瓦片文件名，例如 tile.jpg */
    int tile_size; /* 单张瓦片边长，单位为像素，当前控件默认按 256 像素瓦片优化 */
    int view_width; /* 地图控件可视宽度，单位为像素 */
    int view_height; /* 地图控件可视高度，单位为像素 */
    int min_zoom; /* 允许访问的最小缩放级别 */
    int max_zoom; /* 允许访问的最大缩放级别 */
    int default_zoom; /* 创建后默认加载的缩放级别 */
    int default_tile_x; /* 创建后默认显示的中心瓦片 X 坐标 */
    int default_tile_y; /* 创建后默认显示的中心瓦片 Y 坐标 */
    int track_max_points; /* 轨迹缓存点数量上限，创建控件时分配固定容量 */
    bool use_gcj02_tile; /* true 表示把 WGS84 经纬度转换到 GCJ-02 后再贴合腾讯或高德瓦片 */
    bool show_zoom_controls; /* true 表示控件内置显示放大、缩小和缩放级别控件 */
    bool show_vehicle_marker; /* true 表示未配置方向图片时显示默认点状车标 */
    bool zoom_anim_enable; /* true 表示缩放级别切换时播放容器缩放过渡动画 */
    uint16_t zoom_anim_time_ms; /* 缩放级别切换动画时长，单位为毫秒，0 表示使用默认时长 */
    double follow_speed_kmh; /* 自动跟随轨迹的模拟速度，单位为千米每小时 */
    const void *arrow_src; /* 车辆方向标图片资源，允许为空，为空时不显示方向标 */
    int arrow_pivot_x; /* 方向标图片旋转中心横向像素坐标 */
    int arrow_pivot_y; /* 方向标图片旋转中心纵向像素坐标 */
    lv_coord_t arrow_y_offset; /* 方向标相对地图控件中心的纵向偏移，单位为像素 */
    const lv_font_t *zoom_button_font; /* 缩放按钮文本字体，允许为空，空值使用 LVGL 默认字体 */
    const lv_font_t *zoom_label_font; /* 缩放级别标签字体，允许为空，空值使用 LVGL 默认字体 */
} lv_offline_map_config_t;

typedef struct {
    uint32_t traveled_distance_m; /* 实时定位投影到路线后的已行驶里程，单位为米 */
    uint32_t remaining_distance_m; /* 实时定位投影到路线后的剩余里程，单位为米 */
    uint32_t deviation_distance_m; /* 实时定位点到路线投影点的偏离距离，单位为米 */
    uint16_t segment_index; /* 投影点所在路线段起点索引，用于调试或外部状态同步 */
} lv_offline_map_location_result_t;

/*********************************************************
 * @brief 获取离线地图控件默认配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_get_default_config(lv_offline_map_config_t *config);

/*********************************************************
 * @brief 创建使用默认配置的离线地图控件
 * @param parent 父对象指针，不能为空
 * @return lv_obj_t* 创建成功的离线地图控件对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *lv_offline_map_create(lv_obj_t *parent);

/*********************************************************
 * @brief 创建使用指定配置的离线地图控件
 * @param parent 父对象指针，不能为空
 * @param config 控件配置指针，允许为空，空值使用默认配置
 * @return lv_obj_t* 创建成功的离线地图控件对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *lv_offline_map_create_with_config(lv_obj_t *parent, const lv_offline_map_config_t *config);

/*********************************************************
 * @brief 重新设置离线地图控件配置并刷新瓦片
 * @param map 离线地图控件对象，不能为空
 * @param config 新配置指针，不能为空
 * @return bool true 表示配置已生效，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_set_config(lv_obj_t *map, const lv_offline_map_config_t *config);

/*********************************************************
 * @brief 获取离线地图控件内部滚动容器
 * @param map 离线地图控件对象，不能为空
 * @return lv_obj_t* 地图滚动容器对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *lv_offline_map_get_container(lv_obj_t *map);

/*********************************************************
 * @brief 刷新当前中心瓦片、轨迹和缩放控件显示
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_refresh(lv_obj_t *map);

/*********************************************************
 * @brief 按瓦片坐标居中地图
 * @param map 离线地图控件对象，不能为空
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_center_tile(lv_obj_t *map, int tile_x, int tile_y, int zoom);

/*********************************************************
 * @brief 按 E7 经纬度居中地图
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示地图已居中，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_center_lonlat_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom);

/*********************************************************
 * @brief 设置车辆当前位置车标但不要求存在导航路线
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 车辆经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 车辆纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param center true 表示立即把地图视野对齐到车标位置，false 表示只刷新车标位置
 * @return bool true 表示车标位置已更新，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_set_vehicle_location_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom, bool center);

/*********************************************************
 * @brief 将地图视窗内坐标反算为 WGS84 E7 经纬度
 * @param map 离线地图控件对象，不能为空
 * @param view_x 视窗内横向坐标，单位为像素，原点为地图控件左上角
 * @param view_y 视窗内纵向坐标，单位为像素，原点为地图控件左上角
 * @param lon_e7 输出 WGS84 经度 E7 定点值，不能为空
 * @param lat_e7 输出 WGS84 纬度 E7 定点值，不能为空
 * @return bool true 表示反算成功，false 表示参数无效或坐标不在地图视窗内
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_view_point_to_lonlat_e7(lv_obj_t *map, lv_coord_t view_x, lv_coord_t view_y,
                                            int *lon_e7, int *lat_e7);

/*********************************************************
 * @brief 将 WGS84 E7 经纬度换算为当前地图视窗内坐标
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param view_x 输出视窗内横向坐标，允许超出当前可视范围，不能为空
 * @param view_y 输出视窗内纵向坐标，允许超出当前可视范围，不能为空
 * @return bool true 表示换算成功，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_lonlat_e7_to_view_point(lv_obj_t *map, int lon_e7, int lat_e7,
                                            lv_coord_t *view_x, lv_coord_t *view_y);

/*********************************************************
 * @brief 滚动地图到当前中心瓦片内指定像素
 * @param map 离线地图控件对象，不能为空
 * @param pixel_x 中心瓦片内横向像素坐标
 * @param pixel_y 中心瓦片内纵向像素坐标
 * @param anim 是否启用 LVGL 滚动动画
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_scroll_to_tile_pixel(lv_obj_t *map, int pixel_x, int pixel_y, lv_anim_enable_t anim);

/*********************************************************
 * @brief 设置地图缩放级别并保持当前屏幕中心地理位置
 * @param map 离线地图控件对象，不能为空
 * @param zoom 目标缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_set_zoom(lv_obj_t *map, int zoom);

/*********************************************************
 * @brief 地图放大一级
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_zoom_in(lv_obj_t *map);

/*********************************************************
 * @brief 地图缩小一级
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_zoom_out(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前地图缩放级别
 * @param map 离线地图控件对象，不能为空
 * @return int 当前缩放级别，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
int lv_offline_map_get_zoom(lv_obj_t *map);

/*********************************************************
 * @brief 设置缩放切换动画开关和时长
 * @param map 离线地图控件对象，不能为空
 * @param enable true 表示启用缩放切换动画，false 表示关闭
 * @param anim_time_ms 动画时长，单位为毫秒，0 表示使用默认时长
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_set_zoom_anim(lv_obj_t *map, bool enable, uint16_t anim_time_ms);

/*********************************************************
 * @brief 清空当前轨迹点
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_clear(lv_obj_t *map);

/*********************************************************
 * @brief 开始批量写入轨迹点并暂缓刷新
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_begin_batch(lv_obj_t *map);

/*********************************************************
 * @brief 结束批量写入轨迹点并统一刷新
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_end_batch(lv_obj_t *map);

/*********************************************************
 * @brief 添加基于当前中心瓦片的轨迹点
 * @param map 离线地图控件对象，不能为空
 * @param pixel_x 轨迹点在中心瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在中心瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示参数无效或控件未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_add_point(lv_obj_t *map, int pixel_x, int pixel_y);

/*********************************************************
 * @brief 添加基于瓦片坐标的轨迹点
 * @param map 离线地图控件对象，不能为空
 * @param tile_x 轨迹点所在瓦片 X 坐标
 * @param tile_y 轨迹点所在瓦片 Y 坐标
 * @param pixel_x 轨迹点在瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示参数无效或控件未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_add_tile_point(lv_obj_t *map, int tile_x, int tile_y, int pixel_x, int pixel_y);

/*********************************************************
 * @brief 按 E7 经纬度添加轨迹点
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示轨迹点已写入，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_add_lonlat_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom);

/*********************************************************
 * @brief 按 E7 经纬度添加带累计里程的轨迹点
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param distance_m 轨迹点距起点的累计里程，单位为米
 * @return bool true 表示轨迹点已写入，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_add_lonlat_e7_with_distance(lv_obj_t *map, int lon_e7, int lat_e7, int zoom,
                                                      uint32_t distance_m);

/*********************************************************
 * @brief 使用实时定位经纬度更新当前导航跟随位置
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 实时定位经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 实时定位纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param result 输出定位投影结果，允许为空
 * @return bool true 表示定位已投影到当前路线并更新车标，false 表示参数无效、无路线或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_update_location_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom,
                                             lv_offline_map_location_result_t *result);

/*********************************************************
 * @brief 获取当前虚拟或实时跟随位置对应的 WGS84 E7 经纬度
 * @param map 离线地图控件对象，不能为空
 * @param lon_e7 输出当前跟随经度 E7 定点值，允许为空
 * @param lat_e7 输出当前跟随纬度 E7 定点值，允许为空
 * @return bool true 表示存在可用跟随位置，false 表示当前没有车辆位置
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_map_track_get_follow_location_e7(lv_obj_t *map, int *lon_e7, int *lat_e7);

/*********************************************************
 * @brief 启动轨迹自动跟随播放
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_start_follow(lv_obj_t *map);

/*********************************************************
 * @brief 停止轨迹自动跟随播放
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_stop_follow(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前轨迹点数量
 * @param map 离线地图控件对象，不能为空
 * @return uint16_t 当前轨迹点数量，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint16_t lv_offline_map_track_get_point_count(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前已行驶轨迹点索引
 * @param map 离线地图控件对象，不能为空
 * @return uint16_t 当前已行驶轨迹点索引，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint16_t lv_offline_map_track_get_travel_index(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前轨迹总里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹总里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_total_distance_m(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前轨迹已行驶里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹已行驶里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_traveled_distance_m(lv_obj_t *map);

/*********************************************************
 * @brief 获取当前轨迹剩余里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹剩余里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_remaining_distance_m(lv_obj_t *map);

#ifdef __cplusplus
}
#endif

#endif /* LV_OFFLINE_MAP_H */
