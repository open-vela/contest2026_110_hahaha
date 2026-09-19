############################################################################
# Contest 2026 team 110 qiban AI agent - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = qiban_ai
PRIORITY  = 100
STACKSIZE = $(CONFIG_AI_AGENT_STACK_SIZE)
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_AI_AGENT)

# Core source files

CSRCS = src/ai_agent_main.c \
        src/agent_core.c \
        src/skill_registry.c \
        src/json_reader.c \
        src/action_executor.c \
        src/cli.c \
        src/state_analyzer.c

# Skills (conditionally compiled)

ifeq ($(CONFIG_AI_SKILL_OVERSPEED),y)
CSRCS += src/skills/skill_overspeed.c
endif

ifeq ($(CONFIG_AI_SKILL_LOW_BATTERY),y)
CSRCS += src/skills/skill_low_battery.c
endif

ifeq ($(CONFIG_AI_SKILL_FATIGUE),y)
CSRCS += src/skills/skill_fatigue.c
endif

ifeq ($(CONFIG_AI_SKILL_NAV_ARRIVAL),y)
CSRCS += src/skills/skill_nav_arrival.c
endif

ifeq ($(CONFIG_AI_SKILL_NAV_TURN),y)
CSRCS += src/skills/skill_nav_turn.c
endif

ifeq ($(CONFIG_AI_SKILL_SPEED_TREND),y)
CSRCS += src/skills/skill_speed_trend.c
endif

ifeq ($(CONFIG_AI_SKILL_RIDE_SUMMARY),y)
CSRCS += src/skills/skill_ride_summary.c
endif

ifeq ($(CONFIG_AI_SKILL_ROUTE_SUGGEST),y)
CSRCS += src/skills/skill_route_suggest.c
endif

ifeq ($(CONFIG_AI_SKILL_WEATHER_SAFETY),y)
CSRCS += src/skills/skill_weather_safety.c
endif

ifeq ($(CONFIG_AI_SKILL_EMERGENCY_STOP),y)
CSRCS += src/skills/skill_emergency_stop.c
endif

CFLAGS += -I$(APPDIR)/demos/contest2026_110_qiban_ai_agent/include

include $(APPDIR)/Application.mk
