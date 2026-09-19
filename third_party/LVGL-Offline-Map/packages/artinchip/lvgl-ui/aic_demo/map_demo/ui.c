/*********************************************************
 * @file ui.c
 * @author ^^^^^^^ ()
 * @brief 初始化独立离线地图演示主界面
 * @version 1.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#include "ui.h"

/*********************************************************
 * @brief 初始化独立离线地图演示界面
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void ui_init(void)
{
    lv_obj_t *map_screen; /* 独立地图演示页面对象，创建成功后立即作为当前屏幕加载 */

    ui_Map_screen_init();
    map_screen = ui_mapGetScreen();
    if(map_screen != NULL) {
        lv_scr_load(map_screen);
    }
}
