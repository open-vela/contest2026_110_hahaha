/****************************************************************************
 * skill_nav_turn.c
 *
 * Navigation turn-by-turn reminder skill for the Qiban AI Agent.
 * Triggers when approaching a turn (200m first alert, 50m urgent).
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

#define TURN_FIRST_DIST_M   200
#define TURN_URGENT_DIST_M  50

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool nav_turn_evaluate(qiban_skill_t *skill,
                              const qiban_vehicle_state_t *vs)
{
  if (!vs->nav_active)
    {
      return false;
    }

  if (vs->nav_turn_dist_m <= 0)
    {
      return false;
    }

  /* Within first alert distance */

  return vs->nav_turn_dist_m <= (int)skill->config.threshold;
}

static void nav_turn_build_action(qiban_skill_t *skill,
                                  const qiban_vehicle_state_t *vs,
                                  qiban_action_cmd_t *cmd)
{
  bool urgent = (vs->nav_turn_dist_m <= (int)skill->config.threshold2);

  cmd->type     = ACTION_VOICE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = urgent ? SKILL_PRI_HIGH : skill->priority;

  snprintf(cmd->u.voice.text, sizeof(cmd->u.voice.text),
           "%s\xef\xbc\x8c\xe5\x89\x8d\xe6\x96\xb9"
           "%d\xe7\xb1\xb3",
           vs->nav_next_turn, vs->nav_turn_dist_m);
  /* "{turn}，前方{dist}米" */

  cmd->u.voice.interrupt = urgent;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_nav_turn =
{
  .id           = "nav_turn",
  .name         = "Nav Turn Reminder",
  .description  = "Remind about upcoming turns",
  .trigger_type = TRIGGER_THRESHOLD,
  .action_type  = ACTION_VOICE,
  .priority     = SKILL_PRI_NORMAL,
  .evaluate     = nav_turn_evaluate,
  .build_action = nav_turn_build_action,
  .config       =
  {
    .threshold    = TURN_FIRST_DIST_M,
    .threshold2   = TURN_URGENT_DIST_M,
    .cooldown_sec = 60,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
