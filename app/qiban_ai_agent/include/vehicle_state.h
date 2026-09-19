/****************************************************************************
 * vehicle_state.h
 *
 * Vehicle/nav/location state data structures for the Qiban AI Agent.
 ****************************************************************************/

#ifndef __QIBAN_VEHICLE_STATE_H
#define __QIBAN_VEHICLE_STATE_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QIBAN_SPEED_HISTORY_LEN  30  /* 30 seconds of history */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Main vehicle state — aggregated from multiple JSON files */

struct qiban_vehicle_state_s
{
  /* Motion (from /data/qiban_vehicle_state.json) */

  float    speed_kmh;
  float    battery_pct;
  float    mileage_km;
  uint32_t ride_duration_sec;
  bool     alert_overspeed;
  bool     alert_low_battery;
  bool     alert_fatigue;

  /* Navigation (from /data/qiban_nav_state.json) */

  bool     nav_active;
  float    nav_remaining_km;
  char     nav_next_turn[64];
  int      nav_turn_dist_m;
  char     nav_status[32];
  char     nav_destination[64];

  /* Location (from /data/qiban_location_state.json) */

  double   latitude;
  double   longitude;
  bool     gps_valid;

  /* Derived (computed by state_analyzer) */

  float    accel_ms2;
  float    avg_speed_30s;
  float    speed_trend;       /* +accelerating, -decelerating */
  bool     is_moving;
  uint32_t stationary_sec;
};

typedef struct qiban_vehicle_state_s qiban_vehicle_state_t;

/* Speed history for trend analysis */

struct qiban_speed_history_s
{
  float    buf[QIBAN_SPEED_HISTORY_LEN];
  int      head;
  int      count;
  float    avg_speed;
  float    max_speed;
  float    accel_trend;
};

typedef struct qiban_speed_history_s qiban_speed_history_t;

/* Ride statistics */

struct qiban_ride_stats_s
{
  uint32_t ride_start_tick;
  uint32_t total_ride_sec;
  uint32_t last_moving_tick;
  float    total_distance_km;
  uint32_t stop_count;
  float    max_speed_kmh;
};

typedef struct qiban_ride_stats_s qiban_ride_stats_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_vehicle_state_init
 *
 * Description:
 *   Initialize the vehicle state subsystem.
 *
 ****************************************************************************/

void qiban_vehicle_state_init(void);

/****************************************************************************
 * Name: qiban_vehicle_state_get
 *
 * Description:
 *   Get a snapshot of current vehicle state.
 *
 ****************************************************************************/

void qiban_vehicle_state_get(FAR qiban_vehicle_state_t *out);

/****************************************************************************
 * Name: qiban_vehicle_state_set_derived
 *
 * Description:
 *   Write derived fields (is_moving, stationary_sec) back to global state.
 *
 ****************************************************************************/

void qiban_vehicle_state_set_derived(bool is_moving,
                                     uint32_t stationary_sec);

/****************************************************************************
 * Name: qiban_vehicle_state_update
 *
 * Description:
 *   Update state from JSON files. Returns number of files updated.
 *
 ****************************************************************************/

int qiban_vehicle_state_update(void);

/****************************************************************************
 * Name: qiban_speed_history_get
 *
 * Description:
 *   Get pointer to speed history data.
 *
 ****************************************************************************/

FAR qiban_speed_history_t *qiban_speed_history_get(void);

/****************************************************************************
 * Name: qiban_ride_stats_get
 *
 * Description:
 *   Get pointer to ride statistics.
 *
 ****************************************************************************/

FAR qiban_ride_stats_t *qiban_ride_stats_get(void);

/****************************************************************************
 * Name: qiban_speed_history_at
 *
 * Description:
 *   Get speed value at N seconds ago (0 = most recent).
 *
 ****************************************************************************/

float qiban_speed_history_at(int seconds_ago);

/****************************************************************************
 * Name: qiban_state_analyzer_update
 *
 * Description:
 *   Update speed history and ride statistics. Called each poll cycle.
 *
 ****************************************************************************/

void qiban_state_analyzer_update(FAR const qiban_vehicle_state_t *vs);

/****************************************************************************
 * Name: qiban_state_analyzer_is_moving
 *
 * Description:
 *   Check if vehicle is currently moving.
 *
 ****************************************************************************/

bool qiban_state_analyzer_is_moving(void);

/****************************************************************************
 * Name: qiban_state_analyzer_stationary_sec
 *
 * Description:
 *   Get how long the vehicle has been stationary.
 *
 ****************************************************************************/

uint32_t qiban_state_analyzer_stationary_sec(void);

#endif /* __QIBAN_VEHICLE_STATE_H */
