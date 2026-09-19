/****************************************************************************
 * state_analyzer.c
 *
 * Speed trend analysis and ride statistics for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <time.h>

#include "vehicle_state.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static qiban_speed_history_t g_speed_history;
static qiban_ride_stats_t    g_ride_stats;
static bool                  g_was_moving;
static float                 g_last_speed;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_speed_history_get
 ****************************************************************************/

FAR qiban_speed_history_t *qiban_speed_history_get(void)
{
  return &g_speed_history;
}

/****************************************************************************
 * Name: qiban_ride_stats_get
 ****************************************************************************/

FAR qiban_ride_stats_t *qiban_ride_stats_get(void)
{
  return &g_ride_stats;
}

/****************************************************************************
 * Name: qiban_speed_history_at
 *
 * Description:
 *   Get speed value at N seconds ago (0 = most recent).
 *   Returns 0.0 if not enough history.
 *
 ****************************************************************************/

float qiban_speed_history_at(int seconds_ago)
{
  qiban_speed_history_t *h = &g_speed_history;

  if (seconds_ago < 0 || seconds_ago >= h->count)
    {
      return 0.0f;
    }

  int idx = (h->head - 1 - seconds_ago + QIBAN_SPEED_HISTORY_LEN)
            % QIBAN_SPEED_HISTORY_LEN;
  return h->buf[idx];
}

/****************************************************************************
 * Name: qiban_state_analyzer_update
 *
 * Description:
 *   Update speed history and ride statistics based on current state.
 *   Called once per poll cycle from the agent core.
 *
 ****************************************************************************/

void qiban_state_analyzer_update(FAR const qiban_vehicle_state_t *vs)
{
  qiban_speed_history_t *h = &g_speed_history;
  qiban_ride_stats_t    *r = &g_ride_stats;
  float sum;
  int i;

  /* Update speed history circular buffer */

  h->buf[h->head] = vs->speed_kmh;
  h->head = (h->head + 1) % QIBAN_SPEED_HISTORY_LEN;
  if (h->count < QIBAN_SPEED_HISTORY_LEN)
    {
      h->count++;
    }

  /* Compute average and max */

  sum = 0.0f;
  h->max_speed = 0.0f;
  for (i = 0; i < h->count; i++)
    {
      sum += h->buf[i];
      if (h->buf[i] > h->max_speed)
        {
          h->max_speed = h->buf[i];
        }
    }

  h->avg_speed = sum / h->count;

  /* Compute acceleration trend (delta speed over last 3 samples) */

  if (h->count >= 3)
    {
      float speed_now  = qiban_speed_history_at(0);
      float speed_prev = qiban_speed_history_at(2);
      h->accel_trend = (speed_now - speed_prev) / 2.0f;
    }

  /* Update moving state */

  if (vs->speed_kmh > 1.0f)
    {
      if (!g_was_moving)
        {
          /* Transition: stopped -> moving */

          r->ride_start_tick = (uint32_t)time(NULL);
        }

      r->last_moving_tick = (uint32_t)time(NULL);
      g_was_moving = true;
    }
  else
    {
      if (g_was_moving)
        {
          /* Transition: moving -> stopped */

          r->stop_count++;
        }

      g_was_moving = false;
    }

  /* Update ride stats */

  if (g_was_moving)
    {
      r->total_ride_sec = (uint32_t)time(NULL) - r->ride_start_tick;
    }

  if (vs->speed_kmh > r->max_speed_kmh)
    {
      r->max_speed_kmh = vs->speed_kmh;
    }

  /* Estimate distance from speed (km/h -> km per second) */

  r->total_distance_km += vs->speed_kmh / 3600.0f;
  g_last_speed = vs->speed_kmh;
}

/****************************************************************************
 * Name: qiban_state_analyzer_is_moving
 ****************************************************************************/

bool qiban_state_analyzer_is_moving(void)
{
  return g_was_moving;
}

/****************************************************************************
 * Name: qiban_state_analyzer_stationary_sec
 ****************************************************************************/

uint32_t qiban_state_analyzer_stationary_sec(void)
{
  if (g_was_moving)
    {
      return 0;
    }

  return (uint32_t)time(NULL) - g_ride_stats.last_moving_tick;
}
