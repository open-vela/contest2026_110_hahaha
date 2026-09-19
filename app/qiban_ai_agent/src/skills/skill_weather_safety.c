/****************************************************************************
 * skill_weather_safety.c
 *
 * Weather-based safety reminder skill for the Qiban AI Agent.
 * Triggers when weather conditions indicate rain, snow, or strong wind.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "skill.h"
#include "vehicle_state.h"
#include "action_cmd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WEATHER_JSON_PATH    "/data/qiban_weather.json"
#define WEATHER_BUF_SIZE     512
#define WIND_THRESHOLD_KMH   30.0f

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qiban_weather_s
{
  char  condition[32];
  float wind_speed_kmh;
  float temperature_c;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int weather_state_read(FAR struct qiban_weather_s *wx)
{
  char buf[WEATHER_BUF_SIZE];
  FAR char *pos;
  int fd;
  ssize_t nread;

  fd = open(WEATHER_JSON_PATH, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  nread = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (nread <= 0)
    {
      return -EIO;
    }

  buf[nread] = '\0';

  wx->condition[0] = '\0';
  wx->wind_speed_kmh = 0.0f;
  wx->temperature_c = 0.0f;

  pos = strstr(buf, "\"condition\"");
  if (pos != NULL)
    {
      pos = strchr(pos, ':');
      if (pos != NULL)
        {
          pos++;
          while (*pos == ' ')
            pos++;
          if (*pos == '"')
            {
              pos++;
              FAR char *end = strchr(pos, '"');
              if (end != NULL)
                {
                  size_t len = end - pos;
                  if (len >= sizeof(wx->condition))
                    {
                      len = sizeof(wx->condition) - 1;
                    }

                  memcpy(wx->condition, pos, len);
                  wx->condition[len] = '\0';
                }
            }
        }
    }

  pos = strstr(buf, "\"wind_speed_kmh\"");
  if (pos != NULL)
    {
      pos = strchr(pos, ':');
      if (pos != NULL)
        {
          wx->wind_speed_kmh = (float)atof(pos + 1);
        }
    }

  return 0;
}

static bool weather_safety_evaluate(qiban_skill_t *skill,
                                    const qiban_vehicle_state_t *vs)
{
  struct qiban_weather_s wx;

  if (weather_state_read(&wx) < 0)
    {
      return false;
    }

  if (strstr(wx.condition, "rain") != NULL ||
      strstr(wx.condition, "snow") != NULL)
    {
      return true;
    }

  if (wx.wind_speed_kmh > skill->config.threshold)
    {
      return true;
    }

  return false;
}

static void weather_safety_build_action(qiban_skill_t *skill,
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
           "\xe5\xa4\xa9\xe6\xb0\x94\xe6\x81\xb6\xe5\x8a\xa3"
           "\xef\xbc\x8c\xe8\xaf\xb7\xe6\xb3\xa8\xe6\x84\x8f\xe9\xaa\x91"
           "\xe8\xa1\x8c\xe5\xae\x89\xe5\x85\xa8");
  step_voice.u.voice.interrupt = false;
  cmd->u.composite.steps[0] = &step_voice;

  /* Step 2: UI alert */

  step_alert.type = ACTION_ALERT;
  step_alert.u.alert.severity = ALERT_WARNING;
  snprintf(step_alert.u.alert.text,
           ACTION_VOICE_TEXT_MAXLEN,
           "\xe6\x81\xb6\xe5\x8a\xa3\xe5\xa4\xa9\xe6\xb0\x94"
           "\xe6\x8f\x90\xe9\x86\x92");
  step_alert.u.alert.duration_ms = 8000;
  cmd->u.composite.steps[1] = &step_alert;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

qiban_skill_t g_skill_weather_safety =
{
  .id           = "weather_safety",
  .name         = "Weather Safety",
  .description  = "Safety reminder in bad weather",
  .trigger_type = TRIGGER_POLL,
  .action_type  = ACTION_COMPOSITE,
  .priority     = SKILL_PRI_NORMAL,
  .evaluate     = weather_safety_evaluate,
  .build_action = weather_safety_build_action,
  .config       =
  {
    .threshold    = WIND_THRESHOLD_KMH,
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
