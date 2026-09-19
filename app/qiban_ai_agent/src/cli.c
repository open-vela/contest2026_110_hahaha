/****************************************************************************
 * cli.c
 *
 * NSH CLI interface for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_agent.h"
#include "skill.h"
#include "vehicle_state.h"
#include "action_cmd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HISTORY_DISPLAY_LEN  20

/****************************************************************************
 * External References
 ****************************************************************************/

extern void qiban_agent_core_set_running(bool running);
extern bool qiban_agent_core_is_running(void);
extern int  qiban_agent_core_get_poll_count(void);
extern int  qiban_agent_core_get_history(void *out, int max);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cmd_status
 ****************************************************************************/

static int cmd_status(void)
{
  qiban_speed_history_t *hist = qiban_speed_history_get();
  qiban_ride_stats_t    *stats = qiban_ride_stats_get();

  printf("=== Qiban AI Agent Status ===\n");
  printf("Version:    %s\n", AI_AGENT_VERSION);
  printf("Running:    %s\n",
         qiban_agent_core_is_running() ? "yes" : "no");
  printf("Poll count: %d\n", qiban_agent_core_get_poll_count());
  printf("Skills:     %d registered\n", g_qiban_skill_count);
  printf("Avg speed:  %.1f km/h (30s)\n", hist->avg_speed);
  printf("Max speed:  %.1f km/h\n", hist->max_speed);
  printf("Ride dist:  %.2f km\n", stats->total_distance_km);
  printf("Ride time:  %lu sec\n",
         (unsigned long)stats->total_ride_sec);
  printf("Stops:      %lu\n", (unsigned long)stats->stop_count);
  return 0;
}

/****************************************************************************
 * Name: cmd_skill_list
 ****************************************************************************/

static int cmd_skill_list(void)
{
  qiban_skill_t *s;
  int i;

  printf("%-18s %-8s %-6s %-8s %s\n",
         "ID", "STATE", "PRI", "FIRES", "NAME");
  printf("---------------------------------------------------\n");

  for (i = 0; i < g_qiban_skill_count; i++)
    {
      s = &g_qiban_skills[i];
      printf("%-18s %-8s %-6s %-8lu %s\n",
             s->id,
             qiban_skill_state_str(s->state),
             qiban_skill_priority_str(s->priority),
             (unsigned long)s->fire_count,
             s->name);
    }

  return 0;
}

/****************************************************************************
 * Name: cmd_skill_info
 ****************************************************************************/

static int cmd_skill_info(FAR const char *id)
{
  qiban_skill_t *s = qiban_skill_find(id);

  if (s == NULL)
    {
      printf("skill '%s' not found\n", id);
      return -1;
    }

  printf("=== Skill: %s ===\n", s->id);
  printf("Name:        %s\n", s->name);
  printf("Description: %s\n", s->description);
  printf("State:       %s\n", qiban_skill_state_str(s->state));
  printf("Priority:    %s\n", qiban_skill_priority_str(s->priority));
  printf("Enabled:     %s\n", s->config.enabled ? "yes" : "no");
  printf("Threshold:   %.2f\n", s->config.threshold);
  printf("Threshold2:  %.2f\n", s->config.threshold2);
  printf("Cooldown:    %lu sec\n",
         (unsigned long)s->config.cooldown_sec);
  printf("Fire count:  %lu\n", (unsigned long)s->fire_count);
  printf("Voice text:  %s\n", s->config.voice_text);
  return 0;
}

/****************************************************************************
 * Name: cmd_skill_enable_disable
 ****************************************************************************/

static int cmd_skill_set_enabled(FAR const char *id, bool enabled)
{
  qiban_skill_t *s = qiban_skill_find(id);

  if (s == NULL)
    {
      printf("skill '%s' not found\n", id);
      return -1;
    }

  s->config.enabled = enabled;
  s->state = enabled ? SKILL_STATE_IDLE : SKILL_STATE_DISABLED;
  printf("skill '%s' %s\n", id, enabled ? "enabled" : "disabled");
  return 0;
}

/****************************************************************************
 * Name: cmd_skill_fire
 ****************************************************************************/

static int cmd_skill_fire(FAR const char *id)
{
  qiban_skill_t *s = qiban_skill_find(id);

  if (s == NULL)
    {
      printf("skill '%s' not found\n", id);
      return -1;
    }

  if (s->build_action == NULL)
    {
      printf("skill '%s' has no action builder\n", id);
      return -1;
    }

  qiban_vehicle_state_t vs;
  qiban_action_cmd_t    cmd;

  qiban_vehicle_state_get(&vs);
  memset(&cmd, 0, sizeof(cmd));
  s->build_action(s, &vs, &cmd);
  qiban_action_executor_dispatch(&cmd);

  printf("skill '%s' manually fired\n", id);
  return 0;
}

/****************************************************************************
 * Name: cmd_skill_set
 ****************************************************************************/

static int cmd_skill_set(FAR const char *id, FAR const char *param,
                         FAR const char *value)
{
  qiban_skill_t *s = qiban_skill_find(id);

  if (s == NULL)
    {
      printf("skill '%s' not found\n", id);
      return -1;
    }

  if (strcmp(param, "threshold") == 0)
    {
      s->config.threshold = (float)atof(value);
      printf("skill '%s' threshold = %.2f\n", id, s->config.threshold);
    }
  else if (strcmp(param, "threshold2") == 0)
    {
      s->config.threshold2 = (float)atof(value);
      printf("skill '%s' threshold2 = %.2f\n", id, s->config.threshold2);
    }
  else if (strcmp(param, "cooldown") == 0)
    {
      s->config.cooldown_sec = (uint32_t)atoi(value);
      printf("skill '%s' cooldown = %lu sec\n", id,
             (unsigned long)s->config.cooldown_sec);
    }
  else
    {
      printf("unknown parameter '%s'\n", param);
      return -1;
    }

  return 0;
}

