/*********************************************************
 * @file lv_offline_map.c
 * @author ^^^^^^^ ()
 * @brief 实现可移植的 LVGL 离线瓦片地图控件
 * @version 1.0
 * @date 2026-06-18
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#include "lv_offline_map.h"

#include <lvgl/src/core/lv_obj_class_private.h>
#include <lvgl/src/core/lv_obj_private.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LV_OFFLINE_MAP_TILE_COL_NUM 5 /* 横向瓦片矩阵数量，提供左右拖动缓冲并保持固定显示区域 */
#define LV_OFFLINE_MAP_TILE_ROW_NUM 5 /* 纵向瓦片矩阵数量，提供上下拖动缓冲并保持固定显示区域 */
#define LV_OFFLINE_MAP_CENTER_TILE_COL 2 /* 中心瓦片在矩阵中的列索引 */
#define LV_OFFLINE_MAP_CENTER_TILE_ROW 2 /* 中心瓦片在矩阵中的行索引 */
#define LV_OFFLINE_MAP_DEFAULT_MIN_ZOOM 15 /* 默认最小缩放级别 */
#define LV_OFFLINE_MAP_DEFAULT_MAX_ZOOM 17 /* 默认最大缩放级别 */
#define LV_OFFLINE_MAP_DEFAULT_ZOOM 17 /* 默认启动缩放级别 */
#define LV_OFFLINE_MAP_DEFAULT_TILE_X 107073 /* 默认中心瓦片 X 坐标，匹配当前示例地图资源 */
#define LV_OFFLINE_MAP_DEFAULT_TILE_Y 53161 /* 默认中心瓦片 Y 坐标，匹配当前示例地图资源 */
#define LV_OFFLINE_MAP_TRACK_DRAW_EXTRA_POINTS 1 /* 播放中折线额外插入当前插值位置所需点数 */
#define LV_OFFLINE_MAP_TRACK_WIDTH 5 /* 轨迹线宽，单位为像素 */
#define LV_OFFLINE_MAP_TRACK_ACTIVE_COLOR 0x00E676 /* 未行驶轨迹颜色 */
#define LV_OFFLINE_MAP_TRACK_PASSED_COLOR 0xB7DCC2 /* 已行驶轨迹颜色 */
#define LV_OFFLINE_MAP_TRACK_END_COLOR 0xFF3D00 /* 终点标记颜色 */
#define LV_OFFLINE_MAP_TRACK_POINT_SIZE 9 /* 起终点标记边长，单位为像素 */
#define LV_OFFLINE_MAP_DEFAULT_DIR "L:/sdcard/map/" /* 默认离线瓦片目录 */
#define LV_OFFLINE_MAP_DEFAULT_TILE_FILE "tile.jpg" /* 默认离线瓦片文件名 */
#define LV_OFFLINE_MAP_LON_LAT_SCALE 10000000.0 /* E7 经纬度转换成十进制度的比例 */
#define LV_OFFLINE_MAP_PI 3.14159265358979323846 /* Web Mercator 和 GCJ-02 换算使用的圆周率 */
#define LV_OFFLINE_MAP_MAX_MERCATOR_LAT 85.05112878 /* Web Mercator 支持的最大纬度 */
#define LV_OFFLINE_MAP_GCJ_A 6378245.0 /* GCJ-02 转换使用的 Krasovsky 椭球长半轴 */
#define LV_OFFLINE_MAP_GCJ_EE 0.00669342162296594323 /* GCJ-02 转换使用的第一偏心率平方 */
#define LV_OFFLINE_MAP_GCJ_MIN_LON 72.004 /* GCJ-02 中国区域判断最小经度 */
#define LV_OFFLINE_MAP_GCJ_MAX_LON 137.8347 /* GCJ-02 中国区域判断最大经度 */
#define LV_OFFLINE_MAP_GCJ_MIN_LAT 0.8293 /* GCJ-02 中国区域判断最小纬度 */
#define LV_OFFLINE_MAP_GCJ_MAX_LAT 55.8271 /* GCJ-02 中国区域判断最大纬度 */
#define LV_OFFLINE_MAP_E7_ROUND_OFFSET 0.5 /* 十进制度转 E7 整数时使用的四舍五入偏移 */
#define LV_OFFLINE_MAP_ZOOM_BUTTON_SIZE 34 /* 缩放按钮边长，单位为像素 */
#define LV_OFFLINE_MAP_ZOOM_BUTTON_X (-10) /* 缩放控件相对右侧的横向偏移 */
#define LV_OFFLINE_MAP_ZOOM_IN_Y 34 /* 放大按钮相对顶部的纵向偏移 */
#define LV_OFFLINE_MAP_ZOOM_LABEL_Y 72 /* 缩放级别标签相对顶部的纵向偏移 */
#define LV_OFFLINE_MAP_ZOOM_OUT_Y 98 /* 缩小按钮相对顶部的纵向偏移 */
#define LV_OFFLINE_MAP_ZOOM_ANIM_SCALE_STEP 32 /* 单级缩放切换动画的起始缩放差值，256 表示无缩放 */
#define LV_OFFLINE_MAP_FOLLOW_PERIOD_MS 20 /* 自动跟随刷新周期，单位为毫秒 */
#define LV_OFFLINE_MAP_DEFAULT_FOLLOW_SPEED_KMH 50.0 /* 默认自动跟随速度，单位为千米每小时 */
#define LV_OFFLINE_MAP_FOLLOW_MAX_ELAPSED_MS 80U /* 单次推进允许补偿的最大耗时，避免卡顿后大幅跳变 */
#define LV_OFFLINE_MAP_TRACK_FOLLOW_REFRESH_MS 40U /* 自动跟随时轨迹重绘节流周期，单位为毫秒 */
#define LV_OFFLINE_MAP_AUTO_CENTER_IDLE_MS 5000U /* 用户停止拖图后恢复车标自动居中的等待时间，单位为毫秒 */
#define LV_OFFLINE_MAP_AUTO_CENTER_CHECK_MS 200U /* 检查用户拖图空闲状态的定时器周期，单位为毫秒 */
#define LV_OFFLINE_MAP_EARTH_RADIUS_M 6378137.0 /* Web Mercator 地球半径，单位为米 */
#define LV_OFFLINE_MAP_MAX_PATH_LEN 128 /* 单张瓦片文件路径缓存长度 */
#define LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE 34 /* 默认点状车标外层容器边长，单位为像素 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE 14 /* 默认车标光圈呼吸动画起始直径，单位为像素 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_MAX_SIZE 34 /* 默认车标光圈呼吸动画最大扩散直径，单位为像素 */
#define LV_OFFLINE_MAP_VEHICLE_DOT_SIZE 12 /* 默认车标中心实心点直径，单位为像素 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_COLOR 0x2196F3 /* 默认车标扩散光圈颜色 */
#define LV_OFFLINE_MAP_VEHICLE_DOT_COLOR 0x0D6EFD /* 默认车标中心点颜色 */
#define LV_OFFLINE_MAP_VEHICLE_DOT_BORDER_COLOR 0xFFFFFF /* 默认车标中心点描边颜色 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_START_OPA 120 /* 默认车标光圈起始透明度 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_END_OPA 12 /* 默认车标光圈扩散末端透明度 */
#define LV_OFFLINE_MAP_VEHICLE_HALO_ANIM_MS 1100 /* 默认车标光圈单次扩散动画时长，单位为毫秒 */
#define LV_OFFLINE_MAP_CLASS (&lv_offline_map_class) /* 离线地图 LVGL 类对象访问宏 */

#if LVGL_VERSION_MAJOR >= 9
typedef lv_point_precise_t lv_offline_map_point_t;
#define LV_OFFLINE_MAP_IMAGE_CREATE(parent) lv_image_create(parent) /* 创建图片对象，LVGL 9 使用 image 控件 */
#define LV_OFFLINE_MAP_IMAGE_SET_SRC(obj, src) lv_image_set_src((obj), (src)) /* 设置图片资源，LVGL 9 使用 image API */
#define LV_OFFLINE_MAP_IMAGE_SET_ROTATION(obj, angle) lv_image_set_rotation((obj), (angle)) /* 设置图片旋转角度，单位为 0.1 度 */
#define LV_OFFLINE_MAP_IMAGE_SET_PIVOT(obj, x, y) lv_image_set_pivot((obj), (x), (y)) /* 设置图片旋转中心 */
#define LV_OFFLINE_MAP_OBJ_SET_TRANSFORM_SCALE(obj, value) lv_obj_set_style_transform_scale((obj), (value), LV_PART_MAIN | LV_STATE_DEFAULT) /* 设置对象缩放比例，LVGL 9 使用 transform scale */
#define LV_OFFLINE_MAP_SCALE_NONE LV_SCALE_NONE /* LVGL 9 中对象无缩放的比例值 */
#define LV_OFFLINE_MAP_ANIM_DELETE(var, exec_cb) lv_anim_delete((var), (exec_cb)) /* 删除指定对象动画，LVGL 9 使用 anim_delete API */
#define LV_OFFLINE_MAP_BUTTON_CREATE(parent) lv_button_create(parent) /* 创建按钮对象，LVGL 9 使用 button 控件 */
#define LV_OFFLINE_MAP_OBJ_DELETE(obj) lv_obj_delete(obj) /* 删除 LVGL 对象，LVGL 9 使用 delete API */
#define LV_OFFLINE_MAP_TIMER_DELETE(timer) lv_timer_delete(timer) /* 删除 LVGL 定时器，LVGL 9 使用 timer_delete API */
#define LV_OFFLINE_MAP_OBJ_SEND_EVENT(obj, code, param) lv_obj_send_event((obj), (code), (param)) /* 向对象发送事件，LVGL 9 使用 obj_send_event API */
#else
typedef lv_point_t lv_offline_map_point_t;
#define LV_OFFLINE_MAP_IMAGE_CREATE(parent) lv_img_create(parent) /* 创建图片对象，LVGL 8 使用 img 控件 */
#define LV_OFFLINE_MAP_IMAGE_SET_SRC(obj, src) lv_img_set_src((obj), (src)) /* 设置图片资源，LVGL 8 使用 img API */
#define LV_OFFLINE_MAP_IMAGE_SET_ROTATION(obj, angle) lv_img_set_angle((obj), (angle)) /* 设置图片旋转角度，单位为 0.1 度 */
#define LV_OFFLINE_MAP_IMAGE_SET_PIVOT(obj, x, y) lv_img_set_pivot((obj), (x), (y)) /* 设置图片旋转中心 */
#define LV_OFFLINE_MAP_OBJ_SET_TRANSFORM_SCALE(obj, value) lv_obj_set_style_transform_zoom((obj), (value), LV_PART_MAIN | LV_STATE_DEFAULT) /* 设置对象缩放比例，LVGL 8 使用 transform zoom */
#define LV_OFFLINE_MAP_SCALE_NONE LV_IMG_ZOOM_NONE /* LVGL 8 中对象无缩放的比例值 */
#define LV_OFFLINE_MAP_ANIM_DELETE(var, exec_cb) lv_anim_del((var), (exec_cb)) /* 删除指定对象动画，LVGL 8 使用 anim_del API */
#define LV_OFFLINE_MAP_BUTTON_CREATE(parent) lv_btn_create(parent) /* 创建按钮对象，LVGL 8 使用 btn 控件 */
#define LV_OFFLINE_MAP_OBJ_DELETE(obj) lv_obj_del(obj) /* 删除 LVGL 对象，LVGL 8 使用 del API */
#define LV_OFFLINE_MAP_TIMER_DELETE(timer) lv_timer_del(timer) /* 删除 LVGL 定时器，LVGL 8 使用 timer_del API */
#define LV_OFFLINE_MAP_OBJ_SEND_EVENT(obj, code, param) lv_event_send((obj), (code), (param)) /* 向对象发送事件，LVGL 8 使用 event_send API */
#endif

typedef struct {
    int tile_x; /* 轨迹点所在瓦片 X 坐标，用于中心变化后重新换算 */
    int tile_y; /* 轨迹点所在瓦片 Y 坐标，用于中心变化后重新换算 */
    int pixel_x; /* 轨迹点在瓦片内的横向像素坐标 */
    int pixel_y; /* 轨迹点在瓦片内的纵向像素坐标 */
    int zoom; /* 轨迹点所属缩放级别，用于地图缩放后换算 */
    bool has_distance_m; /* true 表示该轨迹点使用调用方提供的累计里程 */
    uint32_t distance_m; /* 调用方提供的轨迹累计里程，单位为米 */
} lv_offline_map_track_point_t;

typedef struct {
    lv_obj_t obj; /* LVGL 基类对象，必须放在首成员以支持 lv_obj_t 与控件状态互转 */
    lv_offline_map_config_t config; /* 控件配置副本，调用方原始配置释放后仍可继续使用 */
    lv_obj_t *root; /* 离线地图控件根对象，负责承载滚动容器、方向标和缩放控件 */
    lv_obj_t *container; /* 可滚动瓦片容器，承载瓦片图片和轨迹图层 */
    lv_obj_t *content_layer; /* 地图内容层，承载瓦片和轨迹并在缩放动画中单独变换 */
    lv_obj_t *passed_track_line; /* 已行驶轨迹折线对象 */
    lv_obj_t *active_track_line; /* 未行驶轨迹折线对象 */
    lv_obj_t *start_point; /* 轨迹起点标记对象 */
    lv_obj_t *end_point; /* 轨迹终点标记对象 */
    lv_obj_t *arrow; /* 车辆当前位置车标对象，可为方向图片或默认点状车标容器 */
    lv_obj_t *vehicle_halo; /* 默认点状车标的扩散光圈对象，生命周期跟随 arrow */
    lv_obj_t *vehicle_dot; /* 默认点状车标的中心实心点对象，生命周期跟随 arrow */
    lv_obj_t *zoom_in_button; /* 放大按钮对象 */
    lv_obj_t *zoom_out_button; /* 缩小按钮对象 */
    lv_obj_t *zoom_label; /* 当前缩放级别标签对象 */
    lv_obj_t *tile[LV_OFFLINE_MAP_TILE_ROW_NUM][LV_OFFLINE_MAP_TILE_COL_NUM]; /* 固定瓦片图片对象矩阵 */
    char tile_path[LV_OFFLINE_MAP_TILE_ROW_NUM][LV_OFFLINE_MAP_TILE_COL_NUM][LV_OFFLINE_MAP_MAX_PATH_LEN]; /* 每个瓦片对象对应的文件路径缓存，保证图片源字符串生命周期有效 */
    lv_offline_map_track_point_t *track_records; /* 原始轨迹点数组，坐标不随显示中心变化 */
    uint32_t *track_distance_m; /* 轨迹累计里程数组，索引对应轨迹点，单位为米 */
    lv_offline_map_point_t *track_points; /* 当前瓦片容器坐标下的完整轨迹点数组 */
    lv_offline_map_point_t *passed_track_points; /* 已行驶轨迹绘制点数组，播放中包含当前插值位置 */
    lv_offline_map_point_t *active_track_points; /* 未行驶轨迹绘制点数组，播放中从当前插值位置开始 */
    uint16_t track_capacity; /* 当前轨迹缓存容量，创建控件时固定 */
    uint16_t track_draw_capacity; /* 绘制轨迹缓存容量，通常为 track_capacity + 1 */
    uint16_t track_point_count; /* 当前有效轨迹点数量 */
    uint32_t track_total_distance_m; /* 当前轨迹总里程，单位为米 */
    uint32_t track_traveled_distance_m; /* 当前自动跟随已行驶里程，单位为米 */
    int center_tile_x; /* 当前中心瓦片 X 坐标 */
    int center_tile_y; /* 当前中心瓦片 Y 坐标 */
    int zoom; /* 当前地图缩放级别 */
    bool tile_switching; /* 瓦片切换保护标志，避免 scroll_to 递归触发重载 */
    bool track_batch_updating; /* 批量写轨迹保护标志，用于延迟刷新折线 */
    uint16_t track_travel_index; /* 当前已行驶到的轨迹点索引 */
    uint16_t follow_segment_index; /* 自动跟随所在路线段起点索引 */
    int follow_global_x; /* 当前跟随位置全局横向像素坐标 */
    int follow_global_y; /* 当前跟随位置全局纵向像素坐标 */
    double follow_precise_x; /* 当前跟随位置双精度全局横向像素坐标 */
    double follow_precise_y; /* 当前跟随位置双精度全局纵向像素坐标 */
    bool follow_position_valid; /* true 表示当前跟随位置可用于实时定位或自动播放的轨迹分段 */
    bool follow_auto_center; /* true 表示自动跟随时允许地图视野随车辆位置居中，用户拖图后会关闭 */
    uint32_t follow_user_scroll_tick; /* 最近一次用户拖动地图的系统 tick，用于 5 秒后恢复居中 */
    uint32_t follow_last_tick; /* 上一次自动跟随推进时的系统 tick */
    uint32_t follow_last_refresh_tick; /* 上一次刷新跟随轨迹折线时的系统 tick */
    int arrow_rotation; /* 当前方向标旋转角度，单位为 0.1 度 */
    lv_timer_t *follow_timer; /* 自动跟随定时器，空值表示未播放 */
    lv_timer_t *auto_center_timer; /* 用户停止拖图后恢复车标居中的定时器，空值表示未等待恢复 */
} lv_offline_map_state_t;

/*********************************************************
 * @brief 构造新的离线地图控件对象
 * @param class_p LVGL 类描述指针，不能为空
 * @param obj 正在初始化的离线地图对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj);

/*********************************************************
 * @brief 销毁离线地图控件对象并释放动态资源
 * @param class_p LVGL 类描述指针，不能为空
 * @param obj 正在销毁的离线地图对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj);

/*********************************************************
 * @brief 处理离线地图控件类事件
 * @param class_p LVGL 类描述指针，不能为空
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_class_event(const lv_obj_class_t *class_p, lv_event_t *e);

static const lv_obj_class_t lv_offline_map_class = {
    .constructor_cb = lv_offline_map_constructor,
    .destructor_cb = lv_offline_map_destructor,
    .event_cb = lv_offline_map_class_event,
    .width_def = LV_OFFLINE_MAP_DEFAULT_VIEW_WIDTH,
    .height_def = LV_OFFLINE_MAP_DEFAULT_VIEW_HEIGHT,
    .base_class = &lv_obj_class,
    .instance_size = sizeof(lv_offline_map_state_t),
#if LVGL_VERSION_MAJOR > 8
    .name = "offline_map",
#endif
}; /* 离线地图控件类描述，定义对象实例大小、生命周期回调和事件处理入口 */

