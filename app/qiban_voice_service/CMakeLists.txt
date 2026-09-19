# Contest 2026 team 110 qiban voice service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VOICE_SERVICE)
  nuttx_add_application(
    NAME qiban_voice_service
    SRCS qiban_voice_service_main.c
    STACKSIZE 49152
    INCLUDE_DIRECTORIES ${NUTTX_APPS_DIR}/../external/curl/curl/include
    DEPENDS curl)
endif()
