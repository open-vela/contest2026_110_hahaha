/*********************************************************
 * @file ui_map.c
 * @author ^^^^^^^ ()
 * @brief 管理独立离线地图演示页面并接入可移植地图控件
 * @version 1.0
 * @date 2026-06-18
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include "ui.h"
#include "lv_offline_map.h"

#define MAP_DEMO_VIEW_WIDTH 480 /* 独立地图演示页面中的地图控件宽度，单位为像素 */
#define MAP_DEMO_VIEW_HEIGHT 240 /* 独立地图演示页面中的地图控件高度，底部预留状态栏 */
#define MAP_DEMO_DIR "L:/sdcard/map/" /* 独立地图演示离线瓦片根目录，按 zoom/x/y/tile.jpg 文件夹结构读取 */
#define MAP_DEMO_TILE_FILE_NAME "tile.jpg" /* 独立地图演示离线瓦片文件名 */
#define MAP_DEMO_MIN_ZOOM 12 /* 独立地图演示当前离线瓦片包支持的最小缩放级别 */
#define MAP_DEMO_MAX_ZOOM 17 /* 独立地图演示当前离线瓦片支持的最大缩放级别 */
#define MAP_DEMO_DEFAULT_ZOOM 17 /* 独立地图演示页面默认缩放级别 */
#define MAP_DEMO_DEFAULT_TILE_X 107073 /* 独立地图演示页面默认中心瓦片 X 坐标 */
#define MAP_DEMO_DEFAULT_TILE_Y 53161 /* 独立地图演示页面默认中心瓦片 Y 坐标 */
#define MAP_DEMO_DEFAULT_VEHICLE_LON_E7 1140918595 /* 独立地图演示默认车辆经度 E7 定点值，来自默认导航起点 */
#define MAP_DEMO_DEFAULT_VEHICLE_LAT_E7 321578674 /* 独立地图演示默认车辆纬度 E7 定点值，来自默认导航起点 */
#define MAP_DEMO_TRACK_MAX_POINTS 1024 /* 独立地图演示导航路线默认缓存点数量上限 */
#define MAP_DEMO_FOLLOW_SPEED_KMH 50.0 /* 独立地图演示自动跟随模拟速度，单位为千米每小时 */
#define MAP_DEMO_NORTH_INDICATOR_SIZE 30 /* 正北指示器边长，单位为像素 */
#define MAP_DEMO_NORTH_INDICATOR_X_OFFSET (-12) /* 正北指示器相对屏幕右侧的横向偏移 */
#define MAP_DEMO_NORTH_INDICATOR_Y_OFFSET 4 /* 正北指示器相对屏幕顶部的纵向偏移 */
#define MAP_DEMO_NAV_PANEL_WIDTH 210 /* 顶部导航提示条宽度，单位为像素 */
#define MAP_DEMO_NAV_PANEL_HEIGHT 42 /* 顶部导航提示条高度，单位为像素 */
#define MAP_DEMO_NAV_ICON_SIZE 25 /* 顶部导航转向图标边长，单位为像素 */
#define MAP_DEMO_ROUTE_BAR_WIDTH 8 /* 左侧剩余里程条宽度，单位为像素 */
#define MAP_DEMO_ROUTE_BAR_HEIGHT 158 /* 左侧剩余里程条高度，单位为像素 */
#define MAP_DEMO_ROUTE_BAR_X_OFFSET 10 /* 左侧剩余里程条相对屏幕左侧的横向偏移 */
#define MAP_DEMO_ROUTE_BAR_Y_OFFSET 14 /* 左侧剩余里程条相对屏幕垂直中心的纵向偏移 */
#define MAP_DEMO_ROUTE_TEXT_WIDTH 96 /* 左侧剩余里程文本宽度，单位为像素 */
#define MAP_DEMO_ROUTE_TEXT_X_OFFSET 8 /* 左侧剩余里程文本相对屏幕左侧的横向偏移 */
#define MAP_DEMO_ROUTE_TEXT_Y_OFFSET (-8) /* 左侧剩余里程文本相对屏幕底部的纵向偏移 */
#define MAP_DEMO_ROUTE_BAR_MIN_HEIGHT 1 /* 剩余里程条最小填充高度，避免 LVGL 对 0 高对象显示异常 */
#define MAP_DEMO_ZOOM_ANIM_TIME_MS 180 /* 独立地图演示地图缩放切换动画时长，单位为毫秒 */
#define MAP_DEMO_PICK_MARKER_SIZE 14 /* 长按选点标记边长，单位为像素 */
#define MAP_DEMO_PICK_CONFIRM_WIDTH 72 /* 选点确认按钮宽度，单位为像素 */
#define MAP_DEMO_PICK_CONFIRM_HEIGHT 30 /* 选点确认按钮高度，单位为像素 */
#define MAP_DEMO_PICK_CONFIRM_Y_OFFSET (-5) /* 选点确认按钮相对屏幕底部的纵向偏移 */

