/*********************************************************
 * @file ui.h
 * @author ^^^^^^^ ()
 * @brief 声明独立离线地图演示的页面、地图和导航接口
 * @version 1.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#ifndef MAP_DEMO_UI_H
#define MAP_DEMO_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "lv_offline_nav.h"

typedef struct {
    uint32_t traveled_distance_m; /* 实时定位投影到路线后的已行驶里程，单位为米 */
    uint32_t remaining_distance_m; /* 实时定位投影到路线后的剩余里程，单位为米 */
    uint32_t deviation_distance_m; /* 实时定位点到路线投影点的偏离距离，单位为米 */
    uint16_t segment_index; /* 投影点所在路线段起点索引，用于调试或外部状态同步 */
} ui_nav_location_result_t;

LV_IMAGE_DECLARE(ui_img_nav_left_png);
LV_IMAGE_DECLARE(ui_img_nav_right_png);
LV_IMAGE_DECLARE(ui_img_nav_straight_png);
LV_IMAGE_DECLARE(ui_img_nav_uturn_png);

/*********************************************************
 * @brief 初始化独立离线地图演示界面
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_init(void);

/*********************************************************
 * @brief 创建离线地图页面
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_Map_screen_init(void);

/*********************************************************
 * @brief 处理离线地图页面事件
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_event_MapScreen(lv_event_t *e);

/*********************************************************
 * @brief 获取离线地图页面对象
 * @return lv_obj_t* 地图页面对象，未初始化时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *ui_mapGetScreen(void);

/*********************************************************
 * @brief 切换地图中心瓦片并刷新显示
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_changeMap(int tile_x, int tile_y, int zoom);

/*********************************************************
 * @brief 按 E7 经纬度居中离线地图视野
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示地图已居中，false 表示地图未初始化或换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapCenterLonLatE7(int lon_e7, int lat_e7, int zoom);

/*********************************************************
 * @brief 获取当前离线地图缩放级别
 * @return int 当前地图缩放级别，地图未初始化时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
int ui_mapGetZoom(void);

/*********************************************************
 * @brief 滚动地图到中心瓦片内指定像素位置
 * @param x 中心瓦片内的横向像素坐标
 * @param y 中心瓦片内的纵向像素坐标
 * @param anim 是否启用 LVGL 滚动动画
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_scrollMap(int x, int y, lv_anim_enable_t anim);

/*********************************************************
 * @brief 清空当前轨迹记录
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackClear(void);

/*********************************************************
 * @brief 开始批量写入轨迹点并暂缓刷新轨迹图层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackBeginBatch(void);

/*********************************************************
 * @brief 结束批量写入轨迹点并统一刷新轨迹图层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackEndBatch(void);

/*********************************************************
 * @brief 启动轨迹自动跟随播放
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackStartFollow(void);

/*********************************************************
 * @brief 停止轨迹自动跟随播放
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackStopFollow(void);

/*********************************************************
 * @brief 设置地图页面是否处于导航显示态
 * @param active true 表示显示路线提示和剩余里程，false 表示隐藏导航叠层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapSetNavigationActive(bool active);

/*********************************************************
 * @brief 在当前中心瓦片内添加轨迹点
 * @param pixel_x 轨迹点在中心瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在中心瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddPoint(int pixel_x, int pixel_y);

/*********************************************************
 * @brief 按瓦片坐标添加轨迹点
 * @param tile_x 轨迹点所在瓦片 X 坐标
 * @param tile_y 轨迹点所在瓦片 Y 坐标
 * @param pixel_x 轨迹点在瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddTilePoint(int tile_x, int tile_y, int pixel_x, int pixel_y);

/*********************************************************
 * @brief 按 E7 经纬度添加轨迹点
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化或换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddLonLatE7(int lon_e7, int lat_e7, int zoom);

/*********************************************************
 * @brief 按 E7 经纬度添加带累计里程的地图轨迹点
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param distance_m 轨迹点距起点的累计里程，单位为米
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化或换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddLonLatE7WithDistance(int lon_e7, int lat_e7, int zoom, uint32_t distance_m);

/*********************************************************
 * @brief 使用实时定位经纬度更新地图导航车标位置
 * @param lon_e7 实时定位经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 实时定位纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param result 输出定位投影结果，允许为空
 * @return bool true 表示定位已投影到当前路线并更新车标，false 表示地图未初始化、无路线或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapUpdateLocationE7(int lon_e7, int lat_e7, int zoom, ui_nav_location_result_t *result);

/*********************************************************
 * @brief 仅刷新地图车辆车标位置，不要求存在导航路线
 * @param lon_e7 车辆经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 车辆纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param center true 表示立即把地图视野对齐到车标位置，false 表示只更新车标
 * @return bool true 表示车标位置已更新，false 表示地图未初始化或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapSetVehicleLocationE7(int lon_e7, int lat_e7, int zoom, bool center);

/*********************************************************
 * @brief 获取当前地图虚拟车辆位置对应的 WGS84 E7 经纬度
 * @param lon_e7 输出虚拟车辆经度 E7 定点值，允许为空
 * @param lat_e7 输出虚拟车辆纬度 E7 定点值，允许为空
 * @return bool true 表示存在可用于路线规划的虚拟车辆位置，false 表示当前没有虚拟位置
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapGetVirtualLocationE7(int *lon_e7, int *lat_e7);

/*********************************************************
 * @brief 写入默认演示轨迹
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapSeedDemoTrack(void);

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
bool ui_navPlanRoute(int start_lon_e7, int start_lat_e7, int end_lon_e7, int end_lat_e7);

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
bool ui_navPlanRouteFromCurrentLocation(int end_lon_e7, int end_lat_e7);

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
bool ui_navGetCurrentLocationE7(int *lon_e7, int *lat_e7);

/*********************************************************
 * @brief 规划一条位于当前离线地图范围内的演示导航路线
 * @return bool true 表示演示路线规划成功，false 表示路网不可用或示例终点不可达
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_navPlanDemoRoute(void);

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
bool ui_navUpdateLocationE7(int lon_e7, int lat_e7, ui_nav_location_result_t *result);

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
bool ui_navGetGuidance(uint32_t traveled_distance_m, lv_offline_nav_guidance_t *guidance);

#ifdef __cplusplus
}
#endif

#endif /* MAP_DEMO_UI_H */
