/****************************************************************************
 * skill_route_suggest.c
 *
 * Proactive route suggestion skill for the Qiban AI Agent.
 * Suggests navigation when riding without an active route.
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

#define SUGGEST_RIDE_TIME_SEC  300  /* 5 minutes riding without nav */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool route_suggest_evaluate(qiban_skill_t *skill,
                                   const qiban_vehicle_state_t *vs)
{
  /* Only suggest if no active navigation */

  if (vs->nav_active)
    {
      return false;
    }

  /* Must be actively moving */

  if (!vs->is_moving)
    {
      return false;
    }

  /* Must have been riding for a while */

  return vs->ride_duration_sec >= (uint32_t)skill->config.threshold;
}

static void route_suggest_build_action(qiban_skill_t *skill,
                                       const qiban_vehicle_state_t *vs,
                                       qiban_action_cmd_t *cmd)
{
  cmd->type     = ACTION_VOICE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;

  snprintf(cmd->u.voice.text, sizeof(cmd->u.voice.text),
           "\xe6\x98\xaf\xe5\x90\xa6\xe9\x9c\x80\xe8\xa6\x81"
           "\xe5\xaf\xbc\xe8\x88\xaa\xe5\x88\xb0\xe5\xb8\xb8\xe5\x8e\xbb"
           "\xe5\x9c\xb0\xe7\x82\xb9\xef\xbc\x9f");
  /* "是否需要导航到常去地点？" */

  cmd->u.voice.interrupt = false;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_route_suggest =
{
  .id           = "route_suggest",
  .name         = "Route Suggestion",
  .description  = "Suggest navigation when riding without route",
  .trigger_type = TRIGGER_POLL,
  .action_type  = ACTION_VOICE,
  .priority     = SKILL_PRI_LOW,
  .evaluate     = route_suggest_evaluate,
  .build_action = route_suggest_build_action,
  .config       =
  {
    .threshold    = SUGGEST_RIDE_TIME_SEC,
    .threshold2   = 0.0f,
    .cooldown_sec = 1800,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
