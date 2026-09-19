# Contest 2026 team 110 qiban native UI
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI)
  nuttx_add_application(
    NAME qiban_ui
    SRCS qiban_ui_main.c
         qiban_cjk_font.c
         ../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_map/lv_offline_map.c
         ../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_nav/lv_offline_nav.c
    INCLUDE_DIRECTORIES
         ${CMAKE_CURRENT_LIST_DIR}/../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_map
         ${CMAKE_CURRENT_LIST_DIR}/../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_nav
    STACKSIZE 24576)
endif()
