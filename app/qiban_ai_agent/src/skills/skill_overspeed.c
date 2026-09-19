/****************************************************************************
 * skill_overspeed.c
 *
 * Overspeed alert skill for the Qiban AI Agent.
 * Triggers when vehicle speed exceeds threshold (default 25 km/h).
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

static bool overspeed_evaluate(qiban_skill_t *skill,
                               const qiban_vehicle_state_t *vs)
{
  return vs->is_moving && vs->speed_kmh > skill->config.threshold;
}

static void overspeed_build_action(qiban_skill_t *skill,
                                   const qiban_vehicle_state_t *vs,
                                   qiban_action_cmd_t *cmd)
{
  cmd->type     = ACTION_VOICE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;

  snprintf(cmd->u.voice.text, sizeof(cmd->u.voice.text),
           "\xe5\xbd\x93\xe5\x89\x8d\xe9\x80\x9f\xe5\xba\xa6"
           "%.0f\xe5\x85\xac\xe9\x87\x8c\xe6\xaf\x8f\xe5\xb0\x8f"
           "\xe6\x97\xb6\xef\xbc\x8c\xe8\xaf\xb7\xe6\xb3\xa8\xe6\x84\x8f"
           "\xe5\x87\x8f\xe9\x80\x9f",
           vs->speed_kmh);
  /* "当前速度X公里每小时，请注意减速" */

  cmd->u.voice.interrupt = true;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_overspeed =
{
  .id           = "overspeed",
  .name         = "Overspeed Alert",
  .description  = "Alert when speed exceeds threshold",
  .trigger_type = TRIGGER_THRESHOLD,
  .action_type  = ACTION_VOICE,
  .priority     = SKILL_PRI_HIGH,
  .evaluate     = overspeed_evaluate,
  .build_action = overspeed_build_action,
  .config       =
  {
    .threshold    = CONFIG_AI_SKILL_OVERSPEED_THRESHOLD,
    .threshold2   = 0.0f,
    .cooldown_sec = 30,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
