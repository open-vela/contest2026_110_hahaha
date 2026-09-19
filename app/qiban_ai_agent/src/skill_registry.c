/****************************************************************************
 * skill_registry.c
 *
 * Static skill table and registration for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "skill.h"
#include "vehicle_state.h"
#include "action_cmd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_SKILLS  CONFIG_AI_AGENT_MAX_SKILLS

/****************************************************************************
 * External Skill Declarations
 ****************************************************************************/

/* Each skill file defines a global skill descriptor */

#ifdef CONFIG_AI_SKILL_OVERSPEED
extern qiban_skill_t g_skill_overspeed;
#endif
#ifdef CONFIG_AI_SKILL_LOW_BATTERY
extern qiban_skill_t g_skill_low_battery;
#endif
#ifdef CONFIG_AI_SKILL_FATIGUE
extern qiban_skill_t g_skill_fatigue;
#endif
#ifdef CONFIG_AI_SKILL_NAV_ARRIVAL
extern qiban_skill_t g_skill_nav_arrival;
#endif
#ifdef CONFIG_AI_SKILL_NAV_TURN
extern qiban_skill_t g_skill_nav_turn;
#endif
#ifdef CONFIG_AI_SKILL_SPEED_TREND
extern qiban_skill_t g_skill_speed_trend;
#endif
#ifdef CONFIG_AI_SKILL_RIDE_SUMMARY
extern qiban_skill_t g_skill_ride_summary;
#endif
#ifdef CONFIG_AI_SKILL_ROUTE_SUGGEST
extern qiban_skill_t g_skill_route_suggest;
#endif
#ifdef CONFIG_AI_SKILL_WEATHER_SAFETY
extern qiban_skill_t g_skill_weather_safety;
#endif
#ifdef CONFIG_AI_SKILL_EMERGENCY_STOP
extern qiban_skill_t g_skill_emergency_stop;
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_qiban_skills[MAX_SKILLS];
int           g_qiban_skill_count = 0;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_skill_register
 ****************************************************************************/

int qiban_skill_register(qiban_skill_t *skill)
{
  if (g_qiban_skill_count >= MAX_SKILLS)
    {
      printf("ai_agent: skill table full, cannot register '%s'\n",
             skill->id);
      return -ENOSPC;
    }

  memcpy(&g_qiban_skills[g_qiban_skill_count], skill,
         sizeof(qiban_skill_t));
  g_qiban_skill_count++;
  return 0;
}

/****************************************************************************
 * Name: qiban_skill_find
 ****************************************************************************/

qiban_skill_t *qiban_skill_find(FAR const char *id)
{
  int i;

  for (i = 0; i < g_qiban_skill_count; i++)
    {
      if (strcmp(g_qiban_skills[i].id, id) == 0)
        {
          return &g_qiban_skills[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: qiban_skill_state_str
 ****************************************************************************/

FAR const char *qiban_skill_state_str(enum qiban_skill_state_e state)
{
  switch (state)
    {
      case SKILL_STATE_IDLE:
        return "idle";
      case SKILL_STATE_FIRED:
        return "fired";
      case SKILL_STATE_COOLDOWN:
        return "cooldown";
      case SKILL_STATE_DISABLED:
        return "disabled";
      default:
        return "unknown";
    }
}

/****************************************************************************
 * Name: qiban_skill_priority_str
 ****************************************************************************/

FAR const char *qiban_skill_priority_str(enum qiban_skill_priority_e pri)
{
  switch (pri)
    {
      case SKILL_PRI_EMERGENCY:
        return "EMRG";
      case SKILL_PRI_HIGH:
        return "HIGH";
      case SKILL_PRI_NORMAL:
        return "NORM";
      case SKILL_PRI_LOW:
        return "LOW";
      case SKILL_PRI_INFO:
        return "INFO";
      default:
        return "????";
    }
}

/****************************************************************************
 * Name: qiban_skill_registry_init
 *
 * Description:
 *   Register all compiled skills into the global table.
 *
 ****************************************************************************/

void qiban_skill_registry_init(void)
{
  g_qiban_skill_count = 0;

#ifdef CONFIG_AI_SKILL_OVERSPEED
  qiban_skill_register(&g_skill_overspeed);
#endif
#ifdef CONFIG_AI_SKILL_LOW_BATTERY
  qiban_skill_register(&g_skill_low_battery);
#endif
#ifdef CONFIG_AI_SKILL_FATIGUE
  qiban_skill_register(&g_skill_fatigue);
#endif
#ifdef CONFIG_AI_SKILL_NAV_ARRIVAL
  qiban_skill_register(&g_skill_nav_arrival);
#endif
#ifdef CONFIG_AI_SKILL_NAV_TURN
  qiban_skill_register(&g_skill_nav_turn);
#endif
#ifdef CONFIG_AI_SKILL_SPEED_TREND
  qiban_skill_register(&g_skill_speed_trend);
#endif
#ifdef CONFIG_AI_SKILL_RIDE_SUMMARY
  qiban_skill_register(&g_skill_ride_summary);
#endif
#ifdef CONFIG_AI_SKILL_ROUTE_SUGGEST
  qiban_skill_register(&g_skill_route_suggest);
#endif
#ifdef CONFIG_AI_SKILL_WEATHER_SAFETY
  qiban_skill_register(&g_skill_weather_safety);
#endif
#ifdef CONFIG_AI_SKILL_EMERGENCY_STOP
  qiban_skill_register(&g_skill_emergency_stop);
#endif

  printf("ai_agent: registered %d skills\n", g_qiban_skill_count);
}
