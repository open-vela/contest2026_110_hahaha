############################################################################
# Contest 2026 team 110 qiban native UI - Makefile
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += -I$(CURDIR)/../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_map
CFLAGS += -I$(CURDIR)/../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_nav

PROGNAME  = qiban_ui
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 24576
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI)

MAINSRC = qiban_ui_main.c
CSRCS = qiban_cjk_font.c \
	../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_map/lv_offline_map.c \
	../../third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_widgets/lv_offline_nav/lv_offline_nav.c

include $(APPDIR)/Application.mk
