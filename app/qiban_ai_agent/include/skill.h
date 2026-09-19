/****************************************************************************
 * skill.h
 *
 * Skill framework definitions for the Qiban AI Agent.
 ****************************************************************************/

#ifndef __QIBAN_SKILL_H
#define __QIBAN_SKILL_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SKILL_ID_MAXLEN     20
#define SKILL_NAME_MAXLEN   32
#define SKILL_DESC_MAXLEN   64
#define SKILL_VOICE_MAXLEN  96

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Trigger types */

enum qiban_trigger_type_e
{
  TRIGGER_POLL = 0,       /* Evaluated every poll cycle */
  TRIGGER_THRESHOLD,      /* Single threshold crossing */
  TRIGGER_TREND,          /* Pattern over time window */
  TRIGGER_EVENT           /* Specific state transition */
};

/* Action types */

enum qiban_action_type_e
{
  ACTION_NONE = 0,
  ACTION_VOICE,           /* TTS announcement via voice_service */
  ACTION_UI_SWITCH,       /* Switch LVGL page */
  ACTION_NAV_CMD,         /* Send command to nav_service */
  ACTION_ALERT,           /* Show alert overlay on UI */
  ACTION_COMPOSITE        /* Multiple actions in sequence */
};

/* Skill state machine */

enum qiban_skill_state_e
{
  SKILL_STATE_IDLE = 0,   /* Waiting for trigger */
  SKILL_STATE_FIRED,      /* Trigger matched, action executing */
  SKILL_STATE_COOLDOWN,   /* Post-fire cooldown period */
  SKILL_STATE_DISABLED    /* Explicitly disabled */
};

/* Skill priority (lower value = higher priority) */

enum qiban_skill_priority_e
{
  SKILL_PRI_EMERGENCY = 0,
  SKILL_PRI_HIGH      = 1,
  SKILL_PRI_NORMAL    = 2,
  SKILL_PRI_LOW       = 3,
  SKILL_PRI_INFO      = 4
};

/* Forward declarations */

struct qiban_vehicle_state_s;
struct qiban_action_cmd_s;
struct qiban_skill_s;

/* Trigger evaluation callback.
 * Returns true if the skill should fire.
 */

typedef bool (*qiban_skill_evaluate_fn)(
    struct qiban_skill_s *skill,
    const struct qiban_vehicle_state_s *vs);

/* Action builder callback.
 * Populates an action_cmd_t based on current state.
 */

typedef void (*qiban_skill_build_action_fn)(
    struct qiban_skill_s *skill,
    const struct qiban_vehicle_state_s *vs,
    struct qiban_action_cmd_s *cmd);

/* Skill configuration (runtime-adjustable via CLI) */

struct qiban_skill_config_s
{
  float    threshold;         /* Primary threshold value */
  float    threshold2;        /* Secondary threshold (optional) */
  uint32_t cooldown_sec;      /* Minimum seconds between fires */
  uint32_t window_sec;        /* Time window for trend analysis */
  char     voice_text[SKILL_VOICE_MAXLEN]; /* Customizable TTS text */
  bool     enabled;           /* Runtime enable/disable */
};

/* The skill descriptor — one per skill, statically allocated */

struct qiban_skill_s
{
  /* Identity */

  FAR const char *id;
  FAR const char *name;
  FAR const char *description;

  /* Classification */

  enum qiban_trigger_type_e   trigger_type;
  enum qiban_action_type_e    action_type;
  enum qiban_skill_priority_e priority;

  /* Callbacks */

  qiban_skill_evaluate_fn     evaluate;
  qiban_skill_build_action_fn build_action;

  /* Configuration (mutable) */

  struct qiban_skill_config_s config;

  /* Runtime state (managed by core) */

  enum qiban_skill_state_e    state;
  uint32_t                    last_fired_tick;
  uint32_t                    fire_count;
};

typedef struct qiban_skill_s   qiban_skill_t;
typedef struct qiban_skill_config_s qiban_skill_config_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Skill table — defined in skill_registry.c */

extern qiban_skill_t g_qiban_skills[];
extern int           g_qiban_skill_count;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_skill_register
 *
 * Description:
 *   Register a skill into the global skill table.
 *
 ****************************************************************************/

int qiban_skill_register(qiban_skill_t *skill);

/****************************************************************************
 * Name: qiban_skill_find
 *
 * Description:
 *   Find a skill by its ID string.
 *
 ****************************************************************************/

qiban_skill_t *qiban_skill_find(FAR const char *id);

/****************************************************************************
 * Name: qiban_skill_state_str
 *
 * Description:
 *   Return human-readable string for skill state.
 *
 ****************************************************************************/

FAR const char *qiban_skill_state_str(enum qiban_skill_state_e state);

/****************************************************************************
 * Name: qiban_skill_priority_str
 *
 * Description:
 *   Return human-readable string for skill priority.
 *
 ****************************************************************************/

FAR const char *qiban_skill_priority_str(enum qiban_skill_priority_e pri);

#endif /* __QIBAN_SKILL_H */