/*********************************************************
 * @brief 根据地图控件对象获取私有状态
 * @param map 离线地图控件对象，允许为空
 * @return lv_offline_map_state_t* 私有状态指针，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_offline_map_state_t *lv_offline_map_get_state(lv_obj_t *map);

/*********************************************************
 * @brief 执行默认车标光圈的扩散呼吸动画
 * @param var 动画绑定的光圈对象，必须为 LVGL 对象
 * @param value 当前动画进度，范围由 LVGL 动画系统传入
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_vehicle_halo_anim_exec_cb(void *var, int32_t value);

/*********************************************************
 * @brief 创建默认点状车辆车标并启动光圈呼吸动画
 * @param state 控件私有状态，不能为空
 * @return lv_obj_t* 创建成功的车标容器对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_obj_t *lv_offline_map_create_vehicle_marker(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 获取车标地理坐标在对象内对应的锚点
 * @param state 控件私有状态，不能为空
 * @param pivot_x 输出横向锚点，不能为空
 * @param pivot_y 输出纵向锚点，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_get_vehicle_pivot(lv_offline_map_state_t *state, lv_coord_t *pivot_x,
                                             lv_coord_t *pivot_y);

/*********************************************************
 * @brief 刷新轨迹折线显示
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_refresh_track(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 加载指定中心瓦片周围的固定离线瓦片矩阵
 * @param state 控件私有状态，不能为空
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_load_tiles(lv_offline_map_state_t *state, int tile_x, int tile_y, int zoom);

/*********************************************************
 * @brief 设置缩放动画轴心到当前地图锚点
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_set_zoom_animation_pivot(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 执行缩放切换动画的缩放值更新
 * @param var 动画绑定对象，必须为地图内容层对象
 * @param value 当前缩放比例，256 表示原始尺寸
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_zoom_anim_exec_cb(void *var, int32_t value);

/*********************************************************
 * @brief 根据缩放方向启动地图内容层过渡动画
 * @param state 控件私有状态，不能为空
 * @param previous_zoom 切换前缩放级别
 * @param next_zoom 切换后缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_start_zoom_animation(lv_offline_map_state_t *state, int previous_zoom, int next_zoom);

/*********************************************************
 * @brief 将单个瓦片对象更新到指定地图瓦片坐标
 * @param state 控件私有状态，不能为空
 * @param row 瓦片矩阵行索引
 * @param col 瓦片矩阵列索引
 * @param tile_x 地图瓦片 X 坐标
 * @param tile_y 地图瓦片 Y 坐标
 * @param zoom 地图瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_load_tile_cell(lv_offline_map_state_t *state, int row, int col, int tile_x, int tile_y,
                                          int zoom);

/*********************************************************
 * @brief 按瓦片增量复用矩阵并只加载进入边缘的新瓦片
 * @param state 控件私有状态，不能为空
 * @param tile_delta_x 中心瓦片 X 方向变化量
 * @param tile_delta_y 中心瓦片 Y 方向变化量
 * @return bool true 表示已增量加载，false 表示调用方需要全量加载
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_shift_tile_matrix(lv_offline_map_state_t *state, int tile_delta_x, int tile_delta_y);

/*********************************************************
 * @brief 把轨迹图层移动到瓦片图层前方
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_raise_track_layer(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 停止并删除自动跟随定时器
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_stop_track_follow(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 停止并删除用户空闲恢复居中的定时器
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_stop_auto_center_timer(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 处理用户拖图后的空闲恢复居中定时器
 * @param timer LVGL 定时器对象，user_data 指向控件私有状态
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_auto_center_timer_cb(lv_timer_t *timer);

/*********************************************************
 * @brief 记录用户拖动地图并延迟恢复车标居中
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_note_user_scroll(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 释放轨迹动态缓存并重置缓存指针
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_release_track_buffers(lv_offline_map_state_t *state);

/*********************************************************
 * @brief 处理地图控件内部交互事件
 * @param state 控件私有状态，不能为空
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_handle_event(lv_offline_map_state_t *state, lv_event_t *e);

/*********************************************************
 * @brief 将缩放级别限制在配置范围内
 * @param state 控件私有状态，不能为空
 * @param zoom 输入缩放级别
 * @return int 限幅后的缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int lv_offline_map_normalize_zoom(lv_offline_map_state_t *state, int zoom)
{
    int normalized_zoom = zoom; /* 限幅中的缩放级别，避免访问未打包瓦片目录 */

    if(normalized_zoom < state->config.min_zoom) {
        normalized_zoom = state->config.min_zoom;
    } else if(normalized_zoom > state->config.max_zoom) {
        normalized_zoom = state->config.max_zoom;
    }

    return normalized_zoom;
}

/*********************************************************
 * @brief 按缩放级别换算全局像素坐标
 * @param global_pixel 原缩放级别下的全局像素坐标
 * @param from_zoom 原始缩放级别
 * @param to_zoom 目标缩放级别
 * @return int 目标缩放级别下的全局像素坐标
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int lv_offline_map_scale_global_pixel(int global_pixel, int from_zoom, int to_zoom)
{
    int scaled_pixel = global_pixel; /* 换算中的全局像素坐标 */
    int current_zoom = from_zoom; /* 当前参与换算的缩放级别 */

    while(current_zoom < to_zoom) {
        scaled_pixel *= 2;
        current_zoom++;
    }

    while(current_zoom > to_zoom) {
        scaled_pixel /= 2;
        current_zoom--;
    }

    return scaled_pixel;
}

/*********************************************************
 * @brief 按缩放级别换算双精度全局像素坐标
 * @param global_pixel 原缩放级别下的双精度全局像素坐标
 * @param from_zoom 原始缩放级别
 * @param to_zoom 目标缩放级别
 * @return double 目标缩放级别下的双精度全局像素坐标
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static double lv_offline_map_scale_global_pixel_precise(double global_pixel, int from_zoom, int to_zoom)
{
    double scaled_pixel = global_pixel; /* 换算中的双精度全局像素坐标 */
    int current_zoom = from_zoom; /* 当前参与换算的缩放级别 */

    while(current_zoom < to_zoom) {
        scaled_pixel *= 2.0;
        current_zoom++;
    }

    while(current_zoom > to_zoom) {
        scaled_pixel /= 2.0;
        current_zoom--;
    }

    return scaled_pixel;
}

/*********************************************************
 * @brief 判断经纬度是否位于 GCJ-02 偏移区域
 * @param lon 十进制度经度
 * @param lat 十进制度纬度
 * @return bool true 表示需要 GCJ-02 偏移，false 表示保持原值
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_is_in_gcj_china(double lon, double lat)
{
    bool in_region; /* 中国区域粗略判断结果，用于决定是否执行 GCJ-02 转换 */

    in_region = (lon >= LV_OFFLINE_MAP_GCJ_MIN_LON && lon <= LV_OFFLINE_MAP_GCJ_MAX_LON &&
                 lat >= LV_OFFLINE_MAP_GCJ_MIN_LAT && lat <= LV_OFFLINE_MAP_GCJ_MAX_LAT);

    return in_region;
}

/*********************************************************
 * @brief 计算 GCJ-02 经度偏移中间量
 * @param x 相对基准经度 105 的偏移
 * @param y 相对基准纬度 35 的偏移
 * @return double 经度偏移中间计算值
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static double lv_offline_map_transform_gcj_lon(double x, double y)
{
    double ret; /* GCJ-02 经度偏移中间计算结果 */

    ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * LV_OFFLINE_MAP_PI) + 20.0 * sin(2.0 * x * LV_OFFLINE_MAP_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * LV_OFFLINE_MAP_PI) + 40.0 * sin(x / 3.0 * LV_OFFLINE_MAP_PI)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * LV_OFFLINE_MAP_PI) + 300.0 * sin(x / 30.0 * LV_OFFLINE_MAP_PI)) * 2.0 / 3.0;

    return ret;
}

/*********************************************************
 * @brief 计算 GCJ-02 纬度偏移中间量
 * @param x 相对基准经度 105 的偏移
 * @param y 相对基准纬度 35 的偏移
 * @return double 纬度偏移中间计算值
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static double lv_offline_map_transform_gcj_lat(double x, double y)
{
    double ret; /* GCJ-02 纬度偏移中间计算结果 */

    ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * LV_OFFLINE_MAP_PI) + 20.0 * sin(2.0 * x * LV_OFFLINE_MAP_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * LV_OFFLINE_MAP_PI) + 40.0 * sin(y / 3.0 * LV_OFFLINE_MAP_PI)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * LV_OFFLINE_MAP_PI) + 320.0 * sin(y * LV_OFFLINE_MAP_PI / 30.0)) * 2.0 / 3.0;

    return ret;
}

/*********************************************************
 * @brief 将 WGS84 经纬度转换为 GCJ-02 经纬度
 * @param lon 输入输出经度指针，不能为空
 * @param lat 输入输出纬度指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_wgs84_to_gcj02(double *lon, double *lat)
{
    double d_lat; /* GCJ-02 纬度偏移值 */
    double d_lon; /* GCJ-02 经度偏移值 */
    double rad_lat; /* WGS84 纬度对应的弧度值 */
    double magic; /* 椭球偏心率参与计算的中间量 */
    double sqrt_magic; /* magic 平方根，用于经纬度偏移归一化 */
    double source_lon; /* 转换前 WGS84 经度 */
    double source_lat; /* 转换前 WGS84 纬度 */

    if(lon == NULL || lat == NULL || !lv_offline_map_is_in_gcj_china(*lon, *lat)) {
        return;
    }

    source_lon = *lon;
    source_lat = *lat;
    d_lat = lv_offline_map_transform_gcj_lat(source_lon - 105.0, source_lat - 35.0);
    d_lon = lv_offline_map_transform_gcj_lon(source_lon - 105.0, source_lat - 35.0);
    rad_lat = source_lat / 180.0 * LV_OFFLINE_MAP_PI;
    magic = sin(rad_lat);
    magic = 1.0 - LV_OFFLINE_MAP_GCJ_EE * magic * magic;
    sqrt_magic = sqrt(magic);
    d_lat = (d_lat * 180.0) / ((LV_OFFLINE_MAP_GCJ_A * (1.0 - LV_OFFLINE_MAP_GCJ_EE)) / (magic * sqrt_magic) *
                               LV_OFFLINE_MAP_PI);
    d_lon = (d_lon * 180.0) / (LV_OFFLINE_MAP_GCJ_A / sqrt_magic * cos(rad_lat) * LV_OFFLINE_MAP_PI);
    *lat = source_lat + d_lat;
    *lon = source_lon + d_lon;
}

/*********************************************************
 * @brief 将 GCJ-02 经纬度近似反算为 WGS84 经纬度
 * @param lon 输入输出经度指针，不能为空
 * @param lat 输入输出纬度指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_gcj02_to_wgs84(double *lon, double *lat)
{
    double gcj_lon; /* 输入的 GCJ-02 经度，用于计算偏移后的误差 */
    double gcj_lat; /* 输入的 GCJ-02 纬度，用于计算偏移后的误差 */
    double probe_lon; /* 当前迭代探测的 WGS84 经度 */
    double probe_lat; /* 当前迭代探测的 WGS84 纬度 */
    double offset_lon; /* 探测坐标转回 GCJ-02 后相对输入经度的偏移 */
    double offset_lat; /* 探测坐标转回 GCJ-02 后相对输入纬度的偏移 */
    int iteration; /* GCJ-02 逆转换迭代次数 */

    if(lon == NULL || lat == NULL || !lv_offline_map_is_in_gcj_china(*lon, *lat)) {
        return;
    }

    gcj_lon = *lon;
    gcj_lat = *lat;
    probe_lon = gcj_lon;
    probe_lat = gcj_lat;
    for(iteration = 0; iteration < 2; iteration++) {
        offset_lon = probe_lon;
        offset_lat = probe_lat;
        lv_offline_map_wgs84_to_gcj02(&offset_lon, &offset_lat);
        offset_lon -= gcj_lon;
        offset_lat -= gcj_lat;
        probe_lon -= offset_lon;
        probe_lat -= offset_lat;
    }

    *lon = probe_lon;
    *lat = probe_lat;
}

/*********************************************************
 * @brief 将 Web Mercator 全局像素反算为 WGS84 E7 经纬度
 * @param state 控件私有状态，不能为空
 * @param global_x 当前缩放级别下全局横向像素坐标
 * @param global_y 当前缩放级别下全局纵向像素坐标
 * @param lon_e7 输出 WGS84 经度 E7 定点值，不能为空
 * @param lat_e7 输出 WGS84 纬度 E7 定点值，不能为空
 * @return bool true 表示反算成功，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_global_pixel_to_lonlat_e7(lv_offline_map_state_t *state, int global_x, int global_y,
                                                     int *lon_e7, int *lat_e7)
{
    double map_size; /* 当前缩放级别下世界地图像素宽度 */
    double lon; /* 由全局像素反算得到的十进制度经度 */
    double lat; /* 由全局像素反算得到的十进制度纬度 */
    double mercator_y; /* Web Mercator 反算纬度时使用的中间量 */
    double lon_scaled; /* 经度转换为 E7 前的缩放值 */
    double lat_scaled; /* 纬度转换为 E7 前的缩放值 */

    if(state == NULL || lon_e7 == NULL || lat_e7 == NULL || global_x < 0 || global_y < 0) {
        return false;
    }

    map_size = (double)state->config.tile_size * (double)(1UL << state->zoom);
    if(map_size <= 0.0 || (double)global_x >= map_size || (double)global_y >= map_size) {
        return false;
    }

    lon = (double)global_x / map_size * 360.0 - 180.0;
    mercator_y = LV_OFFLINE_MAP_PI * (1.0 - 2.0 * (double)global_y / map_size);
    lat = atan(sinh(mercator_y)) * 180.0 / LV_OFFLINE_MAP_PI;

    if(state->config.use_gcj02_tile) {
        /* 触摸点来自 GCJ-02 瓦片坐标，规划路网仍使用 WGS84，需要先反偏移再返回。 */
        lv_offline_map_gcj02_to_wgs84(&lon, &lat);
    }

    if(lon < -180.0) {
        lon = -180.0;
    } else if(lon > 180.0) {
        lon = 180.0;
    }

    if(lat < -LV_OFFLINE_MAP_MAX_MERCATOR_LAT) {
        lat = -LV_OFFLINE_MAP_MAX_MERCATOR_LAT;
    } else if(lat > LV_OFFLINE_MAP_MAX_MERCATOR_LAT) {
        lat = LV_OFFLINE_MAP_MAX_MERCATOR_LAT;
    }

    lon_scaled = lon * LV_OFFLINE_MAP_LON_LAT_SCALE;
    lat_scaled = lat * LV_OFFLINE_MAP_LON_LAT_SCALE;
    *lon_e7 = (int)(lon_scaled >= 0.0 ? lon_scaled + LV_OFFLINE_MAP_E7_ROUND_OFFSET :
                    lon_scaled - LV_OFFLINE_MAP_E7_ROUND_OFFSET);
    *lat_e7 = (int)(lat_scaled >= 0.0 ? lat_scaled + LV_OFFLINE_MAP_E7_ROUND_OFFSET :
                    lat_scaled - LV_OFFLINE_MAP_E7_ROUND_OFFSET);

    return true;
}

/*********************************************************
 * @brief 将轨迹记录换算成目标缩放级别下的全局像素坐标
 * @param state 控件私有状态，不能为空
 * @param record 轨迹记录指针，不能为空
 * @param target_zoom 目标缩放级别
 * @param global_x 输出全局横向像素坐标，不能为空
 * @param global_y 输出全局纵向像素坐标，不能为空
 * @return bool true 表示换算成功，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_get_track_global_pixel(lv_offline_map_state_t *state,
                                                  const lv_offline_map_track_point_t *record,
                                                  int target_zoom, int *global_x, int *global_y)
{
    int normalized_zoom; /* 限幅后的目标缩放级别 */
    int source_global_x; /* 记录缩放级别下的全局横向像素坐标 */
    int source_global_y; /* 记录缩放级别下的全局纵向像素坐标 */

    if(state == NULL || record == NULL || global_x == NULL || global_y == NULL) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, target_zoom);
    source_global_x = record->tile_x * state->config.tile_size + record->pixel_x;
    source_global_y = record->tile_y * state->config.tile_size + record->pixel_y;
    *global_x = lv_offline_map_scale_global_pixel(source_global_x, record->zoom, normalized_zoom);
    *global_y = lv_offline_map_scale_global_pixel(source_global_y, record->zoom, normalized_zoom);

    return true;
}

/*********************************************************
 * @brief 将 E7 经纬度换算为指定缩放级别下的瓦片像素坐标
 * @param state 控件私有状态，不能为空
 * @param lon_e7 经度 E7 定点值
 * @param lat_e7 纬度 E7 定点值
 * @param zoom 目标缩放级别
 * @param tile_x 输出瓦片 X 坐标，不能为空
 * @param tile_y 输出瓦片 Y 坐标，不能为空
 * @param pixel_x 输出瓦片内横向像素坐标，不能为空
 * @param pixel_y 输出瓦片内纵向像素坐标，不能为空
 * @return bool true 表示换算成功，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_lonlat_to_tile_point(lv_offline_map_state_t *state, int lon_e7, int lat_e7, int zoom,
                                                int *tile_x, int *tile_y, int *pixel_x, int *pixel_y)
{
    int normalized_zoom; /* 限幅后的缩放级别 */
    double lon; /* 十进制度经度 */
    double lat; /* 十进制度纬度 */
    double lat_rad; /* 弧度制纬度 */
    double map_size; /* 当前缩放级别下世界地图像素宽度 */
    double global_x; /* 经纬度对应的全局横向像素坐标 */
    double global_y; /* 经纬度对应的全局纵向像素坐标 */
    int global_pixel_x; /* 取整后的全局横向像素坐标 */
    int global_pixel_y; /* 取整后的全局纵向像素坐标 */

    if(state == NULL || tile_x == NULL || tile_y == NULL || pixel_x == NULL || pixel_y == NULL) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    lon = (double)lon_e7 / LV_OFFLINE_MAP_LON_LAT_SCALE;
    lat = (double)lat_e7 / LV_OFFLINE_MAP_LON_LAT_SCALE;

    if(state->config.use_gcj02_tile) {
        /* OSM 等 WGS84 路网绘制到腾讯或高德瓦片前，需要先转换成 GCJ-02 屏幕坐标。 */
        lv_offline_map_wgs84_to_gcj02(&lon, &lat);
    }

    if(lon < -180.0) {
        lon = -180.0;
    } else if(lon > 180.0) {
        lon = 180.0;
    }

    if(lat < -LV_OFFLINE_MAP_MAX_MERCATOR_LAT) {
        lat = -LV_OFFLINE_MAP_MAX_MERCATOR_LAT;
    } else if(lat > LV_OFFLINE_MAP_MAX_MERCATOR_LAT) {
        lat = LV_OFFLINE_MAP_MAX_MERCATOR_LAT;
    }

    lat_rad = lat * LV_OFFLINE_MAP_PI / 180.0;
    map_size = (double)state->config.tile_size * (double)(1UL << normalized_zoom);
    global_x = (lon + 180.0) / 360.0 * map_size;
    global_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / LV_OFFLINE_MAP_PI) / 2.0 * map_size;

    if(global_x < 0.0) {
        global_x = 0.0;
    } else if(global_x >= map_size) {
        global_x = map_size - 1.0;
    }

    if(global_y < 0.0) {
        global_y = 0.0;
    } else if(global_y >= map_size) {
        global_y = map_size - 1.0;
    }

    global_pixel_x = (int)global_x;
    global_pixel_y = (int)global_y;
    *tile_x = global_pixel_x / state->config.tile_size;
    *tile_y = global_pixel_y / state->config.tile_size;
    *pixel_x = global_pixel_x % state->config.tile_size;
    *pixel_y = global_pixel_y % state->config.tile_size;

    return true;
}