static lv_obj_t *ui_MapScreen; /* 独立地图演示页面对象，生命周期由 ui 初始化流程管理 */
static lv_obj_t *ui_MapWidget; /* 可移植离线地图控件实例，承载瓦片、轨迹、缩放和跟随播放 */
static lv_obj_t *ui_MapTitleLabel; /* 独立地图演示页标题标签 */
static lv_obj_t *ui_MapHintLabel; /* 独立地图演示页底部状态提示标签 */
static lv_obj_t *ui_MapNavPanel; /* 地图页顶部导航提示条容器 */
static lv_obj_t *ui_MapNavIconImage; /* 地图页顶部转向方向图片图标 */
static lv_obj_t *ui_MapNavTextLabel; /* 地图页顶部转向距离提示文本 */
static lv_obj_t *ui_MapRouteBarBg; /* 地图页左侧剩余里程条背景 */
static lv_obj_t *ui_MapRouteBarFill; /* 地图页左侧剩余里程条已行驶填充 */
static lv_obj_t *ui_MapRouteTextLabel; /* 地图页左侧剩余里程文本 */
static lv_obj_t *ui_MapNorthIndicator; /* 地图页正北指示器，固定展示地图上方为北 */
static lv_obj_t *ui_MapNorthLabel; /* 正北指示器内部 N 标记 */
static lv_obj_t *ui_MapPickMarker; /* 长按选点标记对象，按选中终点经纬度刷新屏幕位置 */
static lv_obj_t *ui_MapPickConfirmButton; /* 长按选点确认按钮，点击后才开始路线规划 */
static lv_obj_t *ui_MapPickConfirmLabel; /* 长按选点确认按钮文本标签 */
static bool ui_MapNavigationActive; /* true 表示当前已进入导航态，需要显示路线提示和剩余里程 */
static bool ui_MapPickedEndValid; /* true 表示当前存在等待确认的长按终点 */
static int ui_MapPickedEndLonE7; /* 等待确认的终点 WGS84 经度 E7 定点值，单位为 1e-7 度 */
static int ui_MapPickedEndLatE7; /* 等待确认的终点 WGS84 纬度 E7 定点值，单位为 1e-7 度 */

#if LVGL_VERSION_MAJOR >= 9
#define UI_MAP_ACTIVE_INDEV() lv_indev_active() /* 获取当前触发地图事件的输入设备，LVGL 9 使用 active API */
#define UI_MAP_BUTTON_CREATE(parent) lv_button_create(parent) /* 创建按钮对象，LVGL 9 使用 button 控件 */
#else
#define UI_MAP_ACTIVE_INDEV() lv_indev_get_act() /* 获取当前触发地图事件的输入设备，LVGL 8 使用 get_act API */
#define UI_MAP_BUTTON_CREATE(parent) lv_btn_create(parent) /* 创建按钮对象，LVGL 8 使用 btn 控件 */
#endif

/*********************************************************
 * @brief 更新地图底部轨迹状态提示
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapUpdateHint(void);

/*********************************************************
 * @brief 创建地图页面的文字提示
 * @param parent 父对象指针，不能为空
 * @param text 标签文本，不能为空
 * @param y_offset 相对顶部或底部的 Y 偏移
 * @param align 标签对齐方式
 * @return lv_obj_t* 创建成功的标签对象
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_obj_t *ui_mapCreateLabel(lv_obj_t *parent, const char *text, lv_coord_t y_offset, lv_align_t align)
{
    lv_obj_t *label = lv_label_create(parent); /* 页面提示标签对象，由父对象管理生命周期 */

    lv_obj_set_width(label, MAP_DEMO_VIEW_WIDTH);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_y(label, y_offset);
    lv_obj_set_align(label, align);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return label;
}

