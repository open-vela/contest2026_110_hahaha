/****************************************************************************
 * agent_core.c
 *
 * Main polling loop and trigger evaluation engine for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ai_agent.h"
#include "skill.h"
#include "vehicle_state.h"
#include "action_cmd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define POLL_INTERVAL_MS   CONFIG_AI_AGENT_POLL_INTERVAL_MS
#define HISTORY_LEN        CONFIG_AI_AGENT_CLI_HISTORY_LEN

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qiban_trigger_history_s
{
  uint32_t timestamp;
  char     skill_id[SKILL_ID_MAXLEN];
  char     detail[64];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_agent_running;
static int  g_poll_count;

static struct qiban_trigger_history_s g_history[HISTORY_LEN];
static int g_history_head;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: record_trigger
 ****************************************************************************/

static void record_trigger(FAR const char *skill_id,
                           FAR const char *detail)
{
  struct qiban_trigger_history_s *entry;

  entry = &g_history[g_history_head];
  entry->timestamp = (uint32_t)time(NULL);
  strlcpy(entry->skill_id, skill_id, sizeof(entry->skill_id));
  strlcpy(entry->detail, detail, sizeof(entry->detail));

  g_history_head = (g_history_head + 1) % HISTORY_LEN;
}

/****************************************************************************
 * Name: check_cooldown
 *
 * Description:
 *   Check if a skill's cooldown has expired. If so, transition to IDLE.
 *
 ****************************************************************************/

static bool check_cooldown(FAR qiban_skill_t *skill)
{
  uint32_t now;
  uint32_t elapsed;

  if (skill->state != SKILL_STATE_COOLDOWN)
    {
      return true; /* Not in cooldown */
    }

  now = (uint32_t)time(NULL);
  elapsed = now - skill->last_fired_tick;

  if (elapsed >= skill->config.cooldown_sec)
    {
      skill->state = SKILL_STATE_IDLE;
      return true;
    }

  return false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_agent_core_run
 *
 * Description:
 *   Main agent loop. Runs until g_agent_running is set to false.
 *
 ****************************************************************************/

void qiban_agent_core_run(void)
{
  qiban_vehicle_state_t vs;
  qiban_action_cmd_t    cmd;
  qiban_skill_t        *skill;
  int i;

  printf("ai_agent: main loop started (poll=%dms)\n", POLL_INTERVAL_MS);

  while (g_agent_running)
    {
      /* 1. Update state from JSON files */

      qiban_vehicle_state_update();

      /* 2. Update analyzer (speed history, ride stats) */

      qiban_vehicle_state_get(&vs);
      qiban_state_analyzer_update(&vs);

      /* 3. Write derived fields back to global state */

      qiban_vehicle_state_set_derived(
        qiban_state_analyzer_is_moving(),
        qiban_state_analyzer_stationary_sec());

      /* 4. Re-read state with derived fields */

      qiban_vehicle_state_get(&vs);

      /* 4. Evaluate each skill */

      for (i = 0; i < g_qiban_skill_count; i++)
        {
          skill = &g_qiban_skills[i];

          /* Skip disabled skills */

          if (!skill->config.enabled ||
              skill->state == SKILL_STATE_DISABLED)
            {
              continue;
            }

          /* Check cooldown expiry */

          if (!check_cooldown(skill))
            {
              continue;
            }

          /* Evaluate trigger */

          if (skill->evaluate != NULL &&
              skill->evaluate(skill, &vs))
            {
              /* Trigger fired */

              skill->state = SKILL_STATE_FIRED;
              skill->last_fired_tick = (uint32_t)time(NULL);
              skill->fire_count++;

              /* Build and dispatch action */

              if (skill->build_action != NULL)
                {
                  memset(&cmd, 0, sizeof(cmd));
                  skill->build_action(skill, &vs, &cmd);
                  qiban_action_executor_dispatch(&cmd);
                }

              /* Record in history */

              record_trigger(skill->id, skill->name);

              /* Enter cooldown */

              skill->state = SKILL_STATE_COOLDOWN;
            }
        }

      g_poll_count++;

      /* 5. Sleep until next poll */

      usleep(POLL_INTERVAL_MS * 1000);
    }

  printf("ai_agent: main loop stopped\n");
}

/****************************************************************************
 * Name: qiban_agent_core_stop
 ****************************************************************************/

void qiban_agent_core_stop(void)
{
  g_agent_running = false;
}

/****************************************************************************
 * Name: qiban_agent_core_is_running
 ****************************************************************************/

bool qiban_agent_core_is_running(void)
{
  return g_agent_running;
}

/****************************************************************************
 * Name: qiban_agent_core_set_running
 ****************************************************************************/

void qiban_agent_core_set_running(bool running)
{
  g_agent_running = running;
}

/****************************************************************************
 * Name: qiban_agent_core_get_poll_count
 ****************************************************************************/

int qiban_agent_core_get_poll_count(void)
{
  return g_poll_count;
}

/****************************************************************************
 * Name: qiban_agent_core_get_history
 ****************************************************************************/

int qiban_agent_core_get_history(
    FAR struct qiban_trigger_history_s *out, int max_entries)
{
  int count = 0;
  int idx;
  int i;

  /* Count valid entries */

  for (i = 0; i < HISTORY_LEN; i++)
    {
      if (g_history[i].timestamp > 0)
        {
          count++;
        }
    }

  if (count > max_entries)
    {
      count = max_entries;
    }

  /* Copy most recent entries (newest first) */

  idx = (g_history_head - 1 + HISTORY_LEN) % HISTORY_LEN;
  for (i = 0; i < count; i++)
    {
      memcpy(&out[i], &g_history[idx], sizeof(g_history[0]));
      idx = (idx - 1 + HISTORY_LEN) % HISTORY_LEN;
    }

  return count;
}