/*********************************************************
 * @brief 计算中心瓦片处于视窗中心时的横向滚动值
 * @param state 控件私有状态，不能为空
 * @return lv_coord_t 横向滚动中心值
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_coord_t lv_offline_map_get_center_scroll_x(lv_offline_map_state_t *state)
{
    lv_coord_t scroll_x; /* 中心瓦片位于视窗中心时的横向滚动值 */

    scroll_x = LV_OFFLINE_MAP_CENTER_TILE_COL * state->config.tile_size -
               state->config.view_width / 2 + state->config.tile_size / 2;

    return scroll_x;
}

/*********************************************************
 * @brief 计算中心瓦片处于视窗中心时的纵向滚动值
 * @param state 控件私有状态，不能为空
 * @return lv_coord_t 纵向滚动中心值
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_coord_t lv_offline_map_get_center_scroll_y(lv_offline_map_state_t *state)
{
    lv_coord_t scroll_y; /* 中心瓦片位于视窗中心时的纵向滚动值 */

    scroll_y = LV_OFFLINE_MAP_CENTER_TILE_ROW * state->config.tile_size -
               state->config.view_height / 2 + state->config.tile_size / 2;

    return scroll_y;
}

/*********************************************************
 * @brief 滚动到默认中心位置
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_scroll_to_center(lv_offline_map_state_t *state)
{
    if(state == NULL || state->container == NULL) {
        return;
    }

    state->tile_switching = true;
    lv_obj_scroll_to(state->container, lv_offline_map_get_center_scroll_x(state),
                     lv_offline_map_get_center_scroll_y(state), LV_ANIM_OFF);
    state->tile_switching = false;
}

/*********************************************************
 * @brief 将当前缩放级别下的全局像素坐标换算为地图容器坐标
 * @param state 控件私有状态，不能为空
 * @param global_x 全局横向像素坐标
 * @param global_y 全局纵向像素坐标
 * @return lv_offline_map_point_t 地图容器坐标
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_offline_map_point_t lv_offline_map_global_pixel_to_container_point(lv_offline_map_state_t *state,
                                                                             int global_x, int global_y)
{
    lv_offline_map_point_t point; /* 换算后的容器坐标 */
    int origin_x; /* 当前瓦片容器左上角对应的全局横向像素坐标 */
    int origin_y; /* 当前瓦片容器左上角对应的全局纵向像素坐标 */

    origin_x = (state->center_tile_x - LV_OFFLINE_MAP_CENTER_TILE_COL) * state->config.tile_size;
    origin_y = (state->center_tile_y - LV_OFFLINE_MAP_CENTER_TILE_ROW) * state->config.tile_size;
    point.x = global_x - origin_x;
    point.y = global_y - origin_y;

    return point;
}

/*********************************************************
 * @brief 将原始轨迹记录换算为当前地图容器坐标
 * @param state 控件私有状态，不能为空
 * @param record 原始轨迹记录指针，不能为空
 * @return lv_offline_map_point_t 当前地图容器内的像素坐标
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_offline_map_point_t lv_offline_map_convert_track_point(lv_offline_map_state_t *state,
                                                                 const lv_offline_map_track_point_t *record)
{
    lv_offline_map_point_t point; /* 换算后的轨迹点 */
    int global_x; /* 轨迹点当前缩放级别下的全局横向像素坐标 */
    int global_y; /* 轨迹点当前缩放级别下的全局纵向像素坐标 */

    point.x = 0;
    point.y = 0;
    if(!lv_offline_map_get_track_global_pixel(state, record, state->zoom, &global_x, &global_y)) {
        return point;
    }

    point = lv_offline_map_global_pixel_to_container_point(state, global_x, global_y);

    return point;
}

/*********************************************************
 * @brief 按当前跟随经纬度对应的地图坐标刷新车标位置
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_update_arrow_position(lv_offline_map_state_t *state)
{
    lv_offline_map_point_t follow_point; /* 当前车辆跟随位置换算后的地图内容层坐标 */
    lv_coord_t pivot_x; /* 车标地理坐标对应的对象内横向锚点 */
    lv_coord_t pivot_y; /* 车标地理坐标对应的对象内纵向锚点 */

    if(state == NULL || state->arrow == NULL) {
        return;
    }

    if(!state->follow_position_valid) {
        lv_obj_add_flag(state->arrow, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    follow_point = lv_offline_map_global_pixel_to_container_point(state, state->follow_global_x,
                                                                  state->follow_global_y);
    lv_offline_map_get_vehicle_pivot(state, &pivot_x, &pivot_y);
    lv_obj_remove_flag(state->arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(state->arrow, (lv_coord_t)follow_point.x - pivot_x, (lv_coord_t)follow_point.y - pivot_y);
}

/*********************************************************
 * @brief 获取车标地理坐标在对象内对应的锚点
 * @param state 控件私有状态，不能为空
 * @param pivot_x 输出横向锚点，不能为空
 * @param pivot_y 输出纵向锚点，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_get_vehicle_pivot(lv_offline_map_state_t *state, lv_coord_t *pivot_x,
                                             lv_coord_t *pivot_y)
{
    if(state == NULL || pivot_x == NULL || pivot_y == NULL) {
        return;
    }

    if(state->config.arrow_src != NULL) {
        *pivot_x = (lv_coord_t)state->config.arrow_pivot_x;
        *pivot_y = (lv_coord_t)state->config.arrow_pivot_y;
        return;
    }

    *pivot_x = LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE / 2;
    *pivot_y = LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE / 2;
}

/*********************************************************
 * @brief 获取自动跟随时路线当前位置应对齐到的视窗锚点
 * @param state 控件私有状态，不能为空
 * @param anchor_x 输出视窗内横向锚点坐标，不能为空
 * @param anchor_y 输出视窗内纵向锚点坐标，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-19
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_get_follow_anchor(lv_offline_map_state_t *state, lv_coord_t *anchor_x,
                                             lv_coord_t *anchor_y)
{
    lv_coord_t resolved_x; /* 轨迹当前位置在视窗内应显示的横向坐标 */
    lv_coord_t resolved_y; /* 轨迹当前位置在视窗内应显示的纵向坐标 */
    lv_coord_t arrow_width; /* 方向标对象当前宽度，单位为像素 */
    lv_coord_t arrow_height; /* 方向标对象当前高度，单位为像素 */
    lv_coord_t pivot_x; /* 车标地理坐标对应的对象内横向锚点 */
    lv_coord_t pivot_y; /* 车标地理坐标对应的对象内纵向锚点 */

    if(state == NULL || anchor_x == NULL || anchor_y == NULL) {
        return;
    }

    resolved_x = state->config.view_width / 2;
    resolved_y = state->config.view_height / 2;
    if(state->arrow != NULL) {
        lv_offline_map_get_vehicle_pivot(state, &pivot_x, &pivot_y);
        arrow_width = lv_obj_get_width(state->arrow);
        arrow_height = lv_obj_get_height(state->arrow);
        if(arrow_width > 0) {
            resolved_x += pivot_x - arrow_width / 2;
        }
        if(arrow_height > 0) {
            resolved_y += pivot_y - arrow_height / 2;
        }
        resolved_y += state->config.arrow_y_offset;
    }

    *anchor_x = resolved_x;
    *anchor_y = resolved_y;
}

/*********************************************************
 * @brief 按指定视窗锚点滚动到中心瓦片内的像素位置
 * @param state 控件私有状态，不能为空
 * @param pixel_x 中心瓦片内横向像素坐标
 * @param pixel_y 中心瓦片内纵向像素坐标
 * @param anchor_x 目标像素在视窗内对齐的横向锚点
 * @param anchor_y 目标像素在视窗内对齐的纵向锚点
 * @param anim 是否启用 LVGL 滚动动画
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-19
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_scroll_to_tile_pixel_at_anchor(lv_offline_map_state_t *state, int pixel_x, int pixel_y,
                                                          lv_coord_t anchor_x, lv_coord_t anchor_y,
                                                          lv_anim_enable_t anim)
{
    lv_coord_t scroll_x; /* 横向滚动目标，保证目标像素落到指定横向锚点 */
    lv_coord_t scroll_y; /* 纵向滚动目标，保证目标像素落到指定纵向锚点 */
    bool previous_switching; /* 滚动前瓦片切换保护状态 */

    if(state == NULL || state->container == NULL) {
        return;
    }

    scroll_x = LV_OFFLINE_MAP_CENTER_TILE_COL * state->config.tile_size + pixel_x - anchor_x;
    scroll_y = LV_OFFLINE_MAP_CENTER_TILE_ROW * state->config.tile_size + pixel_y - anchor_y;
    previous_switching = state->tile_switching;
    state->tile_switching = true;
    lv_obj_scroll_to(state->container, scroll_x, scroll_y, anim);
    state->tile_switching = previous_switching;
}

/*********************************************************
 * @brief 将地图视野滚动到全局像素坐标并按指定视窗锚点对齐
 * @param state 控件私有状态，不能为空
 * @param global_x 当前缩放级别下全局横向像素坐标
 * @param global_y 当前缩放级别下全局纵向像素坐标
 * @param zoom 全局像素坐标所属缩放级别
 * @param anchor_x 目标全局像素在视窗内对齐的横向锚点
 * @param anchor_y 目标全局像素在视窗内对齐的纵向锚点
 * @param anim 是否启用 LVGL 滚动动画
 * @return bool true 表示视野已移动，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-19
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_center_global_pixel_at_anchor(lv_offline_map_state_t *state, int global_x, int global_y,
                                                         int zoom, lv_coord_t anchor_x, lv_coord_t anchor_y,
                                                         lv_anim_enable_t anim)
{
    int normalized_zoom; /* 限幅后的缩放级别 */
    int tile_x; /* 全局像素所在瓦片 X 坐标 */
    int tile_y; /* 全局像素所在瓦片 Y 坐标 */
    int pixel_x; /* 全局像素在瓦片内横向偏移 */
    int pixel_y; /* 全局像素在瓦片内纵向偏移 */
    int tile_delta_x; /* 目标中心瓦片相对当前中心瓦片的 X 方向变化量 */
    int tile_delta_y; /* 目标中心瓦片相对当前中心瓦片的 Y 方向变化量 */
    bool need_reload; /* true 表示跨瓦片或跨缩放级别，需要重载瓦片矩阵 */

    if(state == NULL || state->container == NULL || global_x < 0 || global_y < 0) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    tile_x = global_x / state->config.tile_size;
    tile_y = global_y / state->config.tile_size;
    pixel_x = global_x - tile_x * state->config.tile_size;
    pixel_y = global_y - tile_y * state->config.tile_size;
    need_reload = (tile_x != state->center_tile_x || tile_y != state->center_tile_y ||
                   normalized_zoom != state->zoom);
    tile_delta_x = tile_x - state->center_tile_x;
    tile_delta_y = tile_y - state->center_tile_y;

    if(need_reload) {
        if(normalized_zoom != state->zoom || !lv_offline_map_shift_tile_matrix(state, tile_delta_x, tile_delta_y)) {
            lv_offline_map_load_tiles(state, tile_x, tile_y, normalized_zoom);
        }
    }

    lv_offline_map_scroll_to_tile_pixel_at_anchor(state, pixel_x, pixel_y, anchor_x, anchor_y, anim);

    return true;
}

/*********************************************************
 * @brief 将地图视野居中到全局像素坐标并按需切换中心瓦片
 * @param state 控件私有状态，不能为空
 * @param global_x 当前缩放级别下全局横向像素坐标
 * @param global_y 当前缩放级别下全局纵向像素坐标
 * @param zoom 全局像素坐标所属缩放级别
 * @param anim 是否启用 LVGL 滚动动画
 * @return bool true 表示视野已移动，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_center_global_pixel(lv_offline_map_state_t *state, int global_x, int global_y,
                                               int zoom, lv_anim_enable_t anim)
{
    lv_coord_t anchor_x; /* 屏幕中心横向锚点 */
    lv_coord_t anchor_y; /* 屏幕中心纵向锚点 */
    bool centered; /* 地图居中处理结果，true 表示已按屏幕中心完成滚动 */

    if(state == NULL) {
        return false;
    }

    anchor_x = state->config.view_width / 2;
    anchor_y = state->config.view_height / 2;
    centered = lv_offline_map_center_global_pixel_at_anchor(state, global_x, global_y, zoom, anchor_x, anchor_y,
                                                            anim);

    return centered;
}

/*********************************************************
 * @brief 将自动跟随位置对齐到方向标锚点
 * @param state 控件私有状态，不能为空
 * @param global_x 当前缩放级别下全局横向像素坐标
 * @param global_y 当前缩放级别下全局纵向像素坐标
 * @param zoom 全局像素坐标所属缩放级别
 * @param anim 是否启用 LVGL 滚动动画
 * @return bool true 表示视野已移动，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-19
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_center_follow_global_pixel(lv_offline_map_state_t *state, int global_x, int global_y,
                                                      int zoom, lv_anim_enable_t anim)
{
    lv_coord_t anchor_x; /* 自动跟随时当前位置对齐的视窗横向锚点 */
    lv_coord_t anchor_y; /* 自动跟随时当前位置对齐的视窗纵向锚点 */
    bool centered; /* 跟随锚点对齐结果，true 表示当前位置已对齐到方向标 */

    if(state == NULL) {
        return false;
    }

    lv_offline_map_get_follow_anchor(state, &anchor_x, &anchor_y);
    centered = lv_offline_map_center_global_pixel_at_anchor(state, global_x, global_y, zoom, anchor_x, anchor_y,
                                                            anim);

    return centered;
}

/*********************************************************
 * @brief 将方向标旋转角度归一化到 LVGL 图片旋转范围
 * @param rotation 输入旋转角度，单位为 0.1 度
 * @return int 归一化后的旋转角度，范围为 0 到 3599
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int lv_offline_map_normalize_arrow_rotation(int rotation)
{
    int normalized_rotation = rotation; /* 归一化过程中的旋转角度 */

    while(normalized_rotation >= 3600) {
        normalized_rotation -= 3600;
    }

    while(normalized_rotation < 0) {
        normalized_rotation += 3600;
    }

    return normalized_rotation;
}

/*********************************************************
 * @brief 根据当前路线方向更新方向标朝向
 * @param state 控件私有状态，不能为空
 * @param delta_x 路线方向横向增量，向右为正
 * @param delta_y 路线方向纵向增量，向下为正
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_update_arrow_direction(lv_offline_map_state_t *state, int delta_x, int delta_y)
{
    double heading_rad; /* 路线方向相对屏幕正上方的顺时针弧度角 */
    int target_rotation; /* 目标旋转角度，单位为 0.1 度 */

    if(state == NULL || state->arrow == NULL || state->config.arrow_src == NULL || (delta_x == 0 && delta_y == 0)) {
        return;
    }

    heading_rad = atan2((double)delta_x, -(double)delta_y);
    target_rotation = (int)(heading_rad * 1800.0 / LV_OFFLINE_MAP_PI);
    target_rotation = lv_offline_map_normalize_arrow_rotation(target_rotation);
    if(target_rotation == state->arrow_rotation) {
        return;
    }

    state->arrow_rotation = target_rotation;
    LV_OFFLINE_MAP_IMAGE_SET_ROTATION(state->arrow, state->arrow_rotation);
}

/*********************************************************
 * @brief 按时间节流刷新自动跟随中的轨迹折线
 * @param state 控件私有状态，不能为空
 * @param current_tick 当前系统 tick
 * @param force_refresh true 表示立即刷新
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_refresh_follow_track_by_tick(lv_offline_map_state_t *state, uint32_t current_tick,
                                                        bool force_refresh)
{
    if(state == NULL) {
        return;
    }

    if(force_refresh || state->follow_last_refresh_tick == 0U ||
       current_tick - state->follow_last_refresh_tick >= LV_OFFLINE_MAP_TRACK_FOLLOW_REFRESH_MS) {
        if(state->follow_timer != NULL) {
            state->follow_last_refresh_tick = current_tick;
        }
        lv_offline_map_refresh_track(state);
    }
}

/*********************************************************
 * @brief 停止并删除用户空闲恢复居中的定时器
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_stop_auto_center_timer(lv_offline_map_state_t *state)
{
    if(state == NULL) {
        return;
    }

    state->follow_user_scroll_tick = 0U;
    if(state->auto_center_timer == NULL) {
        return;
    }

    LV_OFFLINE_MAP_TIMER_DELETE(state->auto_center_timer);
    state->auto_center_timer = NULL;
}

/*********************************************************
 * @brief 处理用户拖图后的空闲恢复居中定时器
 * @param timer LVGL 定时器对象，user_data 指向控件私有状态
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_auto_center_timer_cb(lv_timer_t *timer)
{
    lv_offline_map_state_t *state; /* 定时器绑定的控件私有状态 */
    uint32_t current_tick; /* 当前系统 tick */

    if(timer == NULL) {
        return;
    }

    state = (lv_offline_map_state_t *)lv_timer_get_user_data(timer);
    if(state == NULL || !state->follow_position_valid || state->follow_auto_center ||
       state->follow_user_scroll_tick == 0U) {
        lv_offline_map_stop_auto_center_timer(state);
        return;
    }

    current_tick = lv_tick_get();
    if(current_tick - state->follow_user_scroll_tick < LV_OFFLINE_MAP_AUTO_CENTER_IDLE_MS) {
        return;
    }

    state->follow_auto_center = true;
    (void)lv_offline_map_center_follow_global_pixel(state, state->follow_global_x, state->follow_global_y,
                                                    state->zoom, LV_ANIM_OFF);
    lv_offline_map_refresh_track(state);
    lv_offline_map_stop_auto_center_timer(state);
}

/*********************************************************
 * @brief 记录用户拖动地图并延迟恢复车标居中
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_note_user_scroll(lv_offline_map_state_t *state)
{
    if(state == NULL || !state->follow_position_valid) {
        return;
    }

    state->follow_auto_center = false;
    state->follow_user_scroll_tick = lv_tick_get();
    if(state->auto_center_timer == NULL) {
        state->auto_center_timer = lv_timer_create(lv_offline_map_auto_center_timer_cb,
                                                   LV_OFFLINE_MAP_AUTO_CENTER_CHECK_MS, state);
    }
}

/*********************************************************
 * @brief 在瓦片矩阵改变后按播放状态刷新轨迹
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_refresh_track_after_tile_change(lv_offline_map_state_t *state)
{
    if(state == NULL) {
        return;
    }

    if(state->follow_timer != NULL) {
        state->follow_last_refresh_tick = lv_tick_get();
    }
    lv_offline_map_refresh_track(state);
}

/*********************************************************
 * @brief 按当前缩放和纬度计算自动跟随本帧应前进的像素距离
 * @param state 控件私有状态，不能为空
 * @param elapsed_ms 当前帧距离上一帧经过的时间，单位为毫秒
 * @return double 当前帧应前进的全局像素距离
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static double lv_offline_map_get_follow_step_pixel(lv_offline_map_state_t *state, uint32_t elapsed_ms)
{
    double world_size; /* 当前缩放级别下世界地图宽度，单位为全局像素 */
    double mercator_y; /* 当前车辆位置归一化后的 Mercator 纵向弧度参数 */
    double latitude_rad; /* 当前车辆位置反算纬度弧度值 */
    double meters_per_pixel; /* 当前纬度和缩放级别下每个像素代表的米数 */
    double speed_mps; /* 自动跟随目标速度，单位为米每秒 */

    if(state == NULL) {
        return 0.0;
    }

    world_size = (double)state->config.tile_size * (double)(1UL << state->zoom);
    if(world_size <= 0.0) {
        return 0.0;
    }

    mercator_y = LV_OFFLINE_MAP_PI - 2.0 * LV_OFFLINE_MAP_PI * state->follow_precise_y / world_size;
    latitude_rad = atan(sinh(mercator_y));
    meters_per_pixel = cos(latitude_rad) * 2.0 * LV_OFFLINE_MAP_PI * LV_OFFLINE_MAP_EARTH_RADIUS_M / world_size;
    if(meters_per_pixel <= 0.0) {
        return 0.0;
    }

    speed_mps = state->config.follow_speed_kmh * 1000.0 / 3600.0;

    return speed_mps * (double)elapsed_ms / 1000.0 / meters_per_pixel;
}