/*********************************************************
 * @brief 格式化导航距离文本
 * @param buffer 输出文本缓冲区，不能为空
 * @param buffer_size 输出文本缓冲区长度
 * @param distance_m 距离值，单位为米
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapFormatDistance(char *buffer, size_t buffer_size, uint32_t distance_m)
{
    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    if(distance_m >= 1000U) {
        snprintf(buffer, buffer_size, "%lu.%lu km", (unsigned long)(distance_m / 1000U),
                 (unsigned long)((distance_m % 1000U) / 100U));
    } else {
        snprintf(buffer, buffer_size, "%lu m", (unsigned long)distance_m);
    }
}

/*********************************************************
 * @brief 获取转向类型对应的导航图标资源
 * @param turn_type 转向类型
 * @return const void* LVGL 图片资源指针，静态资源无需释放
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static const void *ui_mapGetTurnIconSource(lv_offline_nav_turn_type_t turn_type)
{
    if(turn_type == LV_OFFLINE_NAV_TURN_LEFT) {
        return &ui_img_nav_left_png;
    }
    if(turn_type == LV_OFFLINE_NAV_TURN_RIGHT) {
        return &ui_img_nav_right_png;
    }
    if(turn_type == LV_OFFLINE_NAV_TURN_UTURN) {
        return &ui_img_nav_uturn_png;
    }

    return &ui_img_nav_straight_png;
}

/*********************************************************
 * @brief 设置导航叠层对象显示或隐藏
 * @param visible true 表示显示导航提示和剩余里程，false 表示隐藏
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapSetNavigationOverlayVisible(bool visible)
{
    if(ui_MapNavPanel != NULL) {
        if(visible) {
            lv_obj_remove_flag(ui_MapNavPanel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_MapNavPanel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(ui_MapRouteBarBg != NULL) {
        if(visible) {
            lv_obj_remove_flag(ui_MapRouteBarBg, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_MapRouteBarBg, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(ui_MapRouteTextLabel != NULL) {
        if(visible) {
            lv_obj_remove_flag(ui_MapRouteTextLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_MapRouteTextLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*********************************************************
 * @brief 设置地图选点控件显示或隐藏
 * @param visible true 表示显示选点标记和确认按钮，false 表示隐藏
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapSetPickOverlayVisible(bool visible)
{
    if(ui_MapPickMarker != NULL) {
        if(visible) {
            lv_obj_remove_flag(ui_MapPickMarker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_MapPickMarker, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(ui_MapPickConfirmButton != NULL) {
        if(visible) {
            lv_obj_remove_flag(ui_MapPickConfirmButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_MapPickConfirmButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*********************************************************
 * @brief 按当前地图视野刷新选点标记位置
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapUpdatePickOverlay(void)
{
    lv_area_t map_area; /* 地图控件在屏幕坐标系中的区域 */
    lv_area_t screen_area; /* 地图页面在屏幕坐标系中的区域 */
    lv_coord_t view_x; /* 选中终点在当前地图视窗内的横向坐标 */
    lv_coord_t view_y; /* 选中终点在当前地图视窗内的纵向坐标 */
    lv_coord_t marker_x; /* 选点标记在地图页面内的横向位置 */
    lv_coord_t marker_y; /* 选点标记在地图页面内的纵向位置 */
    bool point_ready; /* true 表示选中终点已成功换算到当前视图坐标 */

    if(!ui_MapPickedEndValid) {
        ui_mapSetPickOverlayVisible(false);
        return;
    }

    point_ready = lv_offline_map_lonlat_e7_to_view_point(ui_MapWidget, ui_MapPickedEndLonE7,
                                                         ui_MapPickedEndLatE7, &view_x, &view_y);
    if(ui_MapPickConfirmButton != NULL) {
        lv_obj_remove_flag(ui_MapPickConfirmButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_MapPickConfirmButton);
    }
    if(!point_ready || view_x < 0 || view_y < 0 ||
       view_x >= MAP_DEMO_VIEW_WIDTH || view_y >= MAP_DEMO_VIEW_HEIGHT) {
        if(ui_MapPickMarker != NULL) {
            lv_obj_add_flag(ui_MapPickMarker, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if(ui_MapPickMarker != NULL) {
        lv_obj_remove_flag(ui_MapPickMarker, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_get_coords(ui_MapWidget, &map_area);
    lv_obj_get_coords(ui_MapScreen, &screen_area);
    marker_x = map_area.x1 - screen_area.x1 + view_x - MAP_DEMO_PICK_MARKER_SIZE / 2;
    marker_y = map_area.y1 - screen_area.y1 + view_y - MAP_DEMO_PICK_MARKER_SIZE / 2;
    lv_obj_set_pos(ui_MapPickMarker, marker_x, marker_y);
    lv_obj_move_foreground(ui_MapPickMarker);
}

/*********************************************************
 * @brief 创建顶部导航提示条和底部剩余里程条
 * @param parent 父对象指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapCreateGuidanceOverlay(lv_obj_t *parent)
{
    if(parent == NULL) {
        return;
    }

    ui_MapNavPanel = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_MapNavPanel);
    lv_obj_set_size(ui_MapNavPanel, MAP_DEMO_NAV_PANEL_WIDTH, MAP_DEMO_NAV_PANEL_HEIGHT);
    lv_obj_set_align(ui_MapNavPanel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_MapNavPanel, 8, 8);
    lv_obj_set_style_radius(ui_MapNavPanel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapNavPanel, lv_color_hex(0x111820), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapNavPanel, 225, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MapNavPanel, lv_color_hex(0x2FE47C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MapNavPanel, 210, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MapNavPanel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapNavPanel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapNavIconImage = lv_image_create(ui_MapNavPanel);
    lv_obj_set_size(ui_MapNavIconImage, MAP_DEMO_NAV_ICON_SIZE, MAP_DEMO_NAV_ICON_SIZE);
    lv_obj_set_align(ui_MapNavIconImage, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(ui_MapNavIconImage, 14);
    lv_image_set_src(ui_MapNavIconImage, ui_mapGetTurnIconSource(LV_OFFLINE_NAV_TURN_NONE));
    lv_obj_remove_flag(ui_MapNavIconImage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapNavTextLabel = lv_label_create(ui_MapNavPanel);
    lv_obj_set_size(ui_MapNavTextLabel, MAP_DEMO_NAV_PANEL_WIDTH - 58, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_MapNavTextLabel, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(ui_MapNavTextLabel, 52);
    lv_obj_set_style_text_color(ui_MapNavTextLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_MapNavTextLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MapNavTextLabel, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_MapNavTextLabel, "Route ready");
    lv_obj_remove_flag(ui_MapNavTextLabel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapRouteBarBg = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_MapRouteBarBg);
    lv_obj_set_size(ui_MapRouteBarBg, MAP_DEMO_ROUTE_BAR_WIDTH, MAP_DEMO_ROUTE_BAR_HEIGHT);
    lv_obj_set_align(ui_MapRouteBarBg, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(ui_MapRouteBarBg, MAP_DEMO_ROUTE_BAR_X_OFFSET);
    lv_obj_set_y(ui_MapRouteBarBg, MAP_DEMO_ROUTE_BAR_Y_OFFSET);
    lv_obj_set_style_radius(ui_MapRouteBarBg, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapRouteBarBg, lv_color_hex(0x26313A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapRouteBarBg, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapRouteBarBg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapRouteBarFill = lv_obj_create(ui_MapRouteBarBg);
    lv_obj_remove_style_all(ui_MapRouteBarFill);
    lv_obj_set_size(ui_MapRouteBarFill, MAP_DEMO_ROUTE_BAR_WIDTH, MAP_DEMO_ROUTE_BAR_MIN_HEIGHT);
    lv_obj_set_align(ui_MapRouteBarFill, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_radius(ui_MapRouteBarFill, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapRouteBarFill, lv_color_hex(0x2FE47C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapRouteBarFill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapRouteBarFill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapRouteTextLabel = ui_mapCreateLabel(parent, "0 m", MAP_DEMO_ROUTE_TEXT_Y_OFFSET, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_width(ui_MapRouteTextLabel, MAP_DEMO_ROUTE_TEXT_WIDTH);
    lv_obj_set_x(ui_MapRouteTextLabel, MAP_DEMO_ROUTE_TEXT_X_OFFSET);
    lv_obj_set_style_text_align(ui_MapRouteTextLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_mapSetNavigationOverlayVisible(false);
}

/*********************************************************
 * @brief 创建地图正北方向指示器
 * @param parent 父对象指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapCreateNorthIndicator(lv_obj_t *parent)
{
    if(parent == NULL) {
        return;
    }

    ui_MapNorthIndicator = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_MapNorthIndicator);
    lv_obj_set_size(ui_MapNorthIndicator, MAP_DEMO_NORTH_INDICATOR_SIZE, MAP_DEMO_NORTH_INDICATOR_SIZE);
    lv_obj_set_align(ui_MapNorthIndicator, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(ui_MapNorthIndicator, MAP_DEMO_NORTH_INDICATOR_X_OFFSET);
    lv_obj_set_y(ui_MapNorthIndicator, MAP_DEMO_NORTH_INDICATOR_Y_OFFSET);
    lv_obj_set_style_radius(ui_MapNorthIndicator, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapNorthIndicator, lv_color_hex(0x111820), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapNorthIndicator, 225, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MapNorthIndicator, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MapNorthIndicator, 210, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MapNorthIndicator, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapNorthIndicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapNorthLabel = lv_label_create(ui_MapNorthIndicator);
    lv_label_set_text(ui_MapNorthLabel, "N");
    lv_obj_set_align(ui_MapNorthLabel, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_MapNorthLabel, lv_color_hex(0xFF3D00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MapNorthLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MapNorthLabel, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapNorthLabel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/*********************************************************
 * @brief 确认当前选点并开始从车辆位置规划导航
 * @param e LVGL 点击事件对象，当前不读取事件内容
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapConfirmPickedRoute(lv_event_t *e)
{
    int end_lon_e7; /* 用户确认的终点 WGS84 经度 E7 定点值 */
    int end_lat_e7; /* 用户确认的终点 WGS84 纬度 E7 定点值 */
    bool route_ready; /* 从当前车辆位置到确认终点的规划结果 */

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

/*********************************************************
 * @brief 创建地图选点标记和确认按钮
 * @param parent 父对象指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapCreatePickOverlay(lv_obj_t *parent)
{
    if(parent == NULL) {
        return;
    }

    ui_MapPickMarker = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_MapPickMarker);
    lv_obj_set_size(ui_MapPickMarker, MAP_DEMO_PICK_MARKER_SIZE, MAP_DEMO_PICK_MARKER_SIZE);
    lv_obj_set_style_radius(ui_MapPickMarker, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapPickMarker, lv_color_hex(0xFF3D00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapPickMarker, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MapPickMarker, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MapPickMarker, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MapPickMarker, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapPickMarker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapPickConfirmButton = UI_MAP_BUTTON_CREATE(parent);
    lv_obj_remove_style_all(ui_MapPickConfirmButton);
    lv_obj_set_size(ui_MapPickConfirmButton, MAP_DEMO_PICK_CONFIRM_WIDTH, MAP_DEMO_PICK_CONFIRM_HEIGHT);
    lv_obj_set_align(ui_MapPickConfirmButton, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_MapPickConfirmButton, MAP_DEMO_PICK_CONFIRM_Y_OFFSET);
    lv_obj_set_style_radius(ui_MapPickConfirmButton, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MapPickConfirmButton, lv_color_hex(0x2FE47C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapPickConfirmButton, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MapPickConfirmButton, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MapPickConfirmButton, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MapPickConfirmButton, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_MapPickConfirmButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ui_MapPickConfirmButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui_MapPickConfirmButton, ui_mapConfirmPickedRoute, LV_EVENT_CLICKED, NULL);

    ui_MapPickConfirmLabel = lv_label_create(ui_MapPickConfirmButton);
    lv_label_set_text(ui_MapPickConfirmLabel, "OK");
    lv_obj_set_align(ui_MapPickConfirmLabel, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_MapPickConfirmLabel, lv_color_hex(0x06120A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MapPickConfirmLabel, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(ui_MapPickConfirmLabel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_MapPickedEndValid = false;
    ui_mapSetPickOverlayVisible(false);
}

/*********************************************************
 * @brief 更新地图底部轨迹状态提示
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapUpdateHint(void)
{
    char distance_text[24]; /* 导航距离格式化文本缓冲区 */
    char remaining_text[32]; /* 剩余里程格式化文本缓冲区 */
    uint32_t total_distance_m; /* 当前轨迹总里程，单位为米 */
    uint32_t traveled_distance_m; /* 当前轨迹已行驶里程，单位为米 */
    uint32_t display_remaining_m; /* 当前界面展示的剩余里程，单位为米 */
    uint32_t prompt_distance_m; /* 顶部导航提示显示的距离，单位为米 */
    lv_coord_t fill_height; /* 剩余里程条已行驶填充高度，单位为像素 */
    lv_offline_nav_guidance_t guidance; /* 当前导航提示状态 */
    bool has_guidance; /* true 表示当前存在有效导航路线 */

    if(ui_MapHintLabel == NULL) {
        return;
    }

    ui_mapUpdatePickOverlay();
    if(ui_MapPickedEndValid) {
        lv_label_set_text(ui_MapHintLabel, "OK?");
    } else {
        lv_label_set_text(ui_MapHintLabel, "Hold map");
    }

    if(ui_MapNavIconImage == NULL || ui_MapNavTextLabel == NULL ||
       ui_MapRouteBarFill == NULL || ui_MapRouteTextLabel == NULL) {
        return;
    }

    if(!ui_MapNavigationActive) {
        ui_mapSetNavigationOverlayVisible(false);
        return;
    }
    ui_mapSetNavigationOverlayVisible(true);

    total_distance_m = lv_offline_map_track_get_total_distance_m(ui_MapWidget);
    traveled_distance_m = lv_offline_map_track_get_traveled_distance_m(ui_MapWidget);
    display_remaining_m = lv_offline_map_track_get_remaining_distance_m(ui_MapWidget);
    has_guidance = ui_navGetGuidance(traveled_distance_m, &guidance);
    if(has_guidance) {
        prompt_distance_m = guidance.distance_to_turn_m;
        if(guidance.turn_type == LV_OFFLINE_NAV_TURN_NONE) {
            prompt_distance_m = display_remaining_m;
        }
        ui_mapFormatDistance(distance_text, sizeof(distance_text), prompt_distance_m);
        lv_image_set_src(ui_MapNavIconImage, ui_mapGetTurnIconSource(guidance.turn_type));
        lv_label_set_text(ui_MapNavTextLabel, distance_text);
    } else {
        lv_image_set_src(ui_MapNavIconImage, ui_mapGetTurnIconSource(LV_OFFLINE_NAV_TURN_NONE));
        lv_label_set_text(ui_MapNavTextLabel, "0 m");
    }

    if(total_distance_m == 0U && has_guidance) {
        total_distance_m = guidance.total_distance_m;
    }
    if(display_remaining_m == 0U && has_guidance && guidance.remaining_distance_m > 0U) {
        display_remaining_m = guidance.remaining_distance_m;
    }
    fill_height = MAP_DEMO_ROUTE_BAR_MIN_HEIGHT;
    if(total_distance_m > 0U) {
        fill_height = (lv_coord_t)((uint64_t)MAP_DEMO_ROUTE_BAR_HEIGHT * traveled_distance_m / total_distance_m);
        if(fill_height < MAP_DEMO_ROUTE_BAR_MIN_HEIGHT) {
            fill_height = MAP_DEMO_ROUTE_BAR_MIN_HEIGHT;
        } else if(fill_height > MAP_DEMO_ROUTE_BAR_HEIGHT) {
            fill_height = MAP_DEMO_ROUTE_BAR_HEIGHT;
        }
    }
    lv_obj_set_height(ui_MapRouteBarFill, fill_height);
    ui_mapFormatDistance(remaining_text, sizeof(remaining_text), display_remaining_m);
    lv_label_set_text(ui_MapRouteTextLabel, remaining_text);
}

/*********************************************************
 * @brief 填充独立离线地图演示控件配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void ui_mapFillDemoConfig(lv_offline_map_config_t *config)
{
    if(config == NULL) {
        return;
    }

    lv_offline_map_get_default_config(config);
    config->map_dir = MAP_DEMO_DIR;
    config->tile_file_name = MAP_DEMO_TILE_FILE_NAME;
    config->view_width = MAP_DEMO_VIEW_WIDTH;
    config->view_height = MAP_DEMO_VIEW_HEIGHT;
    config->min_zoom = MAP_DEMO_MIN_ZOOM;
    config->max_zoom = MAP_DEMO_MAX_ZOOM;
    config->default_zoom = MAP_DEMO_DEFAULT_ZOOM;
    config->default_tile_x = MAP_DEMO_DEFAULT_TILE_X;
    config->default_tile_y = MAP_DEMO_DEFAULT_TILE_Y;
    config->track_max_points = MAP_DEMO_TRACK_MAX_POINTS;
    /* 当前离线瓦片来自腾讯地图，路线和路网仍按 WGS84 处理，显示到瓦片前需要转换到 GCJ-02。 */
    config->use_gcj02_tile = true;
    config->show_zoom_controls = true;
    config->show_vehicle_marker = true;
    config->zoom_anim_enable = true;
    config->zoom_anim_time_ms = MAP_DEMO_ZOOM_ANIM_TIME_MS;
    config->follow_speed_kmh = MAP_DEMO_FOLLOW_SPEED_KMH;
    config->arrow_src = NULL;
    config->arrow_pivot_x = 0;
    config->arrow_pivot_y = 0;
    config->arrow_y_offset = 0;
    config->zoom_button_font = LV_FONT_DEFAULT;
    config->zoom_label_font = LV_FONT_DEFAULT;
}

/*********************************************************
 * @brief 判断事件对象是否来自地图控件交互层
 * @param target 事件触发目标对象，允许为空
 * @return bool true 表示事件目标属于地图终点选择热区，false 表示不是
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool ui_mapIsRouteSelectTarget(lv_obj_t *target)
{
    lv_obj_t *container; /* 地图控件内部滚动容器对象，长按时用于选择导航终点 */

    container = lv_offline_map_get_container(ui_MapWidget);

    return target == ui_MapWidget || target == container;
}

/*********************************************************
 * @brief 根据地图长按位置选择待确认终点
 * @param e LVGL 长按事件对象，不能为空
 * @return bool true 表示已经处理长按事件，false 表示未命中地图
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool ui_mapHandleLongPressRoute(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e); /* 当前触发长按事件的对象 */
    lv_obj_t *container; /* 地图控件内部滚动容器对象，用于确认地图已完成初始化 */
    lv_indev_t *indev; /* 当前输入设备对象，用于读取长按触点位置 */
    lv_area_t map_area; /* 地图控件在屏幕坐标系中的可视区域 */
    lv_point_t press_point; /* 当前长按触点屏幕坐标 */
    lv_coord_t view_x; /* 长按点在地图视窗内的横向坐标 */
    lv_coord_t view_y; /* 长按点在地图视窗内的纵向坐标 */
    int end_lon_e7; /* 长按选择的终点 WGS84 经度 E7 定点值 */
    int end_lat_e7; /* 长按选择的终点 WGS84 纬度 E7 定点值 */

    if(e == NULL || !ui_mapIsRouteSelectTarget(target)) {
        return false;
    }

    container = lv_offline_map_get_container(ui_MapWidget);
    indev = UI_MAP_ACTIVE_INDEV();
    if(container == NULL || indev == NULL) {
        lv_label_set_text(ui_MapHintLabel, "No touch");
        return true;
    }

    lv_indev_get_point(indev, &press_point);
    lv_obj_get_coords(ui_MapWidget, &map_area);
    view_x = press_point.x - map_area.x1;
    view_y = press_point.y - map_area.y1;
    if(!lv_offline_map_view_point_to_lonlat_e7(ui_MapWidget, view_x, view_y, &end_lon_e7, &end_lat_e7)) {
        lv_label_set_text(ui_MapHintLabel, "Pick map");
        return true;
    }

    /* 长按只更新待确认终点，确认按钮点击后才进入路线规划。 */
    ui_MapPickedEndLonE7 = end_lon_e7;
    ui_MapPickedEndLatE7 = end_lat_e7;
    ui_MapPickedEndValid = true;
    lv_label_set_text(ui_MapHintLabel, "OK?");
    ui_mapUpdatePickOverlay();

    return true;
}

