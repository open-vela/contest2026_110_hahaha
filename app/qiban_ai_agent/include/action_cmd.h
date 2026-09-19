/****************************************************************************
 * action_cmd.h
 *
 * Action command definitions for the Qiban AI Agent.
 ****************************************************************************/

#ifndef __QIBAN_ACTION_CMD_H
#define __QIBAN_ACTION_CMD_H

#include <nuttx/config.h>
#include <stdint.h>

#include "skill.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ACTION_VOICE_TEXT_MAXLEN  96
#define ACTION_NAV_CMD_MAXLEN     64
#define ACTION_NAV_PARAM_MAXLEN   128
#define ACTION_COMPOSITE_MAX      4

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Forward declaration for self-referential composite */

struct qiban_action_cmd_s;
typedef struct qiban_action_cmd_s qiban_action_cmd_t;

/* Service targets */

enum qiban_service_target_e
{
  SERVICE_VOICE = 0,
  SERVICE_UI,
  SERVICE_NAV,
  SERVICE_ALERT
};

/* UI page IDs (matching qiban_ui pages) */

enum qiban_ui_page_e
{
  UI_PAGE_DASHBOARD = 0,
  UI_PAGE_MAP       = 1,
  UI_PAGE_VOICE     = 2,
  UI_PAGE_WEATHER   = 3
};

/* Alert severity */

enum qiban_alert_severity_e
{
  ALERT_INFO = 0,
  ALERT_WARNING,
  ALERT_CRITICAL
};

/* Action command */

struct qiban_action_cmd_s
{
  enum qiban_action_type_e    type;
  enum qiban_service_target_e target;
  enum qiban_skill_priority_e priority;

  union
  {
    struct /* ACTION_VOICE */
    {
      char  text[ACTION_VOICE_TEXT_MAXLEN];
      bool  interrupt;
    } voice;

    struct /* ACTION_UI_SWITCH */
    {
      enum qiban_ui_page_e page;
      uint32_t             timeout_sec;
    } ui;

    struct /* ACTION_NAV_CMD */
    {
      char command[ACTION_NAV_CMD_MAXLEN];
      char param[ACTION_NAV_PARAM_MAXLEN];
    } nav;

    struct /* ACTION_ALERT */
    {
      char                      text[ACTION_VOICE_TEXT_MAXLEN];
      enum qiban_alert_severity_e severity;
      uint32_t                  duration_ms;
    } alert;

    struct /* ACTION_COMPOSITE */
    {
      qiban_action_cmd_t *steps[ACTION_COMPOSITE_MAX];
      int                 count;
    } composite;
  } u;
};

typedef struct qiban_action_cmd_s qiban_action_cmd_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_action_executor_init
 *
 * Description:
 *   Initialize the action executor.
 *
 ****************************************************************************/

void qiban_action_executor_init(void);

/****************************************************************************
 * Name: qiban_action_executor_dispatch
 *
 * Description:
 *   Dispatch an action command to the appropriate service.
 *
 ****************************************************************************/

int qiban_action_executor_dispatch(FAR const qiban_action_cmd_t *cmd);

#endif /* __QIBAN_ACTION_CMD_H */