/*********************************************************
 * @brief 根据全局像素纵坐标估算当前缩放级别的每像素米数
 * @param state 控件私有状态，不能为空
 * @param global_y 全局纵向像素坐标
 * @param zoom 全局像素坐标所属缩放级别
 * @return double 每个全局像素代表的米数，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static double lv_offline_map_get_meters_per_pixel(lv_offline_map_state_t *state, double global_y, int zoom)
{
    int normalized_zoom; /* 限幅后的缩放级别 */
    double world_size; /* 当前缩放级别下世界地图宽度，单位为全局像素 */
    double mercator_y; /* 当前点归一化后的 Mercator 纵向弧度参数 */
    double latitude_rad; /* 当前点反算纬度弧度值 */
    double meters_per_pixel; /* 当前纬度和缩放级别下每个像素代表的米数 */

    if(state == NULL) {
        return 0.0;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    world_size = (double)state->config.tile_size * (double)(1UL << normalized_zoom);
    if(world_size <= 0.0) {
        return 0.0;
    }

    mercator_y = LV_OFFLINE_MAP_PI - 2.0 * LV_OFFLINE_MAP_PI * global_y / world_size;
    latitude_rad = atan(sinh(mercator_y));
    meters_per_pixel = cos(latitude_rad) * 2.0 * LV_OFFLINE_MAP_PI * LV_OFFLINE_MAP_EARTH_RADIUS_M / world_size;
    if(meters_per_pixel <= 0.0) {
        return 0.0;
    }

    return meters_per_pixel;
}

/*********************************************************
 * @brief 计算两个轨迹记录之间的近似地表距离
 * @param state 控件私有状态，不能为空
 * @param from 起点轨迹记录，不能为空
 * @param to 终点轨迹记录，不能为空
 * @return uint32_t 两点之间近似距离，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static uint32_t lv_offline_map_get_track_segment_distance_m(lv_offline_map_state_t *state,
                                                            const lv_offline_map_track_point_t *from,
                                                            const lv_offline_map_track_point_t *to)
{
    int from_x; /* 起点在当前缩放级别下的全局横向像素坐标 */
    int from_y; /* 起点在当前缩放级别下的全局纵向像素坐标 */
    int to_x; /* 终点在当前缩放级别下的全局横向像素坐标 */
    int to_y; /* 终点在当前缩放级别下的全局纵向像素坐标 */
    double delta_x; /* 两点全局横向像素差 */
    double delta_y; /* 两点全局纵向像素差 */
    double meters_per_pixel; /* 当前路段中点纬度下每像素代表的米数 */
    double distance_m; /* 当前路段估算距离，单位为米 */

    if(state == NULL || from == NULL || to == NULL ||
       !lv_offline_map_get_track_global_pixel(state, from, state->zoom, &from_x, &from_y) ||
       !lv_offline_map_get_track_global_pixel(state, to, state->zoom, &to_x, &to_y)) {
        return 0U;
    }

    delta_x = (double)to_x - (double)from_x;
    delta_y = (double)to_y - (double)from_y;
    meters_per_pixel = lv_offline_map_get_meters_per_pixel(state, ((double)from_y + (double)to_y) / 2.0,
                                                           state->zoom);
    distance_m = sqrt(delta_x * delta_x + delta_y * delta_y) * meters_per_pixel;
    if(distance_m <= 0.0) {
        return 0U;
    }
    if(distance_m > (double)UINT32_MAX) {
        return UINT32_MAX;
    }

    return (uint32_t)(distance_m + 0.5);
}

/*********************************************************
 * @brief 更新自动跟随当前位置对应的已行驶里程
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_update_traveled_distance(lv_offline_map_state_t *state)
{
    uint32_t base_distance; /* 当前路线段起点累计里程，单位为米 */
    uint32_t segment_distance; /* 当前路线段总里程，单位为米 */
    uint32_t partial_distance; /* 当前路线段内已行驶里程，单位为米 */
    int start_global_x; /* 当前路线段起点全局横向像素坐标 */
    int start_global_y; /* 当前路线段起点全局纵向像素坐标 */
    int end_global_x; /* 当前路线段终点全局横向像素坐标 */
    int end_global_y; /* 当前路线段终点全局纵向像素坐标 */
    double segment_pixel_distance; /* 当前路线段全局像素长度 */
    double traveled_pixel_distance; /* 当前路线段内已行驶像素长度 */
    double ratio; /* 当前路线段内已行驶比例 */

    if(state == NULL || state->track_distance_m == NULL || state->track_point_count == 0U) {
        return;
    }

    if(!state->follow_position_valid || state->follow_segment_index + 1U >= state->track_point_count) {
        state->track_traveled_distance_m = state->track_distance_m[state->track_travel_index];
        return;
    }

    base_distance = state->track_distance_m[state->follow_segment_index];
    segment_distance = state->track_distance_m[state->follow_segment_index + 1U] - base_distance;
    if(segment_distance == 0U ||
       !lv_offline_map_get_track_global_pixel(state, &state->track_records[state->follow_segment_index],
                                              state->zoom, &start_global_x, &start_global_y) ||
       !lv_offline_map_get_track_global_pixel(state, &state->track_records[state->follow_segment_index + 1U],
                                              state->zoom, &end_global_x, &end_global_y)) {
        state->track_traveled_distance_m = base_distance;
        return;
    }

    segment_pixel_distance = sqrt((double)(end_global_x - start_global_x) * (double)(end_global_x - start_global_x) +
                                  (double)(end_global_y - start_global_y) * (double)(end_global_y - start_global_y));
    traveled_pixel_distance = sqrt((state->follow_precise_x - (double)start_global_x) *
                                   (state->follow_precise_x - (double)start_global_x) +
                                   (state->follow_precise_y - (double)start_global_y) *
                                   (state->follow_precise_y - (double)start_global_y));
    ratio = 0.0;
    if(segment_pixel_distance > 0.0) {
        ratio = traveled_pixel_distance / segment_pixel_distance;
    }
    if(ratio < 0.0) {
        ratio = 0.0;
    } else if(ratio > 1.0) {
        ratio = 1.0;
    }

    partial_distance = (uint32_t)((double)segment_distance * ratio + 0.5);
    state->track_traveled_distance_m = base_distance + partial_distance;
    if(state->track_traveled_distance_m > state->track_total_distance_m) {
        state->track_traveled_distance_m = state->track_total_distance_m;
    }
}

/*********************************************************
 * @brief 根据实时定位点计算当前路线上的最近投影位置
 * @param state 控件私有状态，不能为空
 * @param location_global_x 实时定位点全局横向像素坐标
 * @param location_global_y 实时定位点全局纵向像素坐标
 * @param projected_global_x 输出投影点全局横向像素坐标，不能为空
 * @param projected_global_y 输出投影点全局纵向像素坐标，不能为空
 * @param segment_index 输出投影点所在路线段起点索引，不能为空
 * @param segment_ratio 输出投影点在线段内的比例，范围为 0 到 1，不能为空
 * @param deviation_pixel 输出定位点到路线投影点的像素距离，不能为空
 * @return bool true 表示已找到最近路线投影点，false 表示路线点不足或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_project_location_to_track(lv_offline_map_state_t *state, int location_global_x,
                                                     int location_global_y, double *projected_global_x,
                                                     double *projected_global_y, uint16_t *segment_index,
                                                     double *segment_ratio, double *deviation_pixel)
{
    uint16_t index; /* 当前扫描的路线段起点索引 */
    int start_global_x; /* 当前路线段起点全局横向像素坐标 */
    int start_global_y; /* 当前路线段起点全局纵向像素坐标 */
    int end_global_x; /* 当前路线段终点全局横向像素坐标 */
    int end_global_y; /* 当前路线段终点全局纵向像素坐标 */
    double segment_x; /* 当前路线段横向像素向量 */
    double segment_y; /* 当前路线段纵向像素向量 */
    double point_x; /* 定位点相对当前路线段起点的横向像素向量 */
    double point_y; /* 定位点相对当前路线段起点的纵向像素向量 */
    double segment_length_square; /* 当前路线段像素长度平方 */
    double ratio; /* 定位点投影到当前路线段后的线段内比例 */
    double candidate_x; /* 当前路线段上的候选投影点全局横向像素坐标 */
    double candidate_y; /* 当前路线段上的候选投影点全局纵向像素坐标 */
    double delta_x; /* 定位点到候选投影点的横向像素差 */
    double delta_y; /* 定位点到候选投影点的纵向像素差 */
    double distance_square; /* 定位点到候选投影点的像素距离平方 */
    double best_distance_square; /* 当前找到的最小像素距离平方 */
    bool has_projection; /* true 表示已经找到至少一个可用路线段投影 */

    if(state == NULL || projected_global_x == NULL || projected_global_y == NULL || segment_index == NULL ||
       segment_ratio == NULL || deviation_pixel == NULL || state->track_point_count < 2U) {
        return false;
    }

    best_distance_square = 0.0;
    has_projection = false;
    for(index = 0U; index + 1U < state->track_point_count; index++) {
        if(!lv_offline_map_get_track_global_pixel(state, &state->track_records[index], state->zoom,
                                                  &start_global_x, &start_global_y) ||
           !lv_offline_map_get_track_global_pixel(state, &state->track_records[index + 1U], state->zoom,
                                                  &end_global_x, &end_global_y)) {
            continue;
        }

        segment_x = (double)end_global_x - (double)start_global_x;
        segment_y = (double)end_global_y - (double)start_global_y;
        segment_length_square = segment_x * segment_x + segment_y * segment_y;
        if(segment_length_square <= 0.0) {
            continue;
        }

        point_x = (double)location_global_x - (double)start_global_x;
        point_y = (double)location_global_y - (double)start_global_y;
        ratio = (point_x * segment_x + point_y * segment_y) / segment_length_square;
        if(ratio < 0.0) {
            ratio = 0.0;
        } else if(ratio > 1.0) {
            ratio = 1.0;
        }

        candidate_x = (double)start_global_x + segment_x * ratio;
        candidate_y = (double)start_global_y + segment_y * ratio;
        delta_x = (double)location_global_x - candidate_x;
        delta_y = (double)location_global_y - candidate_y;
        distance_square = delta_x * delta_x + delta_y * delta_y;
        if(!has_projection || distance_square < best_distance_square) {
            best_distance_square = distance_square;
            *projected_global_x = candidate_x;
            *projected_global_y = candidate_y;
            *segment_index = index;
            *segment_ratio = ratio;
            has_projection = true;
        }
    }

    if(!has_projection) {
        return false;
    }

    *deviation_pixel = sqrt(best_distance_square);

    return true;
}

/*********************************************************
 * @brief 处理自动跟随轨迹的定时推进
 * @param timer LVGL 定时器对象，user_data 指向控件私有状态
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_track_follow_timer_cb(lv_timer_t *timer)
{
    lv_offline_map_state_t *state; /* 定时器绑定的控件私有状态 */
    uint32_t current_tick; /* 当前系统 tick */
    uint32_t elapsed_ms; /* 距离上一帧经过的毫秒数 */
    uint32_t refresh_tick; /* 地图滚动后的系统 tick */
    uint16_t next_index; /* 当前路线段终点索引 */
    int target_global_x; /* 当前路线段终点全局横向像素坐标 */
    int target_global_y; /* 当前路线段终点全局纵向像素坐标 */
    double delta_x; /* 当前跟随位置到终点的横向剩余像素距离 */
    double delta_y; /* 当前跟随位置到终点的纵向剩余像素距离 */
    double remain_distance; /* 当前跟随位置到终点的剩余距离 */
    double remain_step; /* 当前帧剩余可前进距离 */
    double move_ratio; /* 当前帧插值移动比例 */

    if(timer == NULL) {
        return;
    }

    state = (lv_offline_map_state_t *)lv_timer_get_user_data(timer);
    if(state == NULL || state->track_point_count == 0U) {
        lv_offline_map_stop_track_follow(state);
        return;
    }

    current_tick = lv_tick_get();
    if(state->follow_last_tick == 0U) {
        elapsed_ms = LV_OFFLINE_MAP_FOLLOW_PERIOD_MS;
    } else {
        elapsed_ms = current_tick - state->follow_last_tick;
    }
    state->follow_last_tick = current_tick;
    if(elapsed_ms > LV_OFFLINE_MAP_FOLLOW_MAX_ELAPSED_MS) {
        elapsed_ms = LV_OFFLINE_MAP_FOLLOW_MAX_ELAPSED_MS;
    }

    remain_step = lv_offline_map_get_follow_step_pixel(state, elapsed_ms);
    while(state->follow_segment_index + 1U < state->track_point_count && remain_step > 0.0) {
        next_index = state->follow_segment_index + 1U;
        if(!lv_offline_map_get_track_global_pixel(state, &state->track_records[next_index], state->zoom,
                                                  &target_global_x, &target_global_y)) {
            lv_offline_map_stop_track_follow(state);
            return;
        }

        delta_x = (double)target_global_x - state->follow_precise_x;
        delta_y = (double)target_global_y - state->follow_precise_y;
        remain_distance = sqrt(delta_x * delta_x + delta_y * delta_y);

        if(remain_distance > remain_step) {
            move_ratio = remain_step / remain_distance;
            lv_offline_map_update_arrow_direction(state, (int)delta_x, (int)delta_y);
            state->follow_precise_x += delta_x * move_ratio;
            state->follow_precise_y += delta_y * move_ratio;
            state->follow_global_x = (int)(state->follow_precise_x + 0.5);
            state->follow_global_y = (int)(state->follow_precise_y + 0.5);
            lv_offline_map_update_traveled_distance(state);
            if(state->follow_auto_center) {
                lv_offline_map_center_follow_global_pixel(state, state->follow_global_x, state->follow_global_y,
                                                          state->zoom, LV_ANIM_OFF);
            }
            lv_offline_map_update_arrow_position(state);
            refresh_tick = lv_tick_get();
            lv_offline_map_refresh_follow_track_by_tick(state, refresh_tick, false);
            return;
        }

        lv_offline_map_update_arrow_direction(state, (int)delta_x, (int)delta_y);
        state->follow_precise_x = (double)target_global_x;
        state->follow_precise_y = (double)target_global_y;
        state->follow_global_x = target_global_x;
        state->follow_global_y = target_global_y;
        state->follow_segment_index = next_index;
        state->track_travel_index = next_index;
        lv_offline_map_update_traveled_distance(state);
        if(remain_distance > 0.0) {
            remain_step -= remain_distance;
        } else {
            remain_step = 0.0;
        }
    }

    if(state->follow_auto_center) {
        lv_offline_map_center_follow_global_pixel(state, state->follow_global_x, state->follow_global_y,
                                                  state->zoom, LV_ANIM_OFF);
    }
    lv_offline_map_update_arrow_position(state);
    refresh_tick = lv_tick_get();
    if(state->follow_segment_index + 1U >= state->track_point_count) {
        lv_offline_map_update_traveled_distance(state);
        lv_offline_map_refresh_follow_track_by_tick(state, refresh_tick, true);
        lv_offline_map_stop_track_follow(state);
        return;
    }
    lv_offline_map_refresh_follow_track_by_tick(state, refresh_tick, false);
}

/*********************************************************
 * @brief 检查瓦片文件是否存在
 * @param state 控件私有状态，不能为空
 * @param path 瓦片文件路径，不能为空
 * @return bool true 表示文件可读，false 表示文件不存在或不可读
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_tile_exists(lv_offline_map_state_t *state, const char *path)
{
    lv_fs_file_t file; /* LVGL 文件句柄，仅用于检测文件是否可读 */
    lv_fs_res_t result; /* 文件打开结果 */

    if(state == NULL || path == NULL) {
        return false;
    }

    result = lv_fs_open(&file, path, LV_FS_MODE_RD);
    if(result != LV_FS_RES_OK) {
        return false;
    }

    lv_fs_close(&file);
    return true;
}

/*********************************************************
 * @brief 根据滚动偏移计算需要平移的瓦片数量
 * @param state 控件私有状态，不能为空
 * @param scroll_delta 当前滚动值相对中心滚动值的偏移
 * @return int 需要平移的瓦片数量，负数表示向小坐标方向加载
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static int lv_offline_map_get_tile_delta(lv_offline_map_state_t *state, lv_coord_t scroll_delta)
{
    int tile_delta = 0; /* 根据滚动偏移折算出的瓦片坐标变化量 */

    if(scroll_delta >= state->config.tile_size) {
        tile_delta = scroll_delta / state->config.tile_size;
    } else if(scroll_delta <= -state->config.tile_size) {
        tile_delta = -((-scroll_delta) / state->config.tile_size);
    }

    return tile_delta;
}

/*********************************************************
 * @brief 将单个瓦片对象更新到指定地图瓦片坐标
 * @param state 控件私有状态，不能为空
 * @param row 瓦片矩阵行索引
 * @param col 瓦片矩阵列索引
 * @param tile_x 地图瓦片 X 坐标
 * @param tile_y 地图瓦片 Y 坐标
 * @param zoom 地图瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_load_tile_cell(lv_offline_map_state_t *state, int row, int col, int tile_x, int tile_y,
                                          int zoom)
{
    char *path; /* 当前瓦片对象绑定的路径缓存，生命周期与组件状态一致 */

    if(state == NULL || row < 0 || row >= LV_OFFLINE_MAP_TILE_ROW_NUM ||
       col < 0 || col >= LV_OFFLINE_MAP_TILE_COL_NUM || state->tile[row][col] == NULL) {
        return;
    }

    lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
    path = state->tile_path[row][col];
    snprintf(path, LV_OFFLINE_MAP_MAX_PATH_LEN, "%s%d/%d/%d/%s",
             state->config.map_dir, zoom, tile_x, tile_y, state->config.tile_file_name);

    if(lv_offline_map_tile_exists(state, path)) {
        lv_obj_remove_flag(state->tile[row][col], LV_OBJ_FLAG_HIDDEN);
        LV_OFFLINE_MAP_IMAGE_SET_SRC(state->tile[row][col], path);
    } else {
        lv_obj_add_flag(state->tile[row][col], LV_OBJ_FLAG_HIDDEN);
    }
}

