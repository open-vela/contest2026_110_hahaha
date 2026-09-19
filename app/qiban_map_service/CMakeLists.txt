# Contest 2026 team 110 qiban map service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MAP_SERVICE)
  nuttx_add_application(
    NAME qiban_map_service
    SRCS qiban_map_service_main.c
    STACKSIZE 16384
    INCLUDE_DIRECTORIES ${NUTTX_APPS_DIR}/../external/curl/curl/include
    DEPENDS curl)
endif()
