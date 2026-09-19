/****************************************************************************
 * ai_agent_main.c
 *
 * Entry point for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
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

#define AGENT_TASK_NAME    "qiban_ai_agent"
#define AGENT_STACK_SIZE   CONFIG_AI_AGENT_STACK_SIZE
#define AGENT_PRIORITY     100

/****************************************************************************
 * External References
 ****************************************************************************/

extern void qiban_skill_registry_init(void);
extern void qiban_agent_core_run(void);
extern void qiban_agent_core_set_running(bool running);
extern bool qiban_agent_core_is_running(void);

/* CLI entry point */

extern int qiban_ai_cli_main(int argc, FAR char *argv[]);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_agent_task_id = -1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: agent_task
 *
 * Description:
 *   Main agent task entry point.
 *
 ****************************************************************************/

static int agent_task(int argc, FAR char *argv[])
{
  qiban_agent_core_run();
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_ai_agent_init
 ****************************************************************************/

int qiban_ai_agent_init(void)
{
  printf("ai_agent: initializing v%s\n", AI_AGENT_VERSION);

  /* Initialize subsystems */

  qiban_vehicle_state_init();
  qiban_action_executor_init();
  qiban_skill_registry_init();

  printf("ai_agent: init complete\n");
  return 0;
}

/****************************************************************************
 * Name: qiban_ai_agent_start
 ****************************************************************************/

int qiban_ai_agent_start(void)
{
  if (qiban_agent_core_is_running())
    {
      printf("ai_agent: already running\n");
      return -EALREADY;
    }

  /* Initialize if not already done */

  qiban_ai_agent_init();

  /* Set running flag */

  qiban_agent_core_set_running(true);

  /* Spawn agent task */

  g_agent_task_id = task_create(AGENT_TASK_NAME, AGENT_PRIORITY,
                                AGENT_STACK_SIZE, agent_task,
                                NULL);
  if (g_agent_task_id < 0)
    {
      printf("ai_agent: failed to create task (%d)\n", errno);
      qiban_agent_core_set_running(false);
      return -errno;
    }

  printf("ai_agent: started (pid=%d)\n", g_agent_task_id);
  return 0;
}

/****************************************************************************
 * Name: qiban_ai_agent_stop
 ****************************************************************************/

int qiban_ai_agent_stop(void)
{
  if (!qiban_agent_core_is_running())
    {
      printf("ai_agent: not running\n");
      return -EALREADY;
    }

  qiban_agent_core_set_running(false);
  printf("ai_agent: stopping...\n");
  return 0;
}

/****************************************************************************
 * Name: qiban_ai_agent_is_running
 ****************************************************************************/

bool qiban_ai_agent_is_running(void)
{
  return qiban_agent_core_is_running();
}

/****************************************************************************
 * Name: qiban_ai_main (NSH builtin entry)
 *
 * Description:
 *   Registered as the NSH builtin command "qiban_ai".
 *   Delegates to the CLI handler.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  return qiban_ai_cli_main(argc, argv);
}
