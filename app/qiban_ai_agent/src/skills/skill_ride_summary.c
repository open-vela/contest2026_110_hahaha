/****************************************************************************
 * skill_ride_summary.c
 *
 * Ride summary generation skill for the Qiban AI Agent.
 * Triggers when the ride ends (stationary for 2+ minutes after moving).
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

#define STOP_THRESHOLD_SEC  120

/****************************************************************************
 * External References
 ****************************************************************************/

extern bool qiban_state_analyzer_is_moving(void);
extern uint32_t qiban_state_analyzer_stationary_sec(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool ride_summary_evaluate(qiban_skill_t *skill,
                                  const qiban_vehicle_state_t *vs)
{
  qiban_ride_stats_t *stats = qiban_ride_stats_get();

  if (stats->total_ride_sec < 300)
    {
      return false;
    }

  if (qiban_state_analyzer_is_moving())
    {
      return false;
    }

  return qiban_state_analyzer_stationary_sec() >=
         (uint32_t)skill->config.threshold;
}

static void ride_summary_build_action(qiban_skill_t *skill,
                                      const qiban_vehicle_state_t *vs,
                                      qiban_action_cmd_t *cmd)
{
  static qiban_action_cmd_t step_voice;
  static qiban_action_cmd_t step_ui;

  qiban_ride_stats_t *stats = qiban_ride_stats_get();
  float avg_speed;
  uint32_t minutes;

  memset(&step_voice, 0, sizeof(step_voice));
  memset(&step_ui, 0, sizeof(step_ui));

  minutes = stats->total_ride_sec / 60;
  if (minutes > 0)
    {
      avg_speed = stats->total_distance_km /
                  ((float)stats->total_ride_sec / 3600.0f);
    }
  else
    {
      avg_speed = 0.0f;
    }

  cmd->type     = ACTION_COMPOSITE;
  cmd->target   = SERVICE_VOICE;
  cmd->priority = skill->priority;
  cmd->u.composite.count = 2;

  /* Step 1: Voice summary */

  step_voice.type = ACTION_VOICE;
  snprintf(step_voice.u.voice.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe9\xaa\x91\xe8\xa1\x8c\xe7\xbb\x93\xe6\x9d\x9f\xe3\x80\x82"
           "\xe6\x80\xbb\xe9\x87\x8c\xe7\xa8\x8b%.1f\xe5\x85\xac\xe9\x87\x8c"
           "\xef\xbc\x8c\xe9\xaa\x91\xe8\xa1\x8c%lu\xe5\x88\x86\xe9\x92\x9f"
           "\xef\xbc\x8c\xe5\xb9\xb3\xe5\x9d\x87\xe9\x80\x9f\xe5\xba\xa6"
           "%.0f\xe5\x85\xac\xe9\x87\x8c\xe6\xaf\x8f\xe5\xb0\x8f\xe6\x97\xb6",
           stats->total_distance_km,
           (unsigned long)minutes,
           avg_speed);
  step_voice.u.voice.interrupt = false;
  cmd->u.composite.steps[0] = &step_voice;

  /* Step 2: Switch to dashboard */

  step_ui.type = ACTION_UI_SWITCH;
  step_ui.u.ui.page = UI_PAGE_DASHBOARD;
  step_ui.u.ui.timeout_sec = 0;
  cmd->u.composite.steps[1] = &step_ui;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_ride_summary =
{
  .id           = "ride_summary",
  .name         = "Ride Summary",
  .description  = "Generate ride summary when ride ends",
  .trigger_type = TRIGGER_EVENT,
  .action_type  = ACTION_COMPOSITE,
  .priority     = SKILL_PRI_INFO,
  .evaluate     = ride_summary_evaluate,
  .build_action = ride_summary_build_action,
  .config       =
  {
    .threshold    = STOP_THRESHOLD_SEC,
    .threshold2   = 0.0f,
    .cooldown_sec = 900,
    .window_sec   = 0,
    .voice_text   = "",
    .enabled      = true,
  },
  .state           = SKILL_STATE_IDLE,
  .last_fired_tick = 0,
  .fire_count      = 0,
};
