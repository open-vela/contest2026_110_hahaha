/****************************************************************************
 * skill_fatigue.c
 *
 * Fatigue riding alert skill for the Qiban AI Agent.
 * Triggers when continuous riding exceeds threshold (default 45 min).
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

static bool fatigue_evaluate(qiban_skill_t *skill,
                             const qiban_vehicle_state_t *vs)
{
  /* Only trigger if currently moving */

  if (!vs->is_moving)
    {
      return false;
    }

  return vs->ride_duration_sec >= (uint32_t)skill->config.threshold;
}

static void fatigue_build_action(qiban_skill_t *skill,
                                 const qiban_vehicle_state_t *vs,
                                 qiban_action_cmd_t *cmd)
{
  uint32_t minutes = vs->ride_duration_sec / 60;

  cmd->type     = ACTION_VOICE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;

  snprintf(cmd->u.voice.text, sizeof(cmd->u.voice.text),
           "\xe5\xb7\xb2\xe8\xbf\x9e\xe7\xbb\xad\xe9\xaa\x91\xe8\xa1\x8c"
           "%lu\xe5\x88\x86\xe9\x92\x9f\xef\xbc\x8c\xe5\xbb\xba\xe8\xae\xae"
           "\xe5\x81\x9c\xe4\xb8\x8b\xe4\xbc\x91\xe6\x81\xaf\xe7\x89\x87\xe5\x88\xbb",
           (unsigned long)minutes);
  /* "已连续骑行X分钟，建议停下休息片刻" */

  cmd->u.voice.interrupt = false;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_fatigue =
{
  .id           = "fatigue",
  .name         = "Fatigue Riding Alert",
  .description  = "Suggest rest after prolonged riding",
  .trigger_type = TRIGGER_THRESHOLD,
  .action_type  = ACTION_VOICE,
  .priority     = SKILL_PRI_NORMAL,
  .evaluate     = fatigue_evaluate,
  .build_action = fatigue_build_action,
  .config       =
  {
    .threshold    = CONFIG_AI_SKILL_FATIGUE_THRESHOLD * 60, /* min -> sec */
    .threshold2   = 0.0f,
    .cooldown_sec = 600,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
