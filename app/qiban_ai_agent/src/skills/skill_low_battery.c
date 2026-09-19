/****************************************************************************
 * skill_low_battery.c
 *
 * Low battery warning skill for the Qiban AI Agent.
 * Triggers when battery level drops to or below threshold (default 20%).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "skill.h"
#include "vehicle_state.h"
#include "action_cmd.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool low_battery_evaluate(qiban_skill_t *skill,
                                 const qiban_vehicle_state_t *vs)
{
  return vs->battery_pct <= skill->config.threshold &&
         vs->battery_pct > 0.0f;
}

static void low_battery_build_action(qiban_skill_t *skill,
                                     const qiban_vehicle_state_t *vs,
                                     qiban_action_cmd_t *cmd)
{
  static qiban_action_cmd_t step_voice;
  static qiban_action_cmd_t step_alert;

  memset(&step_voice, 0, sizeof(step_voice));
  memset(&step_alert, 0, sizeof(step_alert));

  cmd->type     = ACTION_COMPOSITE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;
  cmd->u.composite.count = 2;

  /* Step 1: Voice warning */

  step_voice.type = ACTION_VOICE;
  snprintf(step_voice.u.voice.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe7\x94\xb5\xe9\x87\x8f\xe5\x89\xa9\xe4\xbd\x99"
           "%.0f%%\xef\xbc\x8c\xe8\xaf\xb7\xe5\x8f\x8a\xe6\x97\xb6"
           "\xe5\x85\x85\xe7\x94\xb5",
           vs->battery_pct);
  step_voice.u.voice.interrupt = true;
  cmd->u.composite.steps[0] = &step_voice;

  /* Step 2: UI alert overlay */

  step_alert.type = ACTION_ALERT;
  step_alert.u.alert.severity = ALERT_WARNING;
  snprintf(step_alert.u.alert.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe7\x94\xb5\xe9\x87\x8f\xe4\xb8\x8d\xe8\xb6\xb3 %.0f%%",
           vs->battery_pct);
  step_alert.u.alert.duration_ms = 5000;
  cmd->u.composite.steps[1] = &step_alert;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_low_battery =
{
  .id           = "low_battery",
  .name         = "Low Battery Warning",
  .description  = "Warn when battery level is critically low",
  .trigger_type = TRIGGER_THRESHOLD,
  .action_type  = ACTION_COMPOSITE,
  .priority     = SKILL_PRI_HIGH,
  .evaluate     = low_battery_evaluate,
  .build_action = low_battery_build_action,
  .config       =
  {
    .threshold    = CONFIG_AI_SKILL_LOW_BATTERY_THRESHOLD,
    .threshold2   = 0.0f,
    .cooldown_sec = 120,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
