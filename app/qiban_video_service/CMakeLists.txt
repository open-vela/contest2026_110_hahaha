# Contest 2026 team 110 qiban video service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VIDEO_SERVICE)
  nuttx_add_application(
    NAME qiban_video_service
    SRCS qiban_video_service_main.c
    STACKSIZE 32768)
endif()
