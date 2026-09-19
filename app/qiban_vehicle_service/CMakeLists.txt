# Contest 2026 team 110 qiban vehicle service
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VEHICLE_SERVICE)
  nuttx_add_application(
    NAME qiban_vehicle_service
    SRCS qiban_vehicle_service_main.c
    STACKSIZE 4096)
endif()