/*********************************************************
 * @brief 处理离线地图页面事件
 * @param e LVGL 事件对象，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_event_MapScreen(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e); /* 当前 LVGL 事件类型 */
    lv_obj_t *target = lv_event_get_target(e); /* 当前触发事件的对象 */

    if(event_code == LV_EVENT_SCREEN_LOAD_START) {
        lv_offline_map_refresh(ui_MapWidget);
        ui_mapUpdateHint();
    } else if(event_code == LV_EVENT_SCROLL && ui_mapIsRouteSelectTarget(target)) {
        ui_mapUpdatePickOverlay();
    } else if(event_code == LV_EVENT_VALUE_CHANGED && target == ui_MapWidget) {
        ui_mapUpdateHint();
    } else if(event_code == LV_EVENT_LONG_PRESSED && ui_mapHandleLongPressRoute(e)) {
        lv_event_stop_bubbling(e);
    }
}

/*********************************************************
 * @brief 创建离线地图页面
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_Map_screen_init(void)
{
    lv_offline_map_config_t config; /* 独立地图演示地图控件配置，基于可移植组件默认配置覆盖工程参数 */

    ui_MapScreen = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_MapScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_MapScreen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ui_MapScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MapScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_mapFillDemoConfig(&config);
    ui_MapWidget = lv_offline_map_create_with_config(ui_MapScreen, &config);
    if(ui_MapWidget != NULL) {
        lv_obj_set_align(ui_MapWidget, LV_ALIGN_TOP_MID);
        lv_obj_add_event_cb(ui_MapWidget, ui_event_MapScreen, LV_EVENT_ALL, NULL);
    }

    ui_MapTitleLabel = ui_mapCreateLabel(ui_MapScreen, "Offline Map", 4, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(ui_MapTitleLabel, LV_OBJ_FLAG_HIDDEN);
    ui_MapHintLabel = ui_mapCreateLabel(ui_MapScreen, "Hold map", -10, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_width(ui_MapHintLabel, 90);
    lv_obj_set_align(ui_MapHintLabel, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_x(ui_MapHintLabel, -6);
    lv_obj_set_y(ui_MapHintLabel, -7);
    lv_obj_set_style_text_align(ui_MapHintLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_mapCreateNorthIndicator(ui_MapScreen);
    ui_mapCreateGuidanceOverlay(ui_MapScreen);
    ui_mapCreatePickOverlay(ui_MapScreen);

    lv_obj_add_event_cb(ui_MapScreen, ui_event_MapScreen, LV_EVENT_ALL, NULL);

    lv_offline_map_center_tile(ui_MapWidget, MAP_DEMO_DEFAULT_TILE_X, MAP_DEMO_DEFAULT_TILE_Y,
                               MAP_DEMO_DEFAULT_ZOOM);
    (void)ui_mapSetVehicleLocationE7(MAP_DEMO_DEFAULT_VEHICLE_LON_E7, MAP_DEMO_DEFAULT_VEHICLE_LAT_E7,
                                     MAP_DEMO_DEFAULT_ZOOM, true);
    ui_MapNavigationActive = false;
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 获取离线地图页面对象
 * @return lv_obj_t* 地图页面对象，未初始化时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
lv_obj_t *ui_mapGetScreen(void)
{
    return ui_MapScreen;
}

/*********************************************************
 * @brief 切换地图中心瓦片并刷新显示
 * @param tile_x 中心瓦片 X 坐标
 * @param tile_y 中心瓦片 Y 坐标
 * @param zoom 瓦片缩放级别
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_changeMap(int tile_x, int tile_y, int zoom)
{
    lv_offline_map_center_tile(ui_MapWidget, tile_x, tile_y, zoom);
}

/*********************************************************
 * @brief 按 E7 经纬度居中离线地图视野
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示地图已居中，false 表示地图未初始化或换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapCenterLonLatE7(int lon_e7, int lat_e7, int zoom)
{
    bool centered = lv_offline_map_center_lonlat_e7(ui_MapWidget, lon_e7, lat_e7, zoom); /* 地图居中结果，true 表示已移动到目标经纬度 */

    return centered;
}

/*********************************************************
 * @brief 获取当前离线地图缩放级别
 * @return int 当前地图缩放级别，地图未初始化时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
int ui_mapGetZoom(void)
{
    int zoom; /* 当前地图控件缩放级别，用于路线重绘和定位投影 */

    zoom = lv_offline_map_get_zoom(ui_MapWidget);

    return zoom;
}

/*********************************************************
 * @brief 滚动地图到中心瓦片内指定像素位置
 * @param x 中心瓦片内的横向像素坐标
 * @param y 中心瓦片内的纵向像素坐标
 * @param anim 是否启用 LVGL 滚动动画
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_scrollMap(int x, int y, lv_anim_enable_t anim)
{
    lv_offline_map_scroll_to_tile_pixel(ui_MapWidget, x, y, anim);
}

/*********************************************************
 * @brief 清空当前轨迹记录
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackClear(void)
{
    lv_offline_map_track_clear(ui_MapWidget);
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 开始批量写入轨迹点并暂缓刷新轨迹图层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackBeginBatch(void)
{
    lv_offline_map_track_begin_batch(ui_MapWidget);
}

/*********************************************************
 * @brief 结束批量写入轨迹点并统一刷新轨迹图层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackEndBatch(void)
{
    lv_offline_map_track_end_batch(ui_MapWidget);
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 启动轨迹自动跟随播放
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackStartFollow(void)
{
    lv_offline_map_track_start_follow(ui_MapWidget);
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 停止轨迹自动跟随播放
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapTrackStopFollow(void)
{
    lv_offline_map_track_stop_follow(ui_MapWidget);
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 设置地图页面是否处于导航显示态
 * @param active true 表示显示路线提示和剩余里程，false 表示隐藏导航叠层
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-21
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapSetNavigationActive(bool active)
{
    int vehicle_lon_e7; /* 退出导航前记录的车辆经度 E7 定点值，用于清轨迹后恢复车标 */
    int vehicle_lat_e7; /* 退出导航前记录的车辆纬度 E7 定点值，用于清轨迹后恢复车标 */
    bool has_vehicle_location; /* true 表示清空路线前存在可恢复的车辆位置 */

    ui_MapNavigationActive = active;
    if(!active) {
        has_vehicle_location = ui_mapGetVirtualLocationE7(&vehicle_lon_e7, &vehicle_lat_e7);
        lv_offline_map_track_stop_follow(ui_MapWidget);
        lv_offline_map_track_clear(ui_MapWidget);
        if(has_vehicle_location) {
            /* 清除导航路线只移除轨迹，不应丢失当前车辆地理位置。 */
            (void)ui_mapSetVehicleLocationE7(vehicle_lon_e7, vehicle_lat_e7, ui_mapGetZoom(), false);
        }
    }
    ui_mapSetNavigationOverlayVisible(active);
    ui_mapUpdateHint();
}

/*********************************************************
 * @brief 在当前中心瓦片内添加轨迹点
 * @param pixel_x 轨迹点在中心瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在中心瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddPoint(int pixel_x, int pixel_y)
{
    bool added = lv_offline_map_track_add_point(ui_MapWidget, pixel_x, pixel_y); /* 轨迹点添加结果，true 表示已写入组件缓存 */

    if(added) {
        ui_mapUpdateHint();
    }

    return added;
}

