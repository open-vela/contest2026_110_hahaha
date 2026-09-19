/****************************************************************************
 * skill_emergency_stop.c
 *
 * Emergency stop detection skill for the Qiban AI Agent.
 * Triggers when speed drops rapidly (hard braking detected).
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

#define SPEED_THRESHOLD_KMH    15.0f
#define SPEED_DROP_KMH         12.0f
#define EMERGENCY_WINDOW_SEC   2

/****************************************************************************
 * External References
 ****************************************************************************/

extern float qiban_speed_history_at(int seconds_ago);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool emergency_stop_evaluate(qiban_skill_t *skill,
                                    const qiban_vehicle_state_t *vs)
{
  float speed_2s_ago;

  if (vs->speed_kmh >= 3.0f)
    {
      return false;
    }

  speed_2s_ago = qiban_speed_history_at(EMERGENCY_WINDOW_SEC);

  return (speed_2s_ago > skill->config.threshold &&
          (speed_2s_ago - vs->speed_kmh) > skill->config.threshold2);
}

static void emergency_stop_build_action(qiban_skill_t *skill,
                                        const qiban_vehicle_state_t *vs,
                                        qiban_action_cmd_t *cmd)
{
  static qiban_action_cmd_t step_voice;
  static qiban_action_cmd_t step_alert;
  static qiban_action_cmd_t step_ui;

  memset(&step_voice, 0, sizeof(step_voice));
  memset(&step_alert, 0, sizeof(step_alert));
  memset(&step_ui, 0, sizeof(step_ui));

  cmd->type     = ACTION_COMPOSITE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;
  cmd->u.composite.count = 3;

  /* Step 1: Voice alert */

  step_voice.type = ACTION_VOICE;
  snprintf(step_voice.u.voice.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe7\xb4\xa7\xe6\x80\xa5"
           "\xe5\x88\xb6\xe5\x8a\xa8\xef\xbc\x8c\xe8\xaf\xb7\xe6\xb3\xa8\xe6\x84\x8f"
           "\xe5\xae\x89\xe5\x85\xa8");
  step_voice.u.voice.interrupt = true;
  cmd->u.composite.steps[0] = &step_voice;

  /* Step 2: Critical UI alert */

  step_alert.type = ACTION_ALERT;
  step_alert.u.alert.severity = ALERT_CRITICAL;
  snprintf(step_alert.u.alert.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe7\xb4\xa7\xe6\x80\xa5\xe5\x88\xb6\xe5\x8a\xa8");
  step_alert.u.alert.duration_ms = 5000;
  cmd->u.composite.steps[1] = &step_alert;

  /* Step 3: Switch to dashboard */

  step_ui.type = ACTION_UI_SWITCH;
  step_ui.u.ui.page = UI_PAGE_DASHBOARD;
  step_ui.u.ui.timeout_sec = 10;
  cmd->u.composite.steps[2] = &step_ui;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_emergency_stop =
{
  .id           = "emergency_stop",
  .name         = "Emergency Stop",
  .description  = "Detect hard braking events",
  .trigger_type = TRIGGER_TREND,
  .action_type  = ACTION_COMPOSITE,
  .priority     = SKILL_PRI_EMERGENCY,
  .evaluate     = emergency_stop_evaluate,
  .build_action = emergency_stop_build_action,
  .config       =
  {
    .threshold    = SPEED_THRESHOLD_KMH,
    .threshold2   = SPEED_DROP_KMH,
    .cooldown_sec = 30,
    .window_sec   = EMERGENCY_WINDOW_SEC,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
