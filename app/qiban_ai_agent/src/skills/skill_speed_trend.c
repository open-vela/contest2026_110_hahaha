/****************************************************************************
 * skill_speed_trend.c
 *
 * Speed trend analysis skill for the Qiban AI Agent.
 * Triggers on sustained deceleration pattern (potential road hazard).
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

#define DECEL_THRESHOLD   -0.5f  /* m/s^2 equivalent in km/h per sample */
#define TREND_WINDOW_SEC  5

/****************************************************************************
 * External References
 ****************************************************************************/

extern float qiban_speed_history_at(int seconds_ago);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool speed_trend_evaluate(qiban_skill_t *skill,
                                 const qiban_vehicle_state_t *vs)
{
  float speed_now;
  float speed_prev;
  float delta;
  int consecutive;
  int i;

  if (!vs->is_moving || vs->speed_kmh < 15.0f)
    {
      return false;
    }

  /* Check for sustained deceleration over the window */

  consecutive = 0;
  for (i = 0; i < (int)skill->config.window_sec - 1; i++)
    {
      speed_now  = qiban_speed_history_at(i);
      speed_prev = qiban_speed_history_at(i + 1);

      if (speed_prev > 0.0f && speed_now < speed_prev)
        {
          delta = speed_now - speed_prev;
          if (delta < skill->config.threshold)
            {
              consecutive++;
            }
        }
    }

  return consecutive >= 3; /* At least 3 consecutive drops */
}

static void speed_trend_build_action(qiban_skill_t *skill,
                                     const qiban_vehicle_state_t *vs,
                                     qiban_action_cmd_t *cmd)
{
  cmd->type     = ACTION_VOICE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;

  snprintf(cmd->u.voice.text, sizeof(cmd->u.voice.text),
           "\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe6\x8c\x81\xe7\xbb\xad"
           "\xe5\x87\x8f\xe9\x80\x9f\xef\xbc\x8c\xe8\xaf\xb7\xe6\xb3\xa8\xe6\x84\x8f"
           "\xe8\xb7\xaf\xe9\x9d\xa2\xe7\x8a\xb6\xe5\x86\xb5");
  /* "检测到持续减速，请注意路面状况" */

  cmd->u.voice.interrupt = false;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_speed_trend =
{
  .id           = "speed_trend",
  .name         = "Speed Trend Analysis",
  .description  = "Detect sustained deceleration patterns",
  .trigger_type = TRIGGER_TREND,
  .action_type  = ACTION_VOICE,
  .priority     = SKILL_PRI_LOW,
  .evaluate     = speed_trend_evaluate,
  .build_action = speed_trend_build_action,
  .config       =
  {
    .threshold    = DECEL_THRESHOLD,
    .threshold2   = 0.0f,
    .cooldown_sec = 180,
    .window_sec   = TREND_WINDOW_SEC,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
