/****************************************************************************
 * action_executor.c
 *
 * Action command dispatcher for the Qiban AI Agent.
 * Writes command files to /data/ for consumption by existing services.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "action_cmd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VOICE_CMD_PATH  "/data/qiban_voice_cmd.json"
#define UI_CMD_PATH     "/data/qiban_ui_cmd.json"
#define NAV_CMD_PATH    "/data/qiban_nav_cmd.json"
#define ALERT_CMD_PATH  "/data/qiban_alert_cmd.json"
#define CMD_BUF_SIZE    512

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: write_cmd_file
 *
 * Description:
 *   Atomically write a JSON command file.
 *
 ****************************************************************************/

static int write_cmd_file(FAR const char *path, FAR const char *json)
{
  char tmppath[128];
  int fd;
  ssize_t nwritten;
  size_t len;

  snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);

  fd = open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      return -errno;
    }

  len = strlen(json);
  nwritten = write(fd, json, len);
  close(fd);

  if (nwritten != (ssize_t)len)
    {
      unlink(tmppath);
      return -EIO;
    }

  /* Atomic rename */

  if (rename(tmppath, path) < 0)
    {
      unlink(tmppath);
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Name: dispatch_voice
 ****************************************************************************/

static int dispatch_voice(FAR const qiban_action_cmd_t *cmd)
{
  char buf[CMD_BUF_SIZE];
  uint32_t ts = (uint32_t)time(NULL);

  snprintf(buf, sizeof(buf),
    "{\"text\":\"%s\",\"interrupt\":%s,"
    "\"timestamp\":%lu,\"source\":\"ai_agent\"}",
    cmd->u.voice.text,
    cmd->u.voice.interrupt ? "true" : "false",
    (unsigned long)ts);

  return write_cmd_file(VOICE_CMD_PATH, buf);
}

/****************************************************************************
 * Name: dispatch_ui_switch
 ****************************************************************************/

static int dispatch_ui_switch(FAR const qiban_action_cmd_t *cmd)
{
  char buf[CMD_BUF_SIZE];
  uint32_t ts = (uint32_t)time(NULL);

  snprintf(buf, sizeof(buf),
    "{\"action\":\"switch_page\",\"page\":%d,"
    "\"timeout_sec\":%lu,\"timestamp\":%lu,"
    "\"source\":\"ai_agent\"}",
    cmd->u.ui.page,
    (unsigned long)cmd->u.ui.timeout_sec,
    (unsigned long)ts);

  return write_cmd_file(UI_CMD_PATH, buf);
}

/****************************************************************************
 * Name: dispatch_nav_cmd
 ****************************************************************************/

static int dispatch_nav_cmd(FAR const qiban_action_cmd_t *cmd)
{
  char buf[CMD_BUF_SIZE];
  uint32_t ts = (uint32_t)time(NULL);

  snprintf(buf, sizeof(buf),
    "{\"command\":\"%s\",\"param\":\"%s\","
    "\"timestamp\":%lu,\"source\":\"ai_agent\"}",
    cmd->u.nav.command,
    cmd->u.nav.param,
    (unsigned long)ts);

  return write_cmd_file(NAV_CMD_PATH, buf);
}

/****************************************************************************
 * Name: dispatch_alert
 ****************************************************************************/

static int dispatch_alert(FAR const qiban_action_cmd_t *cmd)
{
  char buf[CMD_BUF_SIZE];
  FAR const char *severity;
  uint32_t ts = (uint32_t)time(NULL);

  switch (cmd->u.alert.severity)
    {
      case ALERT_INFO:
        severity = "info";
        break;
      case ALERT_WARNING:
        severity = "warning";
        break;
      case ALERT_CRITICAL:
        severity = "critical";
        break;
      default:
        severity = "info";
        break;
    }

  snprintf(buf, sizeof(buf),
    "{\"text\":\"%s\",\"severity\":\"%s\","
    "\"duration_ms\":%lu,\"timestamp\":%lu,"
    "\"source\":\"ai_agent\"}",
    cmd->u.alert.text,
    severity,
    (unsigned long)cmd->u.alert.duration_ms,
    (unsigned long)ts);

  return write_cmd_file(ALERT_CMD_PATH, buf);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_action_executor_init
 ****************************************************************************/

void qiban_action_executor_init(void)
{
  /* Nothing to initialize — file-based IPC is stateless */
}

/****************************************************************************
 * Name: qiban_action_executor_dispatch
 ****************************************************************************/

int qiban_action_executor_dispatch(FAR const qiban_action_cmd_t *cmd)
{
  int ret = 0;
  int i;

  if (cmd == NULL)
    {
      return -EINVAL;
    }

  switch (cmd->type)
    {
      case ACTION_VOICE:
        ret = dispatch_voice(cmd);
        break;

      case ACTION_UI_SWITCH:
        ret = dispatch_ui_switch(cmd);
        break;

      case ACTION_NAV_CMD:
        ret = dispatch_nav_cmd(cmd);
        break;

      case ACTION_ALERT:
        ret = dispatch_alert(cmd);
        break;

      case ACTION_COMPOSITE:
        for (i = 0; i < cmd->u.composite.count; i++)
          {
            if (cmd->u.composite.steps[i] != NULL)
              {
                ret = qiban_action_executor_dispatch(
                    cmd->u.composite.steps[i]);
                if (ret < 0)
                  {
                    break;
                  }
              }
          }
        break;

      default:
        ret = -EINVAL;
        break;
    }

  return ret;
}
