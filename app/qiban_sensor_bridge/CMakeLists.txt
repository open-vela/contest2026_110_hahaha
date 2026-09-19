# Contest 2026 team 110 qiban sensor bridge
if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_SENSOR_BRIDGE)
  nuttx_add_application(
    NAME qiban_sensor_bridge
    SRCS qiban_sensor_bridge_main.c
    STACKSIZE 8192)
endif()
