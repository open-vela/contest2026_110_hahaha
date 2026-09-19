/****************************************************************************
 * json_reader.c
 *
 * JSON file reader with mtime caching for the Qiban AI Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vehicle_state.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define JSON_READER_BUF_SIZE   2048
#define VEHICLE_STATE_PATH     "/data/qiban_vehicle_state.json"
#define NAV_STATE_PATH         "/data/qiban_nav_state.json"
#define LOCATION_STATE_PATH    "/data/qiban_location_state.json"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qiban_json_source_s
{
  FAR const char *path;
  time_t          last_mtime;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct qiban_json_source_s g_sources[] =
{
  { VEHICLE_STATE_PATH,  0 },
  { NAV_STATE_PATH,      0 },
  { LOCATION_STATE_PATH, 0 },
};

#define SOURCE_COUNT (sizeof(g_sources) / sizeof(g_sources[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: read_file_to_buf
 *
 * Description:
 *   Read a file into a static buffer. Returns bytes read or -errno.
 *
 ****************************************************************************/

static int read_file_to_buf(FAR const char *path,
                            FAR char *buf, size_t buflen)
{
  int fd;
  ssize_t nread;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  nread = read(fd, buf, buflen - 1);
  close(fd);

  if (nread < 0)
    {
      return -errno;
    }

  buf[nread] = '\0';
  return (int)nread;
}

/****************************************************************************
 * Name: json_get_int
 *
 * Description:
 *   Extract an integer value from a JSON string by key.
 *
 ****************************************************************************/

static int json_get_int(FAR const char *json, FAR const char *key,
                        int default_val)
{
  FAR char *pos;
  char search[64];
  int val;

  snprintf(search, sizeof(search), "\"%s\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return default_val;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return default_val;
    }

  pos++;
  while (*pos == ' ')
    {
      pos++;
    }

  val = atoi(pos);
  return val;
}

/****************************************************************************
 * Name: json_get_float
 *
 * Description:
 *   Extract a float value from a JSON string by key.
 *
 ****************************************************************************/

static float json_get_float(FAR const char *json, FAR const char *key,
                            float default_val)
{
  FAR char *pos;
  char search[64];

  snprintf(search, sizeof(search), "\"%s\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return default_val;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return default_val;
    }

  pos++;
  while (*pos == ' ')
    {
      pos++;
    }

  return (float)atof(pos);
}

/****************************************************************************
 * Name: json_get_double
 *
 * Description:
 *   Extract a double value from a JSON string by key.
 *
 ****************************************************************************/

static double json_get_double(FAR const char *json, FAR const char *key,
                              double default_val)
{
  FAR char *pos;
  char search[64];

  snprintf(search, sizeof(search), "\"%s\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return default_val;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return default_val;
    }

  pos++;
  while (*pos == ' ')
    {
      pos++;
    }

  return atof(pos);
}

/****************************************************************************
 * Name: json_get_bool
 *
 * Description:
 *   Extract a boolean value from a JSON string by key.
 *
 ****************************************************************************/

static bool json_get_bool(FAR const char *json, FAR const char *key,
                          bool default_val)
{
  FAR char *pos;
  char search[64];

  snprintf(search, sizeof(search), "\"%s\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return default_val;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return default_val;
    }

  pos++;
  while (*pos == ' ')
    {
      pos++;
    }

  return strncmp(pos, "true", 4) == 0;
}

/****************************************************************************
 * Name: json_get_string
 *
 * Description:
 *   Extract a string value from a JSON string by key into buf.
 *
 ****************************************************************************/

static void json_get_string(FAR const char *json, FAR const char *key,
                            FAR char *buf, size_t buflen)
{
  FAR char *pos;
  FAR char *end;
  char search[64];
  size_t len;

  buf[0] = '\0';
  snprintf(search, sizeof(search), "\"%s\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return;
    }

  pos++;
  while (*pos == ' ')
    {
      pos++;
    }

  if (*pos != '"')
    {
      return;
    }

  pos++; /* skip opening quote */
  end = strchr(pos, '"');
  if (end == NULL)
    {
      return;
    }

  len = end - pos;
  if (len >= buflen)
    {
      len = buflen - 1;
    }

  memcpy(buf, pos, len);
  buf[len] = '\0';
}

/****************************************************************************
 * Name: parse_vehicle_state
 *
 * Description:
 *   Parse vehicle state JSON into the state struct.
 *
 ****************************************************************************/