/*********************************************************
 * @brief 按瓦片增量复用矩阵并只加载进入边缘的新瓦片
 * @param state 控件私有状态，不能为空
 * @param tile_delta_x 中心瓦片 X 方向变化量
 * @param tile_delta_y 中心瓦片 Y 方向变化量
 * @return bool true 表示已增量加载，false 表示调用方需要全量加载
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_shift_tile_matrix(lv_offline_map_state_t *state, int tile_delta_x, int tile_delta_y)
{
    lv_obj_t *saved_tile; /* 被移出一侧并复用到另一侧的瓦片对象 */
    int old_center_x; /* 增量加载前的中心瓦片 X 坐标 */
    int old_center_y; /* 增量加载前的中心瓦片 Y 坐标 */
    int row; /* 当前移动的瓦片行索引 */
    int col; /* 当前移动的瓦片列索引 */

    if(state == NULL || tile_delta_x < -1 || tile_delta_x > 1 || tile_delta_y < -1 || tile_delta_y > 1 ||
       (tile_delta_x != 0 && tile_delta_y != 0) || (tile_delta_x == 0 && tile_delta_y == 0)) {
        return false;
    }

    old_center_x = state->center_tile_x;
    old_center_y = state->center_tile_y;
    state->center_tile_x += tile_delta_x;
    state->center_tile_y += tile_delta_y;

    if(tile_delta_x > 0) {
        for(row = 0; row < LV_OFFLINE_MAP_TILE_ROW_NUM; row++) {
            saved_tile = state->tile[row][0];
            for(col = 0; col < LV_OFFLINE_MAP_TILE_COL_NUM - 1; col++) {
                state->tile[row][col] = state->tile[row][col + 1];
                lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
            }
            state->tile[row][LV_OFFLINE_MAP_TILE_COL_NUM - 1] = saved_tile;
            lv_offline_map_load_tile_cell(state, row, LV_OFFLINE_MAP_TILE_COL_NUM - 1,
                                          old_center_x - LV_OFFLINE_MAP_CENTER_TILE_COL +
                                          LV_OFFLINE_MAP_TILE_COL_NUM,
                                          old_center_y - LV_OFFLINE_MAP_CENTER_TILE_ROW + row, state->zoom);
        }
    } else if(tile_delta_x < 0) {
        for(row = 0; row < LV_OFFLINE_MAP_TILE_ROW_NUM; row++) {
            saved_tile = state->tile[row][LV_OFFLINE_MAP_TILE_COL_NUM - 1];
            for(col = LV_OFFLINE_MAP_TILE_COL_NUM - 1; col > 0; col--) {
                state->tile[row][col] = state->tile[row][col - 1];
                lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
            }
            state->tile[row][0] = saved_tile;
            lv_offline_map_load_tile_cell(state, row, 0, old_center_x - LV_OFFLINE_MAP_CENTER_TILE_COL - 1,
                                          old_center_y - LV_OFFLINE_MAP_CENTER_TILE_ROW + row, state->zoom);
        }
    } else if(tile_delta_y > 0) {
        for(col = 0; col < LV_OFFLINE_MAP_TILE_COL_NUM; col++) {
            saved_tile = state->tile[0][col];
            for(row = 0; row < LV_OFFLINE_MAP_TILE_ROW_NUM - 1; row++) {
                state->tile[row][col] = state->tile[row + 1][col];
                lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
            }
            state->tile[LV_OFFLINE_MAP_TILE_ROW_NUM - 1][col] = saved_tile;
            lv_offline_map_load_tile_cell(state, LV_OFFLINE_MAP_TILE_ROW_NUM - 1, col,
                                          old_center_x - LV_OFFLINE_MAP_CENTER_TILE_COL + col,
                                          old_center_y - LV_OFFLINE_MAP_CENTER_TILE_ROW +
                                          LV_OFFLINE_MAP_TILE_ROW_NUM,
                                          state->zoom);
        }
    } else {
        for(col = 0; col < LV_OFFLINE_MAP_TILE_COL_NUM; col++) {
            saved_tile = state->tile[LV_OFFLINE_MAP_TILE_ROW_NUM - 1][col];
            for(row = LV_OFFLINE_MAP_TILE_ROW_NUM - 1; row > 0; row--) {
                state->tile[row][col] = state->tile[row - 1][col];
                lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
            }
            state->tile[0][col] = saved_tile;
            lv_offline_map_load_tile_cell(state, 0, col, old_center_x - LV_OFFLINE_MAP_CENTER_TILE_COL + col,
                                          old_center_y - LV_OFFLINE_MAP_CENTER_TILE_ROW - 1, state->zoom);
        }
    }

    lv_offline_map_raise_track_layer(state);
    lv_offline_map_refresh_track_after_tile_change(state);

    return true;
}

/*********************************************************
 * @brief 把轨迹图层移动到瓦片图层前方
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_raise_track_layer(lv_offline_map_state_t *state)
{
    if(state == NULL) {
        return;
    }
    if(state->passed_track_line != NULL) {
        lv_obj_move_foreground(state->passed_track_line);
    }
    if(state->active_track_line != NULL) {
        lv_obj_move_foreground(state->active_track_line);
    }
    if(state->start_point != NULL) {
        lv_obj_move_foreground(state->start_point);
    }
    if(state->end_point != NULL) {
        lv_obj_move_foreground(state->end_point);
    }
    if(state->arrow != NULL) {
        lv_obj_move_foreground(state->arrow);
    }
}

/*********************************************************
 * @brief 加载指定中心瓦片周围的固定离线瓦片矩阵
 * @param state 控件私有状态，不能为空
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_load_tiles(lv_offline_map_state_t *state, int tile_x, int tile_y, int zoom)
{
    int normalized_zoom; /* 限幅后的缩放级别 */
    int row; /* 当前处理的瓦片行索引 */
    int col; /* 当前处理的瓦片列索引 */

    if(state == NULL || state->container == NULL) {
        return;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    state->center_tile_x = tile_x;
    state->center_tile_y = tile_y;
    state->zoom = normalized_zoom;

    for(row = 0; row < LV_OFFLINE_MAP_TILE_ROW_NUM; row++) {
        for(col = 0; col < LV_OFFLINE_MAP_TILE_COL_NUM; col++) {
            lv_offline_map_load_tile_cell(state, row, col,
                                          state->center_tile_x - LV_OFFLINE_MAP_CENTER_TILE_COL + col,
                                          state->center_tile_y - LV_OFFLINE_MAP_CENTER_TILE_ROW + row,
                                          state->zoom);
        }
    }

    lv_offline_map_raise_track_layer(state);
    lv_offline_map_refresh_track_after_tile_change(state);
}

/*********************************************************
 * @brief 加载瓦片并把视窗滚动回中心
 * @param state 控件私有状态，不能为空
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_refresh_tiles(lv_offline_map_state_t *state, int tile_x, int tile_y, int zoom)
{
    lv_offline_map_load_tiles(state, tile_x, tile_y, zoom);
    lv_offline_map_scroll_to_center(state);
    lv_offline_map_refresh(state->root);
}

/*********************************************************
 * @brief 设置缩放动画轴心到当前地图锚点
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_set_zoom_animation_pivot(lv_offline_map_state_t *state)
{
    lv_coord_t anchor_x; /* 缩放动画轴心在地图可视区域内的横向坐标 */
    lv_coord_t anchor_y; /* 缩放动画轴心在地图可视区域内的纵向坐标 */
    lv_coord_t pivot_x; /* 缩放动画轴心在内容层坐标系内的横向坐标 */
    lv_coord_t pivot_y; /* 缩放动画轴心在内容层坐标系内的纵向坐标 */
    lv_coord_t view_width; /* 地图容器当前可视宽度，单位为像素 */
    lv_coord_t view_height; /* 地图容器当前可视高度，单位为像素 */

    if(state == NULL || state->container == NULL || state->content_layer == NULL) {
        return;
    }

    if(state->follow_timer != NULL && state->follow_auto_center) {
        lv_offline_map_get_follow_anchor(state, &anchor_x, &anchor_y);
    } else {
        view_width = lv_obj_get_width(state->container);
        view_height = lv_obj_get_height(state->container);
        if(view_width <= 0) {
            view_width = state->config.view_width;
        }
        if(view_height <= 0) {
            view_height = state->config.view_height;
        }
        anchor_x = view_width / 2;
        anchor_y = view_height / 2;
    }

    /* 内容层坐标需要叠加当前滚动偏移，确保动画围绕屏幕锚点缩放。 */
    pivot_x = lv_obj_get_scroll_x(state->container) + anchor_x;
    pivot_y = lv_obj_get_scroll_y(state->container) + anchor_y;
    lv_obj_set_style_transform_pivot_x(state->content_layer, pivot_x, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_pivot_y(state->content_layer, pivot_y, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/*********************************************************
 * @brief 执行缩放切换动画的缩放值更新
 * @param var 动画绑定对象，必须为地图内容层对象
 * @param value 当前缩放比例，256 表示原始尺寸
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_zoom_anim_exec_cb(void *var, int32_t value)
{
    lv_obj_t *container = (lv_obj_t *)var; /* 动画绑定的地图内容层对象，由 LVGL 动画系统传入 */

    if(container == NULL) {
        return;
    }

    LV_OFFLINE_MAP_OBJ_SET_TRANSFORM_SCALE(container, value);
}

/*********************************************************
 * @brief 根据缩放方向和当前地图锚点启动内容层过渡动画
 * @param state 控件私有状态，不能为空
 * @param previous_zoom 切换前缩放级别
 * @param next_zoom 切换后缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_start_zoom_animation(lv_offline_map_state_t *state, int previous_zoom, int next_zoom)
{
    lv_anim_t animation; /* 本次缩放切换使用的 LVGL 动画描述 */
    uint16_t anim_time_ms; /* 本次缩放动画时长，单位为毫秒 */
    int32_t start_scale; /* 动画起始缩放比例，256 表示原始尺寸 */

    if(state == NULL || state->container == NULL || state->content_layer == NULL || !state->config.zoom_anim_enable ||
       previous_zoom == next_zoom) {
        return;
    }

    anim_time_ms = state->config.zoom_anim_time_ms;
    if(anim_time_ms == 0U) {
        anim_time_ms = LV_OFFLINE_MAP_DEFAULT_ZOOM_ANIM_TIME_MS;
    }

    if(next_zoom > previous_zoom) {
        start_scale = LV_OFFLINE_MAP_SCALE_NONE - LV_OFFLINE_MAP_ZOOM_ANIM_SCALE_STEP;
    } else {
        start_scale = LV_OFFLINE_MAP_SCALE_NONE + LV_OFFLINE_MAP_ZOOM_ANIM_SCALE_STEP;
    }

    LV_OFFLINE_MAP_ANIM_DELETE(state->content_layer, lv_offline_map_zoom_anim_exec_cb);
    lv_offline_map_set_zoom_animation_pivot(state);
    LV_OFFLINE_MAP_OBJ_SET_TRANSFORM_SCALE(state->content_layer, start_scale);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, state->content_layer);
    lv_anim_set_values(&animation, start_scale, LV_OFFLINE_MAP_SCALE_NONE);
    lv_anim_set_time(&animation, anim_time_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, lv_offline_map_zoom_anim_exec_cb);
    lv_anim_start(&animation);
}

/*********************************************************
 * @brief 根据拖动位置重载相邻瓦片并保持视野连续
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_shift_tiles_by_scroll(lv_offline_map_state_t *state)
{
    lv_coord_t scroll_x; /* 当前横向滚动值 */
    lv_coord_t scroll_y; /* 当前纵向滚动值 */
    lv_coord_t adjusted_x; /* 重载瓦片后折回固定矩阵内的横向滚动值 */
    lv_coord_t adjusted_y; /* 重载瓦片后折回固定矩阵内的纵向滚动值 */
    int tile_delta_x; /* 本次中心瓦片 X 增量 */
    int tile_delta_y; /* 本次中心瓦片 Y 增量 */

    if(state == NULL || state->container == NULL || state->tile_switching) {
        return;
    }

    lv_offline_map_note_user_scroll(state);

    scroll_x = lv_obj_get_scroll_x(state->container);
    scroll_y = lv_obj_get_scroll_y(state->container);
    tile_delta_x = lv_offline_map_get_tile_delta(state, scroll_x - lv_offline_map_get_center_scroll_x(state));
    tile_delta_y = lv_offline_map_get_tile_delta(state, scroll_y - lv_offline_map_get_center_scroll_y(state));
    if(tile_delta_x == 0 && tile_delta_y == 0) {
        return;
    }

    adjusted_x = scroll_x - tile_delta_x * state->config.tile_size;
    adjusted_y = scroll_y - tile_delta_y * state->config.tile_size;

    /* 只复用固定矩阵边缘瓦片，不改变可视区域尺寸，让拖动体验保持连续。 */
    state->tile_switching = true;
    if(!lv_offline_map_shift_tile_matrix(state, tile_delta_x, tile_delta_y)) {
        lv_offline_map_load_tiles(state, state->center_tile_x + tile_delta_x,
                                  state->center_tile_y + tile_delta_y, state->zoom);
    }
    lv_obj_scroll_to(state->container, adjusted_x, adjusted_y, LV_ANIM_OFF);
    state->tile_switching = false;
}

