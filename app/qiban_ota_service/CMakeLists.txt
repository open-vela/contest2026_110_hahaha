# Contest 2026 team 110 qiban OTA service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE)
  nuttx_add_application(
    NAME qiban_ota_service
    SRCS qiban_ota_service_main.c
    STACKSIZE 8192)
endif()
