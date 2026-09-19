# Contest 2026 team 110 qiban navigation service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_NAV_SERVICE)
  nuttx_add_application(
    NAME qiban_nav_service
    SRCS qiban_nav_service_main.c
    STACKSIZE 24576
    INCLUDE_DIRECTORIES ${NUTTX_APPS_DIR}/../external/curl/curl/include
    DEPENDS curl)
endif()
