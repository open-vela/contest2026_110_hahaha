/****************************************************************************
 * ai_agent.h
 *
 * Public API for the Qiban AI Agent.
 ****************************************************************************/

#ifndef __QIBAN_AI_AGENT_H
#define __QIBAN_AI_AGENT_H

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_AGENT_VERSION "1.0.0"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_ai_agent_init
 *
 * Description:
 *   Initialize the AI agent subsystems (state, skills, executor).
 *
 ****************************************************************************/

int qiban_ai_agent_init(void);

/****************************************************************************
 * Name: qiban_ai_agent_start
 *
 * Description:
 *   Start the AI agent main task.
 *
 ****************************************************************************/

int qiban_ai_agent_start(void);

/****************************************************************************
 * Name: qiban_ai_agent_stop
 *
 * Description:
 *   Stop the AI agent main task.
 *
 ****************************************************************************/

int qiban_ai_agent_stop(void);

/****************************************************************************
 * Name: qiban_ai_agent_is_running
 *
 * Description:
 *   Check if the agent task is running.
 *
 ****************************************************************************/

bool qiban_ai_agent_is_running(void);

#endif /* __QIBAN_AI_AGENT_H */
