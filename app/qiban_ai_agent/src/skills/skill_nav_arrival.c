/****************************************************************************
 * skill_nav_arrival.c
 *
 * Navigation arrival announcement skill for the Qiban AI Agent.
 * Triggers when the user arrives at the navigation destination.
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

#define ARRIVAL_THRESHOLD_KM  0.05f

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool nav_arrival_evaluate(qiban_skill_t *skill,
                                 const qiban_vehicle_state_t *vs)
{
  if (!vs->nav_active)
    {
      return false;
    }

  if (strcmp(vs->nav_status, "arrived") == 0)
    {
      return true;
    }

  if (vs->nav_remaining_km > 0 &&
      vs->nav_remaining_km <= skill->config.threshold)
    {
      return true;
    }

  return false;
}

static void nav_arrival_build_action(qiban_skill_t *skill,
                                     const qiban_vehicle_state_t *vs,
                                     qiban_action_cmd_t *cmd)
{
  static qiban_action_cmd_t step_voice;
  static qiban_action_cmd_t step_ui;

  memset(&step_voice, 0, sizeof(step_voice));
  memset(&step_ui, 0, sizeof(step_ui));

  cmd->type     = ACTION_COMPOSITE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;
  cmd->u.composite.count = 2;

  /* Step 1: Voice announcement */

  step_voice.type = ACTION_VOICE;
  snprintf(step_voice.u.voice.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe5\xb7\xb2\xe5\x88\xb0\xe8\xbe\xbe\xe7\x9b\xae\xe7\x9a\x84"
           "\xe5\x9c\xb0");
  step_voice.u.voice.interrupt = true;
  cmd->u.composite.steps[0] = &step_voice;

  /* Step 2: Switch back to dashboard */

  step_ui.type = ACTION_UI_SWITCH;
  step_ui.u.ui.page = UI_PAGE_DASHBOARD;
  step_ui.u.ui.timeout_sec = 0;
  cmd->u.composite.steps[1] = &step_ui;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_nav_arrival =
{
  .id           = "nav_arrival",
  .name         = "Nav Arrival",
  .description  = "Announce when arriving at destination",
  .trigger_type = TRIGGER_EVENT,
  .action_type  = ACTION_COMPOSITE,
  .priority     = SKILL_PRI_HIGH,
  .evaluate     = nav_arrival_evaluate,
  .build_action = nav_arrival_build_action,
  .config       =
  {
    .threshold    = ARRIVAL_THRESHOLD_KM,
    .threshold2   = 0.0f,
    .cooldown_sec = 300,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