static void parse_vehicle_state(FAR const char *json,
                                FAR qiban_vehicle_state_t *vs)
{
  vs->speed_kmh         = json_get_float(json, "speed_kmh", 0.0f);
  vs->battery_pct       = (float)json_get_int(json, "battery_percent", 0);
  vs->mileage_km        = (float)json_get_int(json,
                           "total_distance_km_x10", 0) / 10.0f;
  vs->ride_duration_sec = (uint32_t)json_get_int(json,
                           "ride_duration_min", 0) * 60;
  vs->alert_overspeed   = false;
  vs->alert_low_battery = false;
  vs->alert_fatigue     = false;

  /* Parse nested alerts object */

  if (strstr(json, "\"overspeed\"") != NULL)
    {
      vs->alert_overspeed = json_get_bool(
          strstr(json, "\"alerts\""), "overspeed", false);
    }

  if (strstr(json, "\"low_battery\"") != NULL)
    {
      FAR char *alerts_pos = strstr(json, "\"alerts\"");
      if (alerts_pos != NULL)
        {
          vs->alert_low_battery = json_get_bool(
              alerts_pos, "low_battery", false);
        }
    }
}

/****************************************************************************
 * Name: parse_nav_state
 *
 * Description:
 *   Parse navigation state JSON into the state struct.
 *
 ****************************************************************************/

static void parse_nav_state(FAR const char *json,
                            FAR qiban_vehicle_state_t *vs)
{
  vs->nav_active = json_get_bool(json, "active", false);
  vs->nav_remaining_km = json_get_float(json,
                          "remaining_distance_m", 0.0f) / 1000.0f;
  vs->nav_turn_dist_m = json_get_int(json, "remaining_distance_m", 0);

  json_get_string(json, "next_turn",
                  vs->nav_next_turn, sizeof(vs->nav_next_turn));
  json_get_string(json, "status",
                  vs->nav_status, sizeof(vs->nav_status));
  json_get_string(json, "destination",
                  vs->nav_destination, sizeof(vs->nav_destination));
}

/****************************************************************************
 * Name: parse_location_state
 *
 * Description:
 *   Parse location state JSON into the state struct.
 *
 ****************************************************************************/

static void parse_location_state(FAR const char *json,
                                 FAR qiban_vehicle_state_t *vs)
{
  vs->latitude  = json_get_double(json, "latitude", 0.0);
  vs->longitude = json_get_double(json, "longitude", 0.0);
  vs->gps_valid = (vs->latitude != 0.0 && vs->longitude != 0.0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qiban_vehicle_state_init
 ****************************************************************************/

void qiban_vehicle_state_init(void)
{
  memset(g_sources, 0, sizeof(g_sources));
  g_sources[0].path = VEHICLE_STATE_PATH;
  g_sources[1].path = NAV_STATE_PATH;
  g_sources[2].path = LOCATION_STATE_PATH;
}

/****************************************************************************
 * Name: qiban_vehicle_state_update
 *
 * Description:
 *   Poll JSON files and update state if changed.
 *
 ****************************************************************************/

static qiban_vehicle_state_t g_current_state;

int qiban_vehicle_state_update(void)
{
  char buf[JSON_READER_BUF_SIZE];
  struct stat st;
  int updated = 0;
  int i;

  for (i = 0; i < (int)SOURCE_COUNT; i++)
    {
      if (stat(g_sources[i].path, &st) < 0)
        {
          continue;
        }

      if (st.st_mtime == g_sources[i].last_mtime)
        {
          continue;
        }

      /* File changed — read and parse */

      if (read_file_to_buf(g_sources[i].path,
                           buf, sizeof(buf)) < 0)
        {
          continue;
        }

      g_sources[i].last_mtime = st.st_mtime;

      switch (i)
        {
          case 0:
            parse_vehicle_state(buf, &g_current_state);
            break;
          case 1:
            parse_nav_state(buf, &g_current_state);
            break;
          case 2:
            parse_location_state(buf, &g_current_state);
            break;
        }

      updated++;
    }

  return updated;
}

/****************************************************************************
 * Name: qiban_vehicle_state_get
 ****************************************************************************/

void qiban_vehicle_state_get(FAR qiban_vehicle_state_t *out)
{
  if (out != NULL)
    {
      memcpy(out, &g_current_state, sizeof(qiban_vehicle_state_t));
    }
}

/****************************************************************************
 * Name: qiban_vehicle_state_set_derived
 ****************************************************************************/

void qiban_vehicle_state_set_derived(bool is_moving,
                                     uint32_t stationary_sec)
{
  g_current_state.is_moving = is_moving;
  g_current_state.stationary_sec = stationary_sec;
}
