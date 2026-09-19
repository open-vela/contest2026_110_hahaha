# Contest 2026 team 110 qiban AI agent

if(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_AI_AGENT)

nuttx_add_application(
  NAME qiban_ai
  SRCS
    src/ai_agent_main.c
    src/agent_core.c
    src/skill_registry.c
    src/json_reader.c
    src/action_executor.c
    src/cli.c
    src/state_analyzer.c
  STACKSIZE ${CONFIG_AI_AGENT_STACK_SIZE}
  PRIORITY 100
  INCLUDE_DIRECTORIES include
)

# Conditional skill sources

if(CONFIG_AI_SKILL_OVERSPEED)
  target_sources(qiban_ai PRIVATE src/skills/skill_overspeed.c)
endif()

if(CONFIG_AI_SKILL_LOW_BATTERY)
  target_sources(qiban_ai PRIVATE src/skills/skill_low_battery.c)
endif()

if(CONFIG_AI_SKILL_FATIGUE)
  target_sources(qiban_ai PRIVATE src/skills/skill_fatigue.c)
endif()

if(CONFIG_AI_SKILL_NAV_ARRIVAL)
  target_sources(qiban_ai PRIVATE src/skills/skill_nav_arrival.c)
endif()

if(CONFIG_AI_SKILL_NAV_TURN)
  target_sources(qiban_ai PRIVATE src/skills/skill_nav_turn.c)
endif()

if(CONFIG_AI_SKILL_SPEED_TREND)
  target_sources(qiban_ai PRIVATE src/skills/skill_speed_trend.c)
endif()

if(CONFIG_AI_SKILL_RIDE_SUMMARY)
  target_sources(qiban_ai PRIVATE src/skills/skill_ride_summary.c)
endif()

if(CONFIG_AI_SKILL_ROUTE_SUGGEST)
  target_sources(qiban_ai PRIVATE src/skills/skill_route_suggest.c)
endif()

if(CONFIG_AI_SKILL_WEATHER_SAFETY)
  target_sources(qiban_ai PRIVATE src/skills/skill_weather_safety.c)
endif()

if(CONFIG_AI_SKILL_EMERGENCY_STOP)
  target_sources(qiban_ai PRIVATE src/skills/skill_emergency_stop.c)
endif()

endif()