/*********************************************************
 * @brief 更新缩放按钮和缩放级别标签状态
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_update_zoom_controls(lv_offline_map_state_t *state)
{
    char text[8]; /* 缩放级别标签文本缓冲区 */

    if(state == NULL) {
        return;
    }

    if(state->zoom_label != NULL) {
        snprintf(text, sizeof(text), "Z%d", state->zoom);
        lv_label_set_text(state->zoom_label, text);
    }

    if(state->zoom_in_button != NULL) {
        if(state->zoom >= state->config.max_zoom) {
            lv_obj_add_state(state->zoom_in_button, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(state->zoom_in_button, LV_STATE_DISABLED);
        }
    }

    if(state->zoom_out_button != NULL) {
        if(state->zoom <= state->config.min_zoom) {
            lv_obj_add_state(state->zoom_out_button, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(state->zoom_out_button, LV_STATE_DISABLED);
        }
    }
}

/*********************************************************
 * @brief 更新轨迹起点和终点标记位置
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_update_track_markers(lv_offline_map_state_t *state)
{
    lv_offline_map_point_t *start; /* 轨迹起点坐标，指向当前容器坐标数组 */
    lv_offline_map_point_t *end; /* 轨迹终点坐标，指向当前容器坐标数组 */

    if(state == NULL || state->start_point == NULL || state->end_point == NULL) {
        return;
    }

    if(state->track_point_count == 0U) {
        lv_obj_add_flag(state->start_point, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->end_point, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    start = &state->track_points[0];
    end = &state->track_points[state->track_point_count - 1U];
    lv_obj_remove_flag(state->start_point, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(state->start_point, (lv_coord_t)start->x - LV_OFFLINE_MAP_TRACK_POINT_SIZE / 2,
                   (lv_coord_t)start->y - LV_OFFLINE_MAP_TRACK_POINT_SIZE / 2);

    lv_obj_remove_flag(state->end_point, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(state->end_point, (lv_coord_t)end->x - LV_OFFLINE_MAP_TRACK_POINT_SIZE / 2,
                   (lv_coord_t)end->y - LV_OFFLINE_MAP_TRACK_POINT_SIZE / 2);
}

/*********************************************************
 * @brief 刷新轨迹折线显示
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_refresh_track(lv_offline_map_state_t *state)
{
    uint16_t index; /* 当前换算的轨迹点索引 */
    uint16_t passed_point_count; /* 已行驶折线使用的点数量 */
    uint16_t active_point_count; /* 未行驶折线使用的点数量 */
    uint16_t active_index; /* 未行驶轨迹数组写入索引 */
    lv_offline_map_point_t follow_point; /* 当前跟随位置对应的容器坐标 */
    lv_offline_map_point_t *active_points; /* 未行驶线段起始点数组 */

    if(state == NULL || state->active_track_line == NULL || state->passed_track_line == NULL ||
       state->track_batch_updating || state->track_points == NULL || state->track_records == NULL) {
        return;
    }

    if(state->track_point_count == 0U) {
        state->track_travel_index = 0;
    } else if(state->track_travel_index >= state->track_point_count) {
        state->track_travel_index = state->track_point_count - 1U;
    }

    for(index = 0; index < state->track_point_count; index++) {
        state->track_points[index] = lv_offline_map_convert_track_point(state, &state->track_records[index]);
    }

    passed_point_count = 0U;
    active_point_count = 0U;
    active_points = state->track_points;
    if(state->track_point_count > 0U && state->follow_position_valid) {
        follow_point = lv_offline_map_global_pixel_to_container_point(state, state->follow_global_x,
                                                                      state->follow_global_y);
        passed_point_count = state->follow_segment_index + 1U;
        if(passed_point_count > state->track_point_count) {
            passed_point_count = state->track_point_count;
        }

        memcpy(state->passed_track_points, state->track_points,
               passed_point_count * sizeof(state->passed_track_points[0]));
        if(passed_point_count < state->track_draw_capacity) {
            state->passed_track_points[passed_point_count] = follow_point;
            passed_point_count++;
        }

        active_points = state->active_track_points;
        active_points[0] = follow_point;
        active_point_count = 1U;
        for(active_index = state->follow_segment_index + 1U;
            active_index < state->track_point_count && active_point_count < state->track_draw_capacity;
            active_index++) {
            active_points[active_point_count] = state->track_points[active_index];
            active_point_count++;
        }
    } else if(state->track_point_count > 0U) {
        passed_point_count = state->track_travel_index + 1U;
        if(passed_point_count > state->track_point_count) {
            passed_point_count = state->track_point_count;
        }

        memcpy(state->passed_track_points, state->track_points,
               passed_point_count * sizeof(state->passed_track_points[0]));
        active_points = &state->track_points[state->track_travel_index];
        active_point_count = state->track_point_count - state->track_travel_index;
    }

    if(passed_point_count < 2U) {
        lv_line_set_points(state->passed_track_line, state->passed_track_points, 0);
    } else {
        lv_line_set_points(state->passed_track_line, state->passed_track_points, passed_point_count);
    }

    if(active_point_count < 2U) {
        lv_line_set_points(state->active_track_line, active_points, 0);
    } else {
        lv_line_set_points(state->active_track_line, active_points, active_point_count);
    }

    lv_offline_map_update_track_markers(state);
    lv_offline_map_update_arrow_position(state);
    lv_obj_invalidate(state->passed_track_line);
    lv_obj_invalidate(state->active_track_line);
    LV_OFFLINE_MAP_OBJ_SEND_EVENT(state->root, LV_EVENT_VALUE_CHANGED, NULL);
}

/*********************************************************
 * @brief 创建轨迹起点或终点标记
 * @param parent 父对象指针，不能为空
 * @param color 标记颜色
 * @return lv_obj_t* 创建成功的标记对象
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_obj_t *lv_offline_map_create_track_marker(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *marker = lv_obj_create(parent); /* 圆形轨迹端点标记对象 */

    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, LV_OFFLINE_MAP_TRACK_POINT_SIZE, LV_OFFLINE_MAP_TRACK_POINT_SIZE);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(marker, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(marker, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(marker, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(marker, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(marker, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return marker;
}

/*********************************************************
 * @brief 执行默认车标光圈的扩散呼吸动画
 * @param var 动画绑定的光圈对象，必须为 LVGL 对象
 * @param value 当前动画进度，范围由 LVGL 动画系统传入
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_vehicle_halo_anim_exec_cb(void *var, int32_t value)
{
    lv_obj_t *halo = (lv_obj_t *)var; /* 正在播放扩散呼吸效果的光圈对象 */
    lv_coord_t size; /* 当前光圈直径，单位为像素 */
    lv_coord_t offset; /* 光圈居中到车标容器时使用的左上角偏移 */
    lv_opa_t opacity; /* 当前光圈透明度，随扩散逐步降低 */

    if(halo == NULL) {
        return;
    }

    if(value < 0) {
        value = 0;
    } else if(value > 255) {
        value = 255;
    }

    size = LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE +
           (lv_coord_t)((LV_OFFLINE_MAP_VEHICLE_HALO_MAX_SIZE - LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE) *
                        value / 255);
    offset = (LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE - size) / 2;
    opacity = (lv_opa_t)(LV_OFFLINE_MAP_VEHICLE_HALO_START_OPA -
                         (LV_OFFLINE_MAP_VEHICLE_HALO_START_OPA - LV_OFFLINE_MAP_VEHICLE_HALO_END_OPA) *
                         value / 255);

    lv_obj_set_size(halo, size, size);
    lv_obj_set_pos(halo, offset, offset);
    lv_obj_set_style_bg_opa(halo, opacity, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(halo, opacity, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/*********************************************************
 * @brief 创建默认点状车辆车标并启动光圈呼吸动画
 * @param state 控件私有状态，不能为空
 * @return lv_obj_t* 创建成功的车标容器对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_obj_t *lv_offline_map_create_vehicle_marker(lv_offline_map_state_t *state)
{
    lv_obj_t *marker; /* 默认点状车标外层容器对象 */
    lv_anim_t halo_anim; /* 光圈扩散呼吸动画描述 */

    if(state == NULL || state->content_layer == NULL) {
        return NULL;
    }

    marker = lv_obj_create(state->content_layer);
    if(marker == NULL) {
        return NULL;
    }

    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE, LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE);
    lv_obj_set_align(marker, LV_ALIGN_TOP_LEFT);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    state->vehicle_halo = lv_obj_create(marker);
    if(state->vehicle_halo == NULL) {
        LV_OFFLINE_MAP_OBJ_DELETE(marker);
        return NULL;
    }
    lv_obj_remove_style_all(state->vehicle_halo);
    lv_obj_set_size(state->vehicle_halo, LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE,
                    LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE);
    lv_obj_set_pos(state->vehicle_halo,
                   (LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE - LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE) / 2,
                   (LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE - LV_OFFLINE_MAP_VEHICLE_HALO_MIN_SIZE) / 2);
    lv_obj_set_style_radius(state->vehicle_halo, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(state->vehicle_halo, lv_color_hex(LV_OFFLINE_MAP_VEHICLE_HALO_COLOR),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(state->vehicle_halo, LV_OFFLINE_MAP_VEHICLE_HALO_START_OPA,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(state->vehicle_halo, lv_color_hex(LV_OFFLINE_MAP_VEHICLE_HALO_COLOR),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(state->vehicle_halo, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(state->vehicle_halo, LV_OFFLINE_MAP_VEHICLE_HALO_START_OPA,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(state->vehicle_halo, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    state->vehicle_dot = lv_obj_create(marker);
    if(state->vehicle_dot == NULL) {
        LV_OFFLINE_MAP_OBJ_DELETE(marker);
        state->vehicle_halo = NULL;
        return NULL;
    }
    lv_obj_remove_style_all(state->vehicle_dot);
    lv_obj_set_size(state->vehicle_dot, LV_OFFLINE_MAP_VEHICLE_DOT_SIZE, LV_OFFLINE_MAP_VEHICLE_DOT_SIZE);
    lv_obj_set_pos(state->vehicle_dot, (LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE - LV_OFFLINE_MAP_VEHICLE_DOT_SIZE) / 2,
                   (LV_OFFLINE_MAP_VEHICLE_MARKER_SIZE - LV_OFFLINE_MAP_VEHICLE_DOT_SIZE) / 2);
    lv_obj_set_style_radius(state->vehicle_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(state->vehicle_dot, lv_color_hex(LV_OFFLINE_MAP_VEHICLE_DOT_COLOR),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(state->vehicle_dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(state->vehicle_dot, lv_color_hex(LV_OFFLINE_MAP_VEHICLE_DOT_BORDER_COLOR),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(state->vehicle_dot, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(state->vehicle_dot, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(state->vehicle_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 光圈对象按尺寸和透明度循环变化，形成车辆移动时的呼吸扩散感。 */
    lv_anim_init(&halo_anim);
    lv_anim_set_var(&halo_anim, state->vehicle_halo);
    lv_anim_set_values(&halo_anim, 0, 255);
    lv_anim_set_time(&halo_anim, LV_OFFLINE_MAP_VEHICLE_HALO_ANIM_MS);
    lv_anim_set_path_cb(&halo_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&halo_anim, lv_offline_map_vehicle_halo_anim_exec_cb);
    lv_anim_set_repeat_count(&halo_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&halo_anim);

    return marker;
}

/*********************************************************
 * @brief 创建地图缩放按钮
 * @param state 控件私有状态，不能为空
 * @param text 按钮文本，不能为空
 * @param y_offset 相对控件顶部的纵向偏移
 * @return lv_obj_t* 创建成功的按钮对象
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_obj_t *lv_offline_map_create_zoom_button(lv_offline_map_state_t *state, const char *text,
                                                   lv_coord_t y_offset)
{
    lv_obj_t *button = LV_OFFLINE_MAP_BUTTON_CREATE(state->root); /* 缩放按钮对象 */
    lv_obj_t *label; /* 按钮内部文本标签 */

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, LV_OFFLINE_MAP_ZOOM_BUTTON_SIZE, LV_OFFLINE_MAP_ZOOM_BUTTON_SIZE);
    lv_obj_set_align(button, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(button, LV_OFFLINE_MAP_ZOOM_BUTTON_X);
    lv_obj_set_y(button, y_offset);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x111820), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 120, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(button, 160, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    if(state->config.zoom_button_font != NULL) {
        lv_obj_set_style_text_font(label, state->config.zoom_button_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return button;
}

/*********************************************************
 * @brief 构造新的离线地图控件对象
 * @param class_p LVGL 类描述指针，不能为空
 * @param obj 正在初始化的离线地图对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    lv_offline_map_state_t *state = (lv_offline_map_state_t *)obj; /* 当前构造的离线地图控件状态 */

    (void)class_p;
    state->root = obj;
}

/*********************************************************
 * @brief 销毁离线地图控件对象并释放动态资源
 * @param class_p LVGL 类描述指针，不能为空
 * @param obj 正在销毁的离线地图对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    lv_offline_map_state_t *state = (lv_offline_map_state_t *)obj; /* 正在销毁的离线地图控件状态 */

    (void)class_p;
    lv_offline_map_stop_track_follow(state);
    lv_offline_map_stop_auto_center_timer(state);
    lv_offline_map_release_track_buffers(state);
}

/*********************************************************
 * @brief 处理离线地图控件类事件
 * @param class_p LVGL 类描述指针，不能为空
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_class_event(const lv_obj_class_t *class_p, lv_event_t *e)
{
    lv_res_t result; /* 基类事件处理结果，非 OK 时停止后续控件事件处理 */
    lv_obj_t *current_target = lv_event_get_current_target(e); /* 当前接收类事件的控件对象 */
    lv_offline_map_state_t *state = lv_offline_map_get_state(current_target); /* 当前控件私有状态 */

    result = lv_obj_event_base(class_p, e);
    if(result != LV_RES_OK) {
        return;
    }

    lv_offline_map_handle_event(state, e);
}

/*********************************************************
 * @brief 创建地图缩放控件
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_create_zoom_controls(lv_offline_map_state_t *state)
{
    if(state == NULL || !state->config.show_zoom_controls) {
        return;
    }

    state->zoom_in_button = lv_offline_map_create_zoom_button(state, "+", LV_OFFLINE_MAP_ZOOM_IN_Y);
    state->zoom_out_button = lv_offline_map_create_zoom_button(state, "-", LV_OFFLINE_MAP_ZOOM_OUT_Y);
    state->zoom_label = lv_label_create(state->root);
    lv_obj_set_width(state->zoom_label, LV_OFFLINE_MAP_ZOOM_BUTTON_SIZE);
    lv_obj_set_height(state->zoom_label, LV_SIZE_CONTENT);
    lv_obj_set_align(state->zoom_label, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(state->zoom_label, LV_OFFLINE_MAP_ZOOM_BUTTON_X);
    lv_obj_set_y(state->zoom_label, LV_OFFLINE_MAP_ZOOM_LABEL_Y);
    lv_obj_set_style_text_color(state->zoom_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(state->zoom_label, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(state->zoom_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if(state->config.zoom_label_font != NULL) {
        lv_obj_set_style_text_font(state->zoom_label, state->config.zoom_label_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_remove_flag(state->zoom_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/*********************************************************
 * @brief 处理地图控件内部交互事件
 * @param state 控件私有状态，不能为空
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_handle_event(lv_offline_map_state_t *state, lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e); /* 当前 LVGL 事件类型 */
    lv_obj_t *current_target = lv_event_get_current_target(e); /* 当前注册此回调的对象 */
    lv_obj_t *target = lv_event_get_target(e); /* 原始触发事件的对象 */

    if(state == NULL) {
        return;
    }

    if(event_code == LV_EVENT_SCROLL && target == state->container) {
        if(!state->tile_switching && state->follow_position_valid) {
            /* 用户拖图后先保留当前位置视野，空闲 5 秒再恢复车标自动居中。 */
            lv_offline_map_note_user_scroll(state);
        }
        lv_offline_map_shift_tiles_by_scroll(state);
    } else if(event_code == LV_EVENT_VALUE_CHANGED && current_target == state->root && target == state->root) {
        return;
    } else if(event_code == LV_EVENT_CLICKED && target == state->zoom_in_button) {
        lv_offline_map_zoom_in(state->root);
    } else if(event_code == LV_EVENT_CLICKED && target == state->zoom_out_button) {
        lv_offline_map_zoom_out(state->root);
    }
}

/*********************************************************
 * @brief 创建地图瓦片容器和轨迹图层
 * @param state 控件私有状态，不能为空
 * @return bool true 表示创建成功，false 表示创建失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_create_content(lv_offline_map_state_t *state)
{
    int row; /* 当前创建的瓦片行索引 */
    int col; /* 当前创建的瓦片列索引 */

    if(state == NULL || state->root == NULL) {
        return false;
    }

    state->container = lv_obj_create(state->root);
    lv_obj_remove_style_all(state->container);
    lv_obj_add_flag(state->container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(state->container, state->config.view_width, state->config.view_height);
    lv_obj_set_align(state->container, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(state->container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(state->container, lv_color_hex(0x20262D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(state->container, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    state->content_layer = lv_obj_create(state->container);
    lv_obj_remove_style_all(state->content_layer);
    lv_obj_set_size(state->content_layer, LV_OFFLINE_MAP_TILE_COL_NUM * state->config.tile_size,
                    LV_OFFLINE_MAP_TILE_ROW_NUM * state->config.tile_size);
    lv_obj_set_align(state->content_layer, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(state->content_layer, 0, 0);
    lv_obj_remove_flag(state->content_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for(row = 0; row < LV_OFFLINE_MAP_TILE_ROW_NUM; row++) {
        for(col = 0; col < LV_OFFLINE_MAP_TILE_COL_NUM; col++) {
            state->tile[row][col] = LV_OFFLINE_MAP_IMAGE_CREATE(state->content_layer);
            lv_obj_set_size(state->tile[row][col], state->config.tile_size, state->config.tile_size);
            lv_obj_set_align(state->tile[row][col], LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(state->tile[row][col], col * state->config.tile_size, row * state->config.tile_size);
            lv_obj_remove_flag(state->tile[row][col], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    state->passed_track_line = lv_line_create(state->content_layer);
    lv_obj_set_size(state->passed_track_line, LV_OFFLINE_MAP_TILE_COL_NUM * state->config.tile_size,
                    LV_OFFLINE_MAP_TILE_ROW_NUM * state->config.tile_size);
    lv_obj_set_align(state->passed_track_line, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(state->passed_track_line, 0, 0);
    lv_obj_set_style_line_width(state->passed_track_line, LV_OFFLINE_MAP_TRACK_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(state->passed_track_line, lv_color_hex(LV_OFFLINE_MAP_TRACK_PASSED_COLOR),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(state->passed_track_line, 210, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(state->passed_track_line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    state->active_track_line = lv_line_create(state->content_layer);
    lv_obj_set_size(state->active_track_line, LV_OFFLINE_MAP_TILE_COL_NUM * state->config.tile_size,
                    LV_OFFLINE_MAP_TILE_ROW_NUM * state->config.tile_size);
    lv_obj_set_align(state->active_track_line, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(state->active_track_line, 0, 0);
    lv_obj_set_style_line_width(state->active_track_line, LV_OFFLINE_MAP_TRACK_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(state->active_track_line, lv_color_hex(LV_OFFLINE_MAP_TRACK_ACTIVE_COLOR),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(state->active_track_line, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(state->active_track_line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    state->start_point = lv_offline_map_create_track_marker(state->content_layer,
                                                            lv_color_hex(LV_OFFLINE_MAP_TRACK_ACTIVE_COLOR));
    state->end_point = lv_offline_map_create_track_marker(state->content_layer,
                                                          lv_color_hex(LV_OFFLINE_MAP_TRACK_END_COLOR));
    if(state->config.arrow_src != NULL) {
        state->arrow = LV_OFFLINE_MAP_IMAGE_CREATE(state->content_layer);
        LV_OFFLINE_MAP_IMAGE_SET_SRC(state->arrow, state->config.arrow_src);
        LV_OFFLINE_MAP_IMAGE_SET_PIVOT(state->arrow, state->config.arrow_pivot_x, state->config.arrow_pivot_y);
        LV_OFFLINE_MAP_IMAGE_SET_ROTATION(state->arrow, state->arrow_rotation);
        lv_obj_set_align(state->arrow, LV_ALIGN_TOP_LEFT);
        lv_obj_add_flag(state->arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->arrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    } else if(state->config.show_vehicle_marker) {
        state->arrow = lv_offline_map_create_vehicle_marker(state);
    }

    lv_offline_map_create_zoom_controls(state);

    return true;
}

/*********************************************************
 * @brief 校验并修正地图配置
 * @param config 输入输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_normalize_config(lv_offline_map_config_t *config)
{
    if(config == NULL) {
        return;
    }

    if(config->map_dir == NULL) {
        config->map_dir = LV_OFFLINE_MAP_DEFAULT_DIR;
    }
    if(config->tile_file_name == NULL) {
        config->tile_file_name = LV_OFFLINE_MAP_DEFAULT_TILE_FILE;
    }
    if(config->tile_size <= 0) {
        config->tile_size = LV_OFFLINE_MAP_DEFAULT_TILE_SIZE;
    }
    if(config->view_width <= 0) {
        config->view_width = LV_OFFLINE_MAP_DEFAULT_VIEW_WIDTH;
    }
    if(config->view_height <= 0) {
        config->view_height = LV_OFFLINE_MAP_DEFAULT_VIEW_HEIGHT;
    }
    if(config->min_zoom <= 0) {
        config->min_zoom = LV_OFFLINE_MAP_DEFAULT_MIN_ZOOM;
    }
    if(config->max_zoom < config->min_zoom) {
        config->max_zoom = config->min_zoom;
    }
    if(config->default_zoom < config->min_zoom || config->default_zoom > config->max_zoom) {
        config->default_zoom = config->max_zoom;
    }
    if(config->track_max_points <= 0) {
        config->track_max_points = LV_OFFLINE_MAP_DEFAULT_TRACK_MAX_POINTS;
    }
    if(config->track_max_points > UINT16_MAX - LV_OFFLINE_MAP_TRACK_DRAW_EXTRA_POINTS) {
        config->track_max_points = UINT16_MAX - LV_OFFLINE_MAP_TRACK_DRAW_EXTRA_POINTS;
    }
    if(config->zoom_anim_time_ms == 0U) {
        config->zoom_anim_time_ms = LV_OFFLINE_MAP_DEFAULT_ZOOM_ANIM_TIME_MS;
    }
    if(config->follow_speed_kmh <= 0.0) {
        config->follow_speed_kmh = LV_OFFLINE_MAP_DEFAULT_FOLLOW_SPEED_KMH;
    }
}

/*********************************************************
 * @brief 分配轨迹缓存
 * @param state 控件私有状态，不能为空
 * @return bool true 表示分配成功，false 表示内存不足
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_alloc_track_buffers(lv_offline_map_state_t *state)
{
    uint16_t capacity; /* 原始轨迹缓存容量 */
    uint16_t draw_capacity; /* 绘制轨迹缓存容量 */

    if(state == NULL) {
        return false;
    }

    capacity = (uint16_t)state->config.track_max_points;
    draw_capacity = capacity + LV_OFFLINE_MAP_TRACK_DRAW_EXTRA_POINTS;
    state->track_records = (lv_offline_map_track_point_t *)lv_malloc(sizeof(state->track_records[0]) * capacity);
    state->track_distance_m = (uint32_t *)lv_malloc(sizeof(state->track_distance_m[0]) * capacity);
    state->track_points = (lv_offline_map_point_t *)lv_malloc(sizeof(state->track_points[0]) * capacity);
    state->passed_track_points = (lv_offline_map_point_t *)lv_malloc(sizeof(state->passed_track_points[0]) *
                                                                     draw_capacity);
    state->active_track_points = (lv_offline_map_point_t *)lv_malloc(sizeof(state->active_track_points[0]) *
                                                                     draw_capacity);
    if(state->track_records == NULL || state->track_distance_m == NULL || state->track_points == NULL ||
       state->passed_track_points == NULL || state->active_track_points == NULL) {
        lv_free(state->track_records);
        lv_free(state->track_distance_m);
        lv_free(state->track_points);
        lv_free(state->passed_track_points);
        lv_free(state->active_track_points);
        state->track_records = NULL;
        state->track_distance_m = NULL;
        state->track_points = NULL;
        state->passed_track_points = NULL;
        state->active_track_points = NULL;
        return false;
    }

    state->track_capacity = capacity;
    state->track_draw_capacity = draw_capacity;

    return true;
}

/*********************************************************
 * @brief 释放轨迹动态缓存并重置缓存指针
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_release_track_buffers(lv_offline_map_state_t *state)
{
    if(state == NULL) {
        return;
    }

    lv_free(state->track_records);
    lv_free(state->track_distance_m);
    lv_free(state->track_points);
    lv_free(state->passed_track_points);
    lv_free(state->active_track_points);
    state->track_records = NULL;
    state->track_distance_m = NULL;
    state->track_points = NULL;
    state->passed_track_points = NULL;
    state->active_track_points = NULL;
    state->track_capacity = 0U;
    state->track_draw_capacity = 0U;
    state->track_point_count = 0U;
    state->track_travel_index = 0U;
    state->track_total_distance_m = 0U;
    state->track_traveled_distance_m = 0U;
}

/*********************************************************
 * @brief 根据地图控件对象获取私有状态
 * @param map 离线地图控件对象，允许为空
 * @return lv_offline_map_state_t* 私有状态指针，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_offline_map_state_t *lv_offline_map_get_state(lv_obj_t *map)
{
    if(map == NULL) {
        return NULL;
    }

    if(!lv_obj_has_class(map, LV_OFFLINE_MAP_CLASS)) {
        return NULL;
    }

    return (lv_offline_map_state_t *)map;
}

/*********************************************************
 * @brief 获取离线地图控件默认配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_get_default_config(lv_offline_map_config_t *config)
{
    if(config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->map_dir = LV_OFFLINE_MAP_DEFAULT_DIR;
    config->tile_file_name = LV_OFFLINE_MAP_DEFAULT_TILE_FILE;
    config->tile_size = LV_OFFLINE_MAP_DEFAULT_TILE_SIZE;
    config->view_width = LV_OFFLINE_MAP_DEFAULT_VIEW_WIDTH;
    config->view_height = LV_OFFLINE_MAP_DEFAULT_VIEW_HEIGHT;
    config->min_zoom = LV_OFFLINE_MAP_DEFAULT_MIN_ZOOM;
    config->max_zoom = LV_OFFLINE_MAP_DEFAULT_MAX_ZOOM;
    config->default_zoom = LV_OFFLINE_MAP_DEFAULT_ZOOM;
    config->default_tile_x = LV_OFFLINE_MAP_DEFAULT_TILE_X;
    config->default_tile_y = LV_OFFLINE_MAP_DEFAULT_TILE_Y;
    config->track_max_points = LV_OFFLINE_MAP_DEFAULT_TRACK_MAX_POINTS;
    config->use_gcj02_tile = true;
    config->show_zoom_controls = true;
    config->show_vehicle_marker = false;
    config->zoom_anim_enable = true;
    config->zoom_anim_time_ms = LV_OFFLINE_MAP_DEFAULT_ZOOM_ANIM_TIME_MS;
    config->follow_speed_kmh = LV_OFFLINE_MAP_DEFAULT_FOLLOW_SPEED_KMH;
    config->arrow_pivot_x = 0;
    config->arrow_pivot_y = 0;
    config->arrow_y_offset = 0;
}

/*********************************************************
 * @brief 创建使用默认配置的离线地图控件
 * @param parent 父对象指针，不能为空
 * @return lv_obj_t* 创建成功的离线地图控件对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *lv_offline_map_create(lv_obj_t *parent)
{
    lv_obj_t *map = lv_offline_map_create_with_config(parent, NULL); /* 使用默认配置创建的离线地图控件对象 */

    return map;
}

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
lv_obj_t *lv_offline_map_create_with_config(lv_obj_t *parent, const lv_offline_map_config_t *config)
{
    lv_offline_map_config_t normalized_config; /* 修正后的控件配置副本 */
    lv_offline_map_state_t *state; /* 新创建控件的私有状态 */
    lv_obj_t *map; /* 离线地图控件根对象 */

    if(parent == NULL) {
        return NULL;
    }

    if(config == NULL) {
        lv_offline_map_get_default_config(&normalized_config);
    } else {
        normalized_config = *config;
        lv_offline_map_normalize_config(&normalized_config);
    }

    map = lv_obj_class_create_obj(LV_OFFLINE_MAP_CLASS, parent);
    if(map == NULL) {
        return NULL;
    }

    state = (lv_offline_map_state_t *)map;
    state->config = normalized_config;
    state->center_tile_x = state->config.default_tile_x;
    state->center_tile_y = state->config.default_tile_y;
    state->zoom = state->config.default_zoom;
    state->root = map;

    lv_obj_class_init_obj(map);

    if(!lv_offline_map_alloc_track_buffers(state)) {
        LV_OFFLINE_MAP_OBJ_DELETE(map);
        return NULL;
    }

    lv_obj_remove_style_all(map);
    lv_obj_add_flag(map, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(map, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(map, state->config.view_width, state->config.view_height);
    lv_obj_set_style_bg_color(map, lv_color_hex(0x20262D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(map, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    if(!lv_offline_map_create_content(state)) {
        LV_OFFLINE_MAP_OBJ_DELETE(map);
        return NULL;
    }

    lv_offline_map_refresh_tiles(state, state->center_tile_x, state->center_tile_y, state->zoom);

    return map;
}

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
bool lv_offline_map_set_config(lv_obj_t *map, const lv_offline_map_config_t *config)
{
    lv_offline_map_state_t *state; /* 控件私有状态 */
    lv_offline_map_config_t normalized_config; /* 修正后的新配置 */

    state = lv_offline_map_get_state(map);
    if(state == NULL || config == NULL) {
        return false;
    }

    normalized_config = *config;
    lv_offline_map_normalize_config(&normalized_config);
    state->config = normalized_config;
    state->center_tile_x = state->config.default_tile_x;
    state->center_tile_y = state->config.default_tile_y;
    state->zoom = lv_offline_map_normalize_zoom(state, state->config.default_zoom);
    lv_obj_set_size(state->root, state->config.view_width, state->config.view_height);
    lv_obj_set_size(state->container, state->config.view_width, state->config.view_height);
    if(state->content_layer != NULL) {
        lv_obj_set_size(state->content_layer, LV_OFFLINE_MAP_TILE_COL_NUM * state->config.tile_size,
                        LV_OFFLINE_MAP_TILE_ROW_NUM * state->config.tile_size);
    }
    lv_offline_map_refresh_tiles(state, state->center_tile_x, state->center_tile_y, state->zoom);

    return true;
}

/*********************************************************
 * @brief 获取离线地图控件内部滚动容器
 * @param map 离线地图控件对象，不能为空
 * @return lv_obj_t* 地图滚动容器对象，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *lv_offline_map_get_container(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return NULL;
    }

    return state->container;
}

/*********************************************************
 * @brief 刷新当前中心瓦片、轨迹和缩放控件显示
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_refresh(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    lv_offline_map_update_zoom_controls(state);
    lv_offline_map_refresh_track(state);
}

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
void lv_offline_map_center_tile(lv_obj_t *map, int tile_x, int tile_y, int zoom)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    lv_offline_map_refresh_tiles(state, tile_x, tile_y, zoom);
}

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
bool lv_offline_map_center_lonlat_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int tile_x; /* 经纬度换算得到的中心瓦片 X 坐标 */
    int tile_y; /* 经纬度换算得到的中心瓦片 Y 坐标 */
    int pixel_x; /* 经纬度换算得到的瓦片内横向像素坐标 */
    int pixel_y; /* 经纬度换算得到的瓦片内纵向像素坐标 */
    int global_x; /* 经纬度换算得到的全局横向像素坐标 */
    int global_y; /* 经纬度换算得到的全局纵向像素坐标 */
    int normalized_zoom; /* 限幅后的缩放级别 */
    bool centered; /* 经纬度居中结果，true 表示视野已移动到目标点 */

    if(state == NULL || state->container == NULL) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    if(!lv_offline_map_lonlat_to_tile_point(state, lon_e7, lat_e7, normalized_zoom,
                                            &tile_x, &tile_y, &pixel_x, &pixel_y)) {
        return false;
    }

    global_x = tile_x * state->config.tile_size + pixel_x;
    global_y = tile_y * state->config.tile_size + pixel_y;
    centered = lv_offline_map_center_global_pixel(state, global_x, global_y, normalized_zoom, LV_ANIM_OFF);

    return centered;
}

/*********************************************************
 * @brief 按 E7 经纬度刷新车辆车标位置
 * @param state 控件私有状态，不能为空
 * @param lon_e7 车辆经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 车辆纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param center true 表示立即把地图视野对齐到车标位置，false 表示只刷新车标
 * @return bool true 表示车标位置已更新，false 表示坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_set_vehicle_location_state(lv_offline_map_state_t *state, int lon_e7, int lat_e7,
                                                      int zoom, bool center)
{
    int tile_x; /* 车辆经纬度换算得到的瓦片 X 坐标 */
    int tile_y; /* 车辆经纬度换算得到的瓦片 Y 坐标 */
    int pixel_x; /* 车辆经纬度换算得到的瓦片内横向像素坐标 */
    int pixel_y; /* 车辆经纬度换算得到的瓦片内纵向像素坐标 */
    int normalized_zoom; /* 限幅后的缩放级别 */
    int vehicle_global_x; /* 车辆位置在当前缩放级别下的全局横向像素坐标 */
    int vehicle_global_y; /* 车辆位置在当前缩放级别下的全局纵向像素坐标 */

    if(state == NULL) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    if(!lv_offline_map_lonlat_to_tile_point(state, lon_e7, lat_e7, normalized_zoom,
                                            &tile_x, &tile_y, &pixel_x, &pixel_y)) {
        return false;
    }

    lv_offline_map_stop_track_follow(state);
    if(center) {
        lv_offline_map_stop_auto_center_timer(state);
    }
    vehicle_global_x = tile_x * state->config.tile_size + pixel_x;
    vehicle_global_y = tile_y * state->config.tile_size + pixel_y;
    state->follow_precise_x = (double)vehicle_global_x;
    state->follow_precise_y = (double)vehicle_global_y;
    state->follow_global_x = vehicle_global_x;
    state->follow_global_y = vehicle_global_y;
    state->follow_position_valid = true;
    state->follow_auto_center = center;
    if(center) {
        /* 默认定位和用户主动回到车标时，按车标锚点显示，保持与导航态视觉一致。 */
        (void)lv_offline_map_center_follow_global_pixel(state, vehicle_global_x, vehicle_global_y,
                                                        normalized_zoom, LV_ANIM_OFF);
    }
    lv_offline_map_refresh_track(state);

    return true;
}

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
bool lv_offline_map_set_vehicle_location_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom, bool center)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    bool updated; /* 车辆车标位置更新结果，true 表示车标已可显示 */

    updated = lv_offline_map_set_vehicle_location_state(state, lon_e7, lat_e7, zoom, center);

    return updated;
}

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
                                            int *lon_e7, int *lat_e7)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int origin_x; /* 当前瓦片矩阵容器左上角对应的全局横向像素坐标 */
    int origin_y; /* 当前瓦片矩阵容器左上角对应的全局纵向像素坐标 */
    int global_x; /* 视窗点对应的当前缩放级别全局横向像素坐标 */
    int global_y; /* 视窗点对应的当前缩放级别全局纵向像素坐标 */
    bool converted; /* true 表示全局像素坐标已成功换算为 WGS84 E7 经纬度 */

    if(state == NULL || state->container == NULL || lon_e7 == NULL || lat_e7 == NULL ||
       view_x < 0 || view_y < 0 || view_x >= state->config.view_width || view_y >= state->config.view_height) {
        return false;
    }

    origin_x = (state->center_tile_x - LV_OFFLINE_MAP_CENTER_TILE_COL) * state->config.tile_size;
    origin_y = (state->center_tile_y - LV_OFFLINE_MAP_CENTER_TILE_ROW) * state->config.tile_size;
    global_x = origin_x + lv_obj_get_scroll_x(state->container) + view_x;
    global_y = origin_y + lv_obj_get_scroll_y(state->container) + view_y;

    converted = lv_offline_map_global_pixel_to_lonlat_e7(state, global_x, global_y, lon_e7, lat_e7);

    return converted;
}

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
                                            lv_coord_t *view_x, lv_coord_t *view_y)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int tile_x; /* 经纬度换算得到的瓦片 X 坐标 */
    int tile_y; /* 经纬度换算得到的瓦片 Y 坐标 */
    int pixel_x; /* 经纬度换算得到的瓦片内横向像素坐标 */
    int pixel_y; /* 经纬度换算得到的瓦片内纵向像素坐标 */
    int origin_x; /* 当前 5x5 瓦片容器左上角对应的全局横向像素坐标 */
    int origin_y; /* 当前 5x5 瓦片容器左上角对应的全局纵向像素坐标 */
    int global_x; /* 经纬度对应的当前缩放级别全局横向像素坐标 */
    int global_y; /* 经纬度对应的当前缩放级别全局纵向像素坐标 */

    if(state == NULL || state->container == NULL || view_x == NULL || view_y == NULL) {
        return false;
    }

    if(!lv_offline_map_lonlat_to_tile_point(state, lon_e7, lat_e7, state->zoom,
                                            &tile_x, &tile_y, &pixel_x, &pixel_y)) {
        return false;
    }

    origin_x = (state->center_tile_x - LV_OFFLINE_MAP_CENTER_TILE_COL) * state->config.tile_size;
    origin_y = (state->center_tile_y - LV_OFFLINE_MAP_CENTER_TILE_ROW) * state->config.tile_size;
    global_x = tile_x * state->config.tile_size + pixel_x;
    global_y = tile_y * state->config.tile_size + pixel_y;
    *view_x = (lv_coord_t)(global_x - origin_x - lv_obj_get_scroll_x(state->container));
    *view_y = (lv_coord_t)(global_y - origin_y - lv_obj_get_scroll_y(state->container));

    return true;
}

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
void lv_offline_map_scroll_to_tile_pixel(lv_obj_t *map, int pixel_x, int pixel_y, lv_anim_enable_t anim)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL || state->container == NULL) {
        return;
    }

    lv_offline_map_scroll_to_tile_pixel_at_anchor(state, pixel_x, pixel_y, state->config.view_width / 2,
                                                  state->config.view_height / 2, anim);
}

/*********************************************************
 * @brief 设置地图缩放级别并保持当前屏幕中心地理位置
 * @param map 离线地图控件对象，不能为空
 * @param zoom 目标缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_set_zoom(lv_obj_t *map, int zoom)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    lv_coord_t scroll_x; /* 当前横向滚动值 */
    lv_coord_t scroll_y; /* 当前纵向滚动值 */
    lv_coord_t next_scroll_x; /* 目标缩放级别下横向滚动值 */
    lv_coord_t next_scroll_y; /* 目标缩放级别下纵向滚动值 */
    lv_coord_t anchor_x; /* 缩放时保持地理位置不变的视窗横向锚点 */
    lv_coord_t anchor_y; /* 缩放时保持地理位置不变的视窗纵向锚点 */
    int normalized_zoom; /* 限幅后的目标缩放级别 */
    int anchor_global_x; /* 当前缩放级别下锚点对应的全局横向像素坐标 */
    int anchor_global_y; /* 当前缩放级别下锚点对应的全局纵向像素坐标 */
    int next_global_x; /* 目标缩放级别下锚点对应的全局横向像素坐标 */
    int next_global_y; /* 目标缩放级别下锚点对应的全局纵向像素坐标 */
    int next_tile_x; /* 目标缩放级别下新中心瓦片 X 坐标 */
    int next_tile_y; /* 目标缩放级别下新中心瓦片 Y 坐标 */
    int next_pixel_x; /* 目标缩放级别下中心瓦片内横向像素坐标 */
    int next_pixel_y; /* 目标缩放级别下中心瓦片内纵向像素坐标 */
    int previous_zoom; /* 切换前缩放级别，用于判断动画方向 */
    bool keep_follow_anchor; /* true 表示缩放时以车辆当前位置作为屏幕锚点 */
    bool keep_follow_position; /* true 表示缩放时需要同步换算车辆跟随全局像素 */

    if(state == NULL || state->container == NULL) {
        return;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    if(normalized_zoom == state->zoom) {
        lv_offline_map_update_zoom_controls(state);
        return;
    }

    previous_zoom = state->zoom;
    scroll_x = lv_obj_get_scroll_x(state->container);
    scroll_y = lv_obj_get_scroll_y(state->container);
    keep_follow_anchor = (state->follow_timer != NULL && state->follow_auto_center);
    keep_follow_position = state->follow_position_valid;
    if(keep_follow_anchor) {
        lv_offline_map_get_follow_anchor(state, &anchor_x, &anchor_y);
        anchor_global_x = (int)(state->follow_precise_x + 0.5);
        anchor_global_y = (int)(state->follow_precise_y + 0.5);
    } else {
        anchor_x = state->config.view_width / 2;
        anchor_y = state->config.view_height / 2;
        anchor_global_x = (state->center_tile_x - LV_OFFLINE_MAP_CENTER_TILE_COL) * state->config.tile_size +
                          scroll_x + anchor_x;
        anchor_global_y = (state->center_tile_y - LV_OFFLINE_MAP_CENTER_TILE_ROW) * state->config.tile_size +
                          scroll_y + anchor_y;
    }

    if(keep_follow_position) {
        state->follow_precise_x = lv_offline_map_scale_global_pixel_precise(state->follow_precise_x,
                                                                            state->zoom, normalized_zoom);
        state->follow_precise_y = lv_offline_map_scale_global_pixel_precise(state->follow_precise_y,
                                                                            state->zoom, normalized_zoom);
        state->follow_global_x = (int)(state->follow_precise_x + 0.5);
        state->follow_global_y = (int)(state->follow_precise_y + 0.5);
    }
    if(keep_follow_anchor) {
        next_global_x = state->follow_global_x;
        next_global_y = state->follow_global_y;
    } else {
        next_global_x = lv_offline_map_scale_global_pixel(anchor_global_x, state->zoom, normalized_zoom);
        next_global_y = lv_offline_map_scale_global_pixel(anchor_global_y, state->zoom, normalized_zoom);
    }

    next_tile_x = next_global_x / state->config.tile_size;
    next_tile_y = next_global_y / state->config.tile_size;
    next_pixel_x = next_global_x % state->config.tile_size;
    next_pixel_y = next_global_y % state->config.tile_size;
    next_scroll_x = LV_OFFLINE_MAP_CENTER_TILE_COL * state->config.tile_size + next_pixel_x - anchor_x;
    next_scroll_y = LV_OFFLINE_MAP_CENTER_TILE_ROW * state->config.tile_size + next_pixel_y - anchor_y;

    state->tile_switching = true;
    lv_offline_map_load_tiles(state, next_tile_x, next_tile_y, normalized_zoom);
    lv_obj_scroll_to(state->container, next_scroll_x, next_scroll_y, LV_ANIM_OFF);
    state->tile_switching = false;
    if(keep_follow_position) {
        lv_offline_map_update_traveled_distance(state);
        lv_offline_map_refresh_track(state);
    }
    lv_offline_map_start_zoom_animation(state, previous_zoom, normalized_zoom);
    lv_offline_map_update_zoom_controls(state);
}

/*********************************************************
 * @brief 地图放大一级
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_zoom_in(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    lv_offline_map_set_zoom(map, state->zoom + 1);
}

/*********************************************************
 * @brief 地图缩小一级
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_zoom_out(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    lv_offline_map_set_zoom(map, state->zoom - 1);
}

/*********************************************************
 * @brief 获取当前地图缩放级别
 * @param map 离线地图控件对象，不能为空
 * @return int 当前缩放级别，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
int lv_offline_map_get_zoom(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return 0;
    }

    return state->zoom;
}

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
void lv_offline_map_set_zoom_anim(lv_obj_t *map, bool enable, uint16_t anim_time_ms)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    state->config.zoom_anim_enable = enable;
    state->config.zoom_anim_time_ms = anim_time_ms == 0U ? LV_OFFLINE_MAP_DEFAULT_ZOOM_ANIM_TIME_MS : anim_time_ms;
    if(!enable && state->content_layer != NULL) {
        LV_OFFLINE_MAP_ANIM_DELETE(state->content_layer, lv_offline_map_zoom_anim_exec_cb);
        LV_OFFLINE_MAP_OBJ_SET_TRANSFORM_SCALE(state->content_layer, LV_OFFLINE_MAP_SCALE_NONE);
    }
}

/*********************************************************
 * @brief 停止并删除自动跟随定时器
 * @param state 控件私有状态，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_map_stop_track_follow(lv_offline_map_state_t *state)
{
    if(state == NULL) {
        return;
    }

    state->follow_last_tick = 0U;
    state->follow_last_refresh_tick = 0U;
    if(state->follow_timer == NULL) {
        return;
    }

    LV_OFFLINE_MAP_TIMER_DELETE(state->follow_timer);
    state->follow_timer = NULL;
}

/*********************************************************
 * @brief 清空当前轨迹点
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_clear(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    lv_offline_map_stop_track_follow(state);
    lv_offline_map_stop_auto_center_timer(state);
    state->track_point_count = 0;
    state->track_travel_index = 0;
    state->follow_segment_index = 0;
    state->follow_global_x = 0;
    state->follow_global_y = 0;
    state->follow_precise_x = 0.0;
    state->follow_precise_y = 0.0;
    state->follow_position_valid = false;
    state->follow_auto_center = false;
    state->follow_last_tick = 0U;
    state->track_total_distance_m = 0U;
    state->track_traveled_distance_m = 0U;
    if(!state->track_batch_updating) {
        lv_offline_map_refresh_track(state);
    }
}

/*********************************************************
 * @brief 开始批量写入轨迹点并暂缓刷新
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_begin_batch(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    state->track_batch_updating = true;
}

/*********************************************************
 * @brief 结束批量写入轨迹点并统一刷新
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_end_batch(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return;
    }

    state->track_batch_updating = false;
    if(state->track_travel_index >= state->track_point_count) {
        state->track_travel_index = 0;
    }
    if(state->track_point_count == 0U) {
        state->track_total_distance_m = 0U;
        state->track_traveled_distance_m = 0U;
    } else {
        state->track_total_distance_m = state->track_distance_m[state->track_point_count - 1U];
        state->track_traveled_distance_m = state->track_distance_m[state->track_travel_index];
    }
    lv_offline_map_refresh_track(state);
}

/*********************************************************
 * @brief 追加一个带缩放级别的瓦片轨迹点
 * @param state 控件私有状态，不能为空
 * @param tile_x 轨迹点所在瓦片 X 坐标
 * @param tile_y 轨迹点所在瓦片 Y 坐标
 * @param pixel_x 轨迹点在瓦片内横向像素坐标
 * @param pixel_y 轨迹点在瓦片内纵向像素坐标
 * @param zoom 轨迹点对应的缩放级别
 * @param has_distance_m true 表示使用调用方提供的累计里程
 * @param distance_m 调用方提供的轨迹累计里程，单位为米
 * @return bool true 表示已写入，false 表示参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_track_append_tile_point(lv_offline_map_state_t *state, int tile_x, int tile_y,
                                                   int pixel_x, int pixel_y, int zoom, bool has_distance_m,
                                                   uint32_t distance_m)
{
    lv_offline_map_track_point_t record; /* 待写入的原始轨迹记录 */
    uint32_t segment_distance_m; /* 与前一个轨迹点之间的近似距离，单位为米 */
    uint32_t dropped_distance_m = 0U; /* 缓存满时被丢弃首点对应的累计里程，单位为米 */
    uint16_t distance_index; /* 重排累计里程数组时使用的轨迹点索引 */

    if(state == NULL || state->container == NULL || state->track_records == NULL || state->track_distance_m == NULL) {
        return false;
    }

    record.tile_x = tile_x;
    record.tile_y = tile_y;
    record.pixel_x = pixel_x;
    record.pixel_y = pixel_y;
    record.zoom = lv_offline_map_normalize_zoom(state, zoom);
    record.has_distance_m = has_distance_m;
    record.distance_m = distance_m;

    if(state->track_point_count >= state->track_capacity) {
        if(state->track_capacity <= 1U) {
            state->track_point_count = 0U;
            state->track_travel_index = 0U;
            state->track_total_distance_m = 0U;
            state->track_traveled_distance_m = 0U;
        } else {
            dropped_distance_m = state->track_distance_m[1U];
            memmove(&state->track_records[0], &state->track_records[1],
                    (state->track_capacity - 1U) * sizeof(state->track_records[0]));
            memmove(&state->track_distance_m[0], &state->track_distance_m[1],
                    (state->track_capacity - 1U) * sizeof(state->track_distance_m[0]));
            state->track_point_count = state->track_capacity - 1U;
            for(distance_index = 0U; distance_index < state->track_point_count; distance_index++) {
                state->track_distance_m[distance_index] -= dropped_distance_m;
                if(state->track_records[distance_index].has_distance_m) {
                    if(state->track_records[distance_index].distance_m > dropped_distance_m) {
                        state->track_records[distance_index].distance_m -= dropped_distance_m;
                    } else {
                        state->track_records[distance_index].distance_m = 0U;
                    }
                }
            }
            if(state->track_travel_index > 0U) {
                state->track_travel_index--;
            }
        }
    }

    if(record.has_distance_m) {
        if(record.distance_m > dropped_distance_m) {
            record.distance_m -= dropped_distance_m;
        } else {
            record.distance_m = 0U;
        }
    }

    if(state->track_point_count == 0U) {
        state->track_distance_m[0] = 0U;
        record.distance_m = 0U;
    } else {
        if(record.has_distance_m && record.distance_m >= state->track_distance_m[state->track_point_count - 1U]) {
            state->track_distance_m[state->track_point_count] = record.distance_m;
        } else {
            /* 调用方未提供有效累计里程时，回退到地图像素距离估算，保持旧接口行为。 */
            segment_distance_m = lv_offline_map_get_track_segment_distance_m(state,
                                                                             &state->track_records[state->track_point_count - 1U],
                                                                             &record);
            state->track_distance_m[state->track_point_count] =
                state->track_distance_m[state->track_point_count - 1U] + segment_distance_m;
            record.distance_m = state->track_distance_m[state->track_point_count];
            record.has_distance_m = false;
        }
    }
    state->track_records[state->track_point_count] = record;
    state->track_point_count++;
    state->track_total_distance_m = state->track_distance_m[state->track_point_count - 1U];
    if(state->track_travel_index < state->track_point_count) {
        state->track_traveled_distance_m = state->track_distance_m[state->track_travel_index];
    }
    if(!state->track_batch_updating) {
        lv_offline_map_refresh_track(state);
    }

    return true;
}

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
bool lv_offline_map_track_add_point(lv_obj_t *map, int pixel_x, int pixel_y)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    bool added; /* 轨迹点写入结果，true 表示已添加到当前中心瓦片轨迹 */

    if(state == NULL) {
        return false;
    }

    added = lv_offline_map_track_add_tile_point(map, state->center_tile_x, state->center_tile_y, pixel_x, pixel_y);

    return added;
}

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
bool lv_offline_map_track_add_tile_point(lv_obj_t *map, int tile_x, int tile_y, int pixel_x, int pixel_y)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    bool added; /* 轨迹点写入结果，true 表示已添加到指定瓦片轨迹 */

    if(state == NULL) {
        return false;
    }

    added = lv_offline_map_track_append_tile_point(state, tile_x, tile_y, pixel_x, pixel_y, state->zoom,
                                                  false, 0U);

    return added;
}

/*********************************************************
 * @brief 按 E7 经纬度添加轨迹点到控件内部轨迹缓存
 * @param state 控件私有状态，不能为空
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @param has_distance_m true 表示使用调用方提供的累计里程
 * @param distance_m 调用方提供的轨迹累计里程，单位为米
 * @return bool true 表示轨迹点已写入，false 表示参数无效或坐标换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_map_track_append_lonlat_e7(lv_offline_map_state_t *state, int lon_e7, int lat_e7,
                                                  int zoom, bool has_distance_m, uint32_t distance_m)
{
    int tile_x; /* 经纬度换算得到的瓦片 X 坐标 */
    int tile_y; /* 经纬度换算得到的瓦片 Y 坐标 */
    int pixel_x; /* 经纬度换算得到的瓦片内横向像素坐标 */
    int pixel_y; /* 经纬度换算得到的瓦片内纵向像素坐标 */
    int normalized_zoom; /* 限幅后的缩放级别 */

    if(state == NULL) {
        return false;
    }

    normalized_zoom = lv_offline_map_normalize_zoom(state, zoom);
    if(!lv_offline_map_lonlat_to_tile_point(state, lon_e7, lat_e7, normalized_zoom,
                                            &tile_x, &tile_y, &pixel_x, &pixel_y)) {
        return false;
    }

    {
        bool added = lv_offline_map_track_append_tile_point(state, tile_x, tile_y, pixel_x, pixel_y,
                                                           normalized_zoom, has_distance_m,
                                                           distance_m); /* 经纬度轨迹点写入结果，true 表示已添加到轨迹缓存 */

        return added;
    }
}

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
bool lv_offline_map_track_add_lonlat_e7(lv_obj_t *map, int lon_e7, int lat_e7, int zoom)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    bool added; /* 经纬度轨迹点写入结果，true 表示已添加到轨迹缓存 */

    added = lv_offline_map_track_append_lonlat_e7(state, lon_e7, lat_e7, zoom, false, 0U);

    return added;
}

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
                                                      uint32_t distance_m)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    bool added; /* 带累计里程的经纬度轨迹点写入结果，true 表示已添加到轨迹缓存 */

    added = lv_offline_map_track_append_lonlat_e7(state, lon_e7, lat_e7, zoom, true, distance_m);

    return added;
}

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
                                             lv_offline_map_location_result_t *result)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int tile_x; /* 实时定位经纬度换算得到的瓦片 X 坐标 */
    int tile_y; /* 实时定位经纬度换算得到的瓦片 Y 坐标 */
    int pixel_x; /* 实时定位经纬度换算得到的瓦片内横向像素坐标 */
    int pixel_y; /* 实时定位经纬度换算得到的瓦片内纵向像素坐标 */
    int location_global_x; /* 实时定位点全局横向像素坐标 */
    int location_global_y; /* 实时定位点全局纵向像素坐标 */
    int next_global_x; /* 投影路线段终点全局横向像素坐标，用于更新车标方向 */
    int next_global_y; /* 投影路线段终点全局纵向像素坐标，用于更新车标方向 */
    int prev_global_x; /* 投影路线段起点全局横向像素坐标，用于末尾方向回退 */
    int prev_global_y; /* 投影路线段起点全局纵向像素坐标，用于末尾方向回退 */
    uint16_t segment_index; /* 定位投影点所在路线段起点索引 */
    uint32_t base_distance_m; /* 投影路线段起点累计里程，单位为米 */
    uint32_t segment_distance_m; /* 投影路线段总里程，单位为米 */
    uint32_t partial_distance_m; /* 投影路线段内已行驶里程，单位为米 */
    double projected_global_x; /* 定位点投影到路线后的全局横向像素坐标 */
    double projected_global_y; /* 定位点投影到路线后的全局纵向像素坐标 */
    double segment_ratio; /* 定位投影点在线段内的比例，范围为 0 到 1 */
    double deviation_pixel; /* 实时定位点到路线投影点的像素距离 */
    double meters_per_pixel; /* 当前投影点附近每像素代表的米数 */
    bool updated; /* true 表示实时定位已成功更新到地图状态 */
    bool should_auto_center; /* true 表示本次定位更新后仍允许地图视野跟随车辆 */

    if(state == NULL || state->track_point_count < 2U || state->track_distance_m == NULL) {
        return false;
    }

    if(!lv_offline_map_lonlat_to_tile_point(state, lon_e7, lat_e7, zoom, &tile_x, &tile_y, &pixel_x, &pixel_y)) {
        return false;
    }

    location_global_x = lv_offline_map_scale_global_pixel(tile_x * state->config.tile_size + pixel_x,
                                                         lv_offline_map_normalize_zoom(state, zoom), state->zoom);
    location_global_y = lv_offline_map_scale_global_pixel(tile_y * state->config.tile_size + pixel_y,
                                                         lv_offline_map_normalize_zoom(state, zoom), state->zoom);
    updated = lv_offline_map_project_location_to_track(state, location_global_x, location_global_y,
                                                       &projected_global_x, &projected_global_y, &segment_index,
                                                       &segment_ratio, &deviation_pixel);
    if(!updated || segment_index + 1U >= state->track_point_count) {
        return false;
    }

    should_auto_center = !state->follow_position_valid || state->follow_auto_center;
    lv_offline_map_stop_track_follow(state);
    if(should_auto_center) {
        lv_offline_map_stop_auto_center_timer(state);
    }
    state->follow_segment_index = segment_index;
    state->track_travel_index = segment_index;
    state->follow_precise_x = projected_global_x;
    state->follow_precise_y = projected_global_y;
    state->follow_global_x = (int)(projected_global_x + 0.5);
    state->follow_global_y = (int)(projected_global_y + 0.5);
    state->follow_position_valid = true;
    state->follow_auto_center = should_auto_center;
    base_distance_m = state->track_distance_m[segment_index];
    segment_distance_m = state->track_distance_m[segment_index + 1U] - base_distance_m;
    partial_distance_m = (uint32_t)((double)segment_distance_m * segment_ratio + 0.5);
    state->track_traveled_distance_m = base_distance_m + partial_distance_m;
    if(state->track_traveled_distance_m > state->track_total_distance_m) {
        state->track_traveled_distance_m = state->track_total_distance_m;
    }

    if(lv_offline_map_get_track_global_pixel(state, &state->track_records[segment_index + 1U], state->zoom,
                                             &next_global_x, &next_global_y)) {
        lv_offline_map_update_arrow_direction(state, next_global_x - state->follow_global_x,
                                              next_global_y - state->follow_global_y);
    } else if(lv_offline_map_get_track_global_pixel(state, &state->track_records[segment_index], state->zoom,
                                                    &prev_global_x, &prev_global_y)) {
        lv_offline_map_update_arrow_direction(state, state->follow_global_x - prev_global_x,
                                              state->follow_global_y - prev_global_y);
    }

    if(state->follow_auto_center) {
        lv_offline_map_center_follow_global_pixel(state, state->follow_global_x, state->follow_global_y,
                                                  state->zoom, LV_ANIM_OFF);
    }
    lv_offline_map_refresh_track(state);

    if(result != NULL) {
        meters_per_pixel = lv_offline_map_get_meters_per_pixel(state, projected_global_y, state->zoom);
        result->traveled_distance_m = state->track_traveled_distance_m;
        result->remaining_distance_m = state->track_total_distance_m > state->track_traveled_distance_m ?
                                           state->track_total_distance_m - state->track_traveled_distance_m :
                                           0U;
        result->deviation_distance_m = (uint32_t)(deviation_pixel * meters_per_pixel + 0.5);
        result->segment_index = segment_index;
    }

    return true;
}

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
bool lv_offline_map_track_get_follow_location_e7(lv_obj_t *map, int *lon_e7, int *lat_e7)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int current_lon_e7; /* 当前跟随位置反算得到的经度 E7 定点值 */
    int current_lat_e7; /* 当前跟随位置反算得到的纬度 E7 定点值 */

    if(state == NULL || !state->follow_position_valid ||
       !lv_offline_map_global_pixel_to_lonlat_e7(state, state->follow_global_x, state->follow_global_y,
                                                 &current_lon_e7, &current_lat_e7)) {
        return false;
    }

    if(lon_e7 != NULL) {
        *lon_e7 = current_lon_e7;
    }
    if(lat_e7 != NULL) {
        *lat_e7 = current_lat_e7;
    }

    return true;
}

/*********************************************************
 * @brief 启动轨迹自动跟随播放
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_start_follow(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */
    int next_global_x; /* 自动跟随首段终点全局横向像素坐标 */
    int next_global_y; /* 自动跟随首段终点全局纵向像素坐标 */

    if(state == NULL || state->track_point_count == 0U) {
        return;
    }

    lv_offline_map_stop_track_follow(state);
    state->track_travel_index = 0;
    state->follow_segment_index = 0;
    if(!lv_offline_map_get_track_global_pixel(state, &state->track_records[0], state->zoom,
                                              &state->follow_global_x, &state->follow_global_y)) {
        return;
    }
    state->follow_precise_x = (double)state->follow_global_x;
    state->follow_precise_y = (double)state->follow_global_y;
    state->follow_position_valid = true;
    state->follow_auto_center = true;
    lv_offline_map_stop_auto_center_timer(state);
    state->follow_last_tick = lv_tick_get();

    lv_offline_map_center_follow_global_pixel(state, state->follow_global_x, state->follow_global_y,
                                              state->zoom, LV_ANIM_OFF);
    if(state->track_point_count > 1U &&
       lv_offline_map_get_track_global_pixel(state, &state->track_records[1], state->zoom,
                                             &next_global_x, &next_global_y)) {
        lv_offline_map_update_arrow_direction(state, next_global_x - state->follow_global_x,
                                              next_global_y - state->follow_global_y);
    }
    lv_offline_map_refresh_track(state);

    if(state->track_point_count > 1U) {
        state->follow_timer = lv_timer_create(lv_offline_map_track_follow_timer_cb,
                                              LV_OFFLINE_MAP_FOLLOW_PERIOD_MS, state);
    }
}

/*********************************************************
 * @brief 停止轨迹自动跟随播放
 * @param map 离线地图控件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_map_track_stop_follow(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    lv_offline_map_stop_track_follow(state);
}

/*********************************************************
 * @brief 获取当前轨迹点数量
 * @param map 离线地图控件对象，不能为空
 * @return uint16_t 当前轨迹点数量，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint16_t lv_offline_map_track_get_point_count(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return 0;
    }

    return state->track_point_count;
}

/*********************************************************
 * @brief 获取当前已行驶轨迹点索引
 * @param map 离线地图控件对象，不能为空
 * @return uint16_t 当前已行驶轨迹点索引，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint16_t lv_offline_map_track_get_travel_index(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return 0;
    }

    return state->track_travel_index;
}

/*********************************************************
 * @brief 获取当前轨迹总里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹总里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_total_distance_m(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return 0U;
    }

    return state->track_total_distance_m;
}

/*********************************************************
 * @brief 获取当前轨迹已行驶里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹已行驶里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_traveled_distance_m(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL) {
        return 0U;
    }

    return state->track_traveled_distance_m;
}

/*********************************************************
 * @brief 获取当前轨迹剩余里程
 * @param map 离线地图控件对象，不能为空
 * @return uint32_t 当前轨迹剩余里程，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
uint32_t lv_offline_map_track_get_remaining_distance_m(lv_obj_t *map)
{
    lv_offline_map_state_t *state = lv_offline_map_get_state(map); /* 控件私有状态 */

    if(state == NULL || state->track_traveled_distance_m >= state->track_total_distance_m) {
        return 0U;
    }

    return state->track_total_distance_m - state->track_traveled_distance_m;
}
