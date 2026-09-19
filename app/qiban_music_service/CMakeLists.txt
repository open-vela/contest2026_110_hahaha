# Contest 2026 team 110 qiban music service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MUSIC_SERVICE)
  nuttx_add_application(
    NAME qiban_music_service
    SRCS qiban_music_service_main.c
    STACKSIZE 16384)
endif()