/****************************************************************************
 * Name: cmd_state
 ****************************************************************************/

static int cmd_state(void)
{
  qiban_vehicle_state_t vs;
  qiban_speed_history_t *hist = qiban_speed_history_get();

  qiban_vehicle_state_get(&vs);

  printf("=== Vehicle State ===\n");
  printf("Speed:       %.1f km/h\n", vs.speed_kmh);
  printf("Battery:     %.0f%%\n", vs.battery_pct);
  printf("Mileage:     %.1f km\n", vs.mileage_km);
  printf("Ride time:   %lu sec\n",
         (unsigned long)vs.ride_duration_sec);
  printf("Moving:      %s\n", vs.is_moving ? "yes" : "no");
  printf("Speed avg:   %.1f km/h (30s)\n", hist->avg_speed);
  printf("Speed trend: %.2f km/h/s\n", hist->accel_trend);
  printf("\n=== Navigation ===\n");
  printf("Active:      %s\n", vs.nav_active ? "yes" : "no");
  printf("Destination: %s\n", vs.nav_destination);
  printf("Status:      %s\n", vs.nav_status);
  printf("Remaining:   %.2f km\n", vs.nav_remaining_km);
  printf("Next turn:   %s (%dm)\n", vs.nav_next_turn,
         vs.nav_turn_dist_m);
  printf("\n=== Location ===\n");
  printf("GPS valid:   %s\n", vs.gps_valid ? "yes" : "no");
  printf("Lat/Lon:     %.6f, %.6f\n", vs.latitude, vs.longitude);
  printf("\n=== Alerts ===\n");
  printf("Overspeed:   %s\n", vs.alert_overspeed ? "YES" : "no");
  printf("Low battery: %s\n", vs.alert_low_battery ? "YES" : "no");
  printf("Fatigue:     %s\n", vs.alert_fatigue ? "YES" : "no");
  return 0;
}

/****************************************************************************
 * Name: cmd_history
 ****************************************************************************/

static int cmd_history(void)
{
  struct
  {
    uint32_t timestamp;
    char     skill_id[SKILL_ID_MAXLEN];
    char     detail[64];
  } entries[HISTORY_DISPLAY_LEN];

  int count;
  int i;

  count = qiban_agent_core_get_history(entries, HISTORY_DISPLAY_LEN);

  printf("=== Trigger History (last %d) ===\n", count);
  printf("%-12s %-18s %s\n", "TIME", "SKILL", "DETAIL");
  printf("----------------------------------------\n");

  for (i = 0; i < count; i++)
    {
      printf("%-12lu %-18s %s\n",
             (unsigned long)entries[i].timestamp,
             entries[i].skill_id,
             entries[i].detail);
    }

  return 0;
}

/****************************************************************************
 * Name: cmd_help
 ****************************************************************************/

static void cmd_help(void)
{
  printf("Usage: qiban_ai <command>\n");
  printf("Commands:\n");
  printf("  (no args)           Start the AI agent task\n");
  printf("  status              Show agent status\n");
  printf("  skill list          List all skills\n");
  printf("  skill info <id>     Show skill details\n");
  printf("  skill enable <id>   Enable a skill\n");
  printf("  skill disable <id>  Disable a skill\n");
  printf("  skill fire <id>     Manually trigger a skill\n");
  printf("  skill set <id> <param> <value>  Set skill config\n");
  printf("  state               Show current vehicle state\n");
  printf("  history             Show trigger history\n");
  printf("  help                Show this help\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_ai_cli_main
 *
 * Description:
 *   NSH builtin command entry point.
 *
 ****************************************************************************/

int qiban_ai_cli_main(int argc, FAR char *argv[])
{
  if (argc < 2)
    {
      /* No args — start the agent */

      return qiban_ai_agent_start();
    }

  if (strcmp(argv[1], "status") == 0)
    {
      return cmd_status();
    }
  else if (strcmp(argv[1], "skill") == 0)
    {
      if (argc < 3)
        {
          printf("Usage: qiban_ai skill <list|info|enable|disable|fire|set>\n");
          return -1;
        }

      if (strcmp(argv[2], "list") == 0)
        {
          return cmd_skill_list();
        }
      else if (strcmp(argv[2], "info") == 0 && argc >= 4)
        {
          return cmd_skill_info(argv[3]);
        }
      else if (strcmp(argv[2], "enable") == 0 && argc >= 4)
        {
          return cmd_skill_set_enabled(argv[3], true);
        }
      else if (strcmp(argv[2], "disable") == 0 && argc >= 4)
        {
          return cmd_skill_set_enabled(argv[3], false);
        }
      else if (strcmp(argv[2], "fire") == 0 && argc >= 4)
        {
          return cmd_skill_fire(argv[3]);
        }
      else if (strcmp(argv[2], "set") == 0 && argc >= 6)
        {
          return cmd_skill_set(argv[3], argv[4], argv[5]);
        }
      else
        {
          printf("Usage: qiban_ai skill <list|info|enable|disable|fire|set>\n");
          return -1;
        }
    }
  else if (strcmp(argv[1], "state") == 0)
    {
      return cmd_state();
    }
  else if (strcmp(argv[1], "history") == 0)
    {
      return cmd_history();
    }
  else if (strcmp(argv[1], "help") == 0)
    {
      cmd_help();
      return 0;
    }
  else
    {
      printf("Unknown command: %s\n", argv[1]);
      cmd_help();
      return -1;
    }

  return 0;
}