/*********************************************************
 * @brief 按瓦片坐标添加轨迹点
 * @param tile_x 轨迹点所在瓦片 X 坐标
 * @param tile_y 轨迹点所在瓦片 Y 坐标
 * @param pixel_x 轨迹点在瓦片内的横向像素坐标
 * @param pixel_y 轨迹点在瓦片内的纵向像素坐标
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddTilePoint(int tile_x, int tile_y, int pixel_x, int pixel_y)
{
    bool added = lv_offline_map_track_add_tile_point(ui_MapWidget, tile_x, tile_y, pixel_x, pixel_y); /* 轨迹点添加结果，true 表示已写入组件缓存 */

    if(added) {
        ui_mapUpdateHint();
    }

    return added;
}

/*********************************************************
 * @brief 按 E7 经纬度添加轨迹点
 * @param lon_e7 经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 纬度 E7 定点值，单位为 1e-7 度
 * @param zoom 经纬度换算时使用的瓦片缩放级别
 * @return bool true 表示轨迹点已写入，false 表示地图未初始化或换算失败
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool ui_mapTrackAddLonLatE7(int lon_e7, int lat_e7, int zoom)
{
    bool added = lv_offline_map_track_add_lonlat_e7(ui_MapWidget, lon_e7, lat_e7, zoom); /* 经纬度轨迹点添加结果，true 表示已写入组件缓存 */

    if(added) {
        ui_mapUpdateHint();
    }

    return added;
}

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
bool ui_mapTrackAddLonLatE7WithDistance(int lon_e7, int lat_e7, int zoom, uint32_t distance_m)
{
    bool added = lv_offline_map_track_add_lonlat_e7_with_distance(ui_MapWidget, lon_e7, lat_e7, zoom,
                                                                  distance_m); /* 带累计里程轨迹点添加结果，true 表示已写入组件缓存 */

    if(added) {
        ui_mapUpdateHint();
    }

    return added;
}

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
bool ui_mapUpdateLocationE7(int lon_e7, int lat_e7, int zoom, ui_nav_location_result_t *result)
{
    lv_offline_map_location_result_t map_result; /* 地图控件返回的定位投影结果，用于转换成 独立地图演示公共结构 */
    bool updated; /* 实时定位更新结果，true 表示车标和导航距离已同步到当前定位 */

    updated = lv_offline_map_track_update_location_e7(ui_MapWidget, lon_e7, lat_e7, zoom,
                                                      result == NULL ? NULL : &map_result);
    if(updated) {
        if(result != NULL) {
            result->traveled_distance_m = map_result.traveled_distance_m;
            result->remaining_distance_m = map_result.remaining_distance_m;
            result->deviation_distance_m = map_result.deviation_distance_m;
            result->segment_index = map_result.segment_index;
        }
        ui_MapNavigationActive = true;
        ui_mapUpdateHint();
    }

    return updated;
}

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
bool ui_mapSetVehicleLocationE7(int lon_e7, int lat_e7, int zoom, bool center)
{
    bool updated; /* 车辆车标位置更新结果，true 表示车标已显示在地图层 */

    updated = lv_offline_map_set_vehicle_location_e7(ui_MapWidget, lon_e7, lat_e7, zoom, center);
    if(updated) {
        ui_mapUpdateHint();
    }

    return updated;
}

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
bool ui_mapGetVirtualLocationE7(int *lon_e7, int *lat_e7)
{
    bool has_location; /* true 表示地图控件存在当前跟随车辆位置 */

    has_location = lv_offline_map_track_get_follow_location_e7(ui_MapWidget, lon_e7, lat_e7);

    return has_location;
}

/*********************************************************
 * @brief 写入默认演示轨迹
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-18
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_mapSeedDemoTrack(void)
{
    ui_mapTrackBeginBatch();
    ui_mapTrackClear();

    /* 使用 SD 卡 map 目录内已存在的瓦片范围生成一段跨瓦片路线，便于离线状态直接验证显示效果。 */
    ui_mapTrackAddTilePoint(107068, 53160, 220, 212);
    ui_mapTrackAddTilePoint(107069, 53161, 64, 188);
    ui_mapTrackAddTilePoint(107070, 53162, 118, 44);
    ui_mapTrackAddTilePoint(107071, 53163, 170, 126);
    ui_mapTrackAddTilePoint(107072, 53164, 36, 156);
    ui_mapTrackAddTilePoint(107073, 53165, 112, 38);
    ui_mapTrackAddTilePoint(107074, 53166, 42, 94);
    ui_mapTrackEndBatch();
}
