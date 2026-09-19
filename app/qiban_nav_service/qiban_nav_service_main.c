/****************************************************************************
 * qiban_nav_service_main.c
 *
 * Board-side command entry for the "Qiban AI" navigation MVP.
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_NAV_DIR                "/data"
#define QIBAN_NAV_REQUEST_PATH       QIBAN_NAV_DIR "/qiban_nav_request.json"
#define QIBAN_NAV_STATE_PATH         QIBAN_NAV_DIR "/qiban_nav_state.json"
#define QIBAN_LOCATION_STATE_PATH    QIBAN_NAV_DIR "/qiban_location_state.json"
#define QIBAN_NAV_INPUT_DIR          QIBAN_NAV_DIR "/qiban_inputs"
#define QIBAN_NAV_INPUT_PATH         QIBAN_NAV_INPUT_DIR "/nav_remaining_m"
#define QIBAN_NAV_SOURCE             "board:qiban_nav_service"
#define QIBAN_NAV_SCHEMA_VERSION     1
#define QIBAN_NAV_DEFAULT_LONGITUDE  112.938814
#define QIBAN_NAV_DEFAULT_LATITUDE   28.228209
#define QIBAN_NAV_DEFAULT_ZOOM       17
#define QIBAN_NAV_AMAP_KEY_ENV       "QIBAN_AMAP_KEY"
#define QIBAN_NAV_AMAP_GEOCODE_URL   "http://restapi.amap.com/v3/geocode/geo"
#define QIBAN_NAV_AMAP_DRIVING_URL   "http://restapi.amap.com/v3/direction/driving"
#define QIBAN_NAV_AMAP_URL_SIZE      768
#define QIBAN_NAV_GEOCODE_BUFFER_SIZE 4096
#define QIBAN_NAV_ROUTE_BUFFER_SIZE  16384
#define QIBAN_NAV_MAX_STEPS          32
#define QIBAN_NAV_STATUS_READY       "路线已同步到地图"
#define QIBAN_NAV_STATUS_MAP_FAILED  "导航状态已生成，地图同步失败"
#define QIBAN_NAV_STATUS_CLEARED     "导航已清除"
#define QIBAN_NAV_STATUS_GEOCODE_FAILED "地址解析失败，请补坐标或检查网络"

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

struct qiban_destination_s
{
  FAR const char *name;
  double longitude;
  double latitude;
  int total_distance_m;
  int eta_minutes;
  FAR const char *next_turn;
};

struct qiban_nav_state_s
{
  bool active;
  char destination[48];
  char status[48];
  char next_turn[96];
  int total_distance_m;
  int remaining_distance_m;
  int eta_minutes;
  double start_longitude;
  double start_latitude;
  double end_longitude;
  double end_latitude;
};

struct qiban_curl_buffer_s
{
  FAR char *data;
  size_t capacity;
  size_t length;
  bool truncated;
};

struct qiban_route_step_s
{
  char instruction[128];
  char road[64];
  int distance_m;
  int duration_s;
  char action[16];
};

struct qiban_route_s
{
  struct qiban_route_step_s steps[QIBAN_NAV_MAX_STEPS];
  int step_count;
  int current_step;
  int total_distance_m;
  int total_duration_s;
};

static struct qiban_route_s g_current_route;

static const struct qiban_destination_s g_qiban_destinations[] =
{
  {
    "软件园二期",
    116.508789,
    39.984674,
    4800,
    12,
    "前方 180 米右转进入学院路"
  },
  {
    "软件园一期",
    116.498621,
    39.988219,
    3600,
    10,
    "前方 220 米直行通过创业大道路口"
  },
  {
    "中关村壹号",
    116.470885,
    40.044772,
    6200,
    16,
    "前方 300 米靠左进入北清路辅路"
  },
  {
    "清华大学",
    116.326980,
    40.003202,
    3900,
    11,
    "前方 200 米右转，沿清华东路继续前进"
  },
  {
    "清华东门",
    116.331457,
    40.003573,
    4200,
    12,
    "前方 180 米右转，目的地位于道路右侧"
  },
  {
    "北京大学东门",
    116.316472,
    39.992924,
    3600,
    10,
    "前方 220 米左转，目的地位于道路左侧"
  },
  {
    "北大东门",
    116.316472,
    39.992924,
    3600,
    10,
    "前方 220 米左转，目的地位于道路左侧"
  },
  {
    "北京大学",
    116.310000,
    39.992000,
    4100,
    12,
    "前方 260 米直行，沿校园路继续前进"
  },
  {
    "北大",
    116.310000,
    39.992000,
    4100,
    12,
    "前方 260 米直行，沿校园路继续前进"
  }
};

static int qiban_ensure_data_dir(void)
{
  int ret;

  ret = mkdir(QIBAN_NAV_DIR, 0777);
  if (ret < 0 && errno != EEXIST)
    {
      fprintf(stderr, "failed to create %s: %d\n", QIBAN_NAV_DIR, errno);
      return -errno;
    }

  ret = mkdir(QIBAN_NAV_INPUT_DIR, 0777);
  if (ret < 0 && errno != EEXIST)
    {
      fprintf(stderr, "failed to create %s: %d\n", QIBAN_NAV_INPUT_DIR, errno);
      return -errno;
    }

  return OK;
}

static int qiban_make_temp_path(FAR const char *dst_path,
                                FAR char *buffer, size_t buffer_size)
{
  int written;

  written = snprintf(buffer, buffer_size, "%s.%ld.tmp",
                     dst_path, (long)getpid());
  if (written < 0 || written >= (int)buffer_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static time_t qiban_now_epoch_s(void)
{
  time_t now;

  now = time(NULL);
  if (now < 0)
    {
      return 0;
    }

  return now;
}

static int qiban_write_text_atomic(FAR const char *path,
                                   FAR const char *content)
{
  char temp_path[PATH_MAX];
  FAR FILE *fp;
  int close_result;
  int ret;

  ret = qiban_make_temp_path(path, temp_path, sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      fprintf(stderr, "failed to open %s: %d\n", temp_path, errno);
      return -errno;
    }

  if (fputs(content, fp) == EOF || ferror(fp) != 0)
    {
      fclose(fp);
      unlink(temp_path);
      fprintf(stderr, "failed to write %s\n", temp_path);
      return -EIO;
    }

  close_result = fclose(fp);
  if (close_result != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to write %s\n", temp_path);
      return -EIO;
    }

  if (rename(temp_path, path) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, path, errno);
      return -errno;
    }

  return OK;
}

static int qiban_write_int_atomic(FAR const char *path, int value)
{
  char buffer[32];

  snprintf(buffer, sizeof(buffer), "%d\n", value);
  return qiban_write_text_atomic(path, buffer);
}

static void qiban_state_set_defaults(FAR struct qiban_nav_state_s *state)
{
  state->active = false;
  state->destination[0] = '\0';
  snprintf(state->status, sizeof(state->status), "%s", QIBAN_NAV_STATUS_CLEARED);
  state->next_turn[0] = '\0';
  state->total_distance_m = 0;
  state->remaining_distance_m = 0;
  state->eta_minutes = 0;
  state->start_longitude = QIBAN_NAV_DEFAULT_LONGITUDE;
  state->start_latitude = QIBAN_NAV_DEFAULT_LATITUDE;
  state->end_longitude = QIBAN_NAV_DEFAULT_LONGITUDE;
  state->end_latitude = QIBAN_NAV_DEFAULT_LATITUDE;
}

static bool qiban_nav_coordinate_is_valid(double value,
                                          double minimum,
                                          double maximum)
{
  return isfinite(value) && value >= minimum && value <= maximum;
}

static void qiban_nav_state_sanitize(FAR struct qiban_nav_state_s *state)
{
  if (state == NULL)
    {
      return;
    }

  if (!qiban_nav_coordinate_is_valid(state->start_longitude,
                                     -180.0, 180.0) ||
      !qiban_nav_coordinate_is_valid(state->start_latitude,
                                     -90.0, 90.0))
    {
      state->start_longitude = QIBAN_NAV_DEFAULT_LONGITUDE;
      state->start_latitude = QIBAN_NAV_DEFAULT_LATITUDE;
    }

  if (!qiban_nav_coordinate_is_valid(state->end_longitude,
                                     -180.0, 180.0) ||
      !qiban_nav_coordinate_is_valid(state->end_latitude,
                                     -90.0, 90.0))
    {
      state->end_longitude = QIBAN_NAV_DEFAULT_LONGITUDE;
      state->end_latitude = QIBAN_NAV_DEFAULT_LATITUDE;
    }
}

static FAR const struct qiban_destination_s *
qiban_find_destination(FAR const char *destination)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_qiban_destinations) / sizeof(g_qiban_destinations[0]);
       i++)
    {
      if (strcmp(destination, g_qiban_destinations[i].name) == 0)
        {
          return &g_qiban_destinations[i];
        }
    }

  return NULL;
}

static int qiban_try_parse_coordinate(FAR const char *text,
                                      double minimum, double maximum,
                                      FAR double *out_value)
{
  FAR char *endptr;
  double value;

  if (text == NULL || *text == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  value = strtod(text, &endptr);
  if (text == endptr || errno != 0)
    {
      return -EINVAL;
    }

  while (*endptr == ' ' || *endptr == '\t' ||
         *endptr == '\n' || *endptr == '\r')
    {
      endptr++;
    }

  if (*endptr != '\0')
    {
      return -EINVAL;
    }

  if (value < minimum || value > maximum)
    {
      return -ERANGE;
    }

  *out_value = value;
  return OK;
}

static void qiban_print_destinations(void)
{
  unsigned int i;

  printf("Supported destinations:\n");
  for (i = 0; i < sizeof(g_qiban_destinations) / sizeof(g_qiban_destinations[0]);
       i++)
    {
      printf("  %s\n", g_qiban_destinations[i].name);
    }
}

static int qiban_join_destination_name(int argc, FAR char *argv[],
                                       int start_index,
                                       int end_index,
                                       FAR char *buffer,
                                       size_t buffer_size)
{
  size_t used = 0;
  int index;

  if (start_index >= end_index || end_index > argc)
    {
      return -EINVAL;
    }

  buffer[0] = '\0';

  for (index = start_index; index < end_index; index++)
    {
      int written;

      written = snprintf(buffer + used, buffer_size - used, "%s%s",
                         used == 0 ? "" : " ", argv[index]);
      if (written < 0 || written >= (int)(buffer_size - used))
        {
          return -ENAMETOOLONG;
        }

      used += (size_t)written;
    }

  return OK;
}

static int qiban_parse_start_args(int argc, FAR char *argv[],
                                  FAR char *destination_name,
                                  size_t destination_size,
                                  FAR double *start_longitude,
                                  FAR double *start_latitude,
                                  FAR double *end_longitude,
                                  FAR double *end_latitude,
                                  FAR bool *has_explicit_start_coordinates,
                                  FAR bool *has_explicit_end_coordinates)
{
  int destination_end_index = argc;
  double parsed_start_longitude = 0.0;
  double parsed_start_latitude = 0.0;
  double parsed_end_longitude = 0.0;
  double parsed_end_latitude = 0.0;

  *has_explicit_start_coordinates = false;
  *has_explicit_end_coordinates = false;

  if (argc >= 7 &&
      qiban_try_parse_coordinate(argv[argc - 4], -180.0, 180.0,
                                 &parsed_start_longitude) == OK &&
      qiban_try_parse_coordinate(argv[argc - 3], -90.0, 90.0,
                                 &parsed_start_latitude) == OK &&
      qiban_try_parse_coordinate(argv[argc - 2], -180.0, 180.0,
                                 &parsed_end_longitude) == OK &&
      qiban_try_parse_coordinate(argv[argc - 1], -90.0, 90.0,
                                 &parsed_end_latitude) == OK)
    {
      destination_end_index = argc - 4;
      *start_longitude = parsed_start_longitude;
      *start_latitude = parsed_start_latitude;
      *end_longitude = parsed_end_longitude;
      *end_latitude = parsed_end_latitude;
      *has_explicit_start_coordinates = true;
      *has_explicit_end_coordinates = true;
    }
  else if (argc >= 5 &&
           qiban_try_parse_coordinate(argv[argc - 2], -180.0, 180.0,
                                      &parsed_end_longitude) == OK &&
           qiban_try_parse_coordinate(argv[argc - 1], -90.0, 90.0,
                                      &parsed_end_latitude) == OK)
    {
      destination_end_index = argc - 2;
      *end_longitude = parsed_end_longitude;
      *end_latitude = parsed_end_latitude;
      *has_explicit_end_coordinates = true;
    }

  return qiban_join_destination_name(argc, argv, 2, destination_end_index,
                                     destination_name, destination_size);
}

static int qiban_estimate_distance_m(double start_longitude,
                                     double start_latitude,
                                     double end_longitude,
                                     double end_latitude)
{
  double longitude_delta;
  double latitude_delta;
  int east_west_m;
  int north_south_m;
  int distance_m;

  longitude_delta = end_longitude - start_longitude;
  if (longitude_delta < 0.0)
    {
      longitude_delta = -longitude_delta;
    }

  latitude_delta = end_latitude - start_latitude;
  if (latitude_delta < 0.0)
    {
      latitude_delta = -latitude_delta;
    }

  east_west_m = (int)(longitude_delta * 90000.0);
  north_south_m = (int)(latitude_delta * 111000.0);
  distance_m = east_west_m + north_south_m;
  if (distance_m < 800)
    {
      distance_m = 800;
    }

  return distance_m;
}

static FAR const char *qiban_json_find_key(FAR const char *json,
                                           FAR const char *key)
{
  char pattern[64];

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  return strstr(json, pattern);
}

static bool qiban_json_extract_string(FAR const char *json,
                                      FAR const char *key,
                                      FAR char *buffer,
                                      size_t buffer_size)
{
  FAR const char *cursor;
  FAR const char *start;
  FAR const char *end;
  size_t length;

  cursor = qiban_json_find_key(json, key);
  if (cursor == NULL)
    {
      return false;
    }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
    {
      return false;
    }

  start = strchr(cursor, '"');
  if (start == NULL)
    {
      return false;
    }

  start++;
  end = strchr(start, '"');
  if (end == NULL)
    {
      return false;
    }

  length = (size_t)(end - start);
  if (length >= buffer_size)
    {
      length = buffer_size - 1;
    }

  memcpy(buffer, start, length);
  buffer[length] = '\0';
  return true;
}

static bool qiban_json_extract_double(FAR const char *json,
                                      FAR const char *key,
                                      FAR double *out)
{
  FAR const char *cursor;
  FAR char *endptr;
  double value;

  cursor = qiban_json_find_key(json, key);
  if (cursor == NULL)
    {
      return false;
    }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
    {
      return false;
    }

  cursor++;
  while (*cursor == ' ' || *cursor == '\t' ||
         *cursor == '\n' || *cursor == '\r')
    {
      cursor++;
    }

  errno = 0;
  value = strtod(cursor, &endptr);
  if (cursor == endptr || errno != 0 || !isfinite(value))
    {
      return false;
    }

  *out = value;
  return true;
}

static size_t qiban_curl_buffer_write_cb(FAR void *ptr, size_t size,
                                         size_t nmemb, FAR void *userdata)
{
  struct qiban_curl_buffer_s *buffer;
  size_t total;
  size_t available;

  buffer = (struct qiban_curl_buffer_s *)userdata;
  total = size * nmemb;

  if (buffer == NULL)
    {
      return 0;
    }

  if (buffer->data == NULL || buffer->capacity < 2)
    {
      return 0;
    }

  if (buffer->length >= buffer->capacity - 1)
    {
      buffer->truncated = true;
      return 0;
    }

  available = buffer->capacity - 1 - buffer->length;
  if (total > available)
    {
      total = available;
      buffer->truncated = true;
    }

  memcpy(buffer->data + buffer->length, ptr, total);
  buffer->length += total;
  buffer->data[buffer->length] = '\0';
  return size * nmemb;
}

static int qiban_json_extract_coordinate_pair(FAR const char *json,
                                              FAR double *longitude,
                                              FAR double *latitude)
{
  FAR const char *cursor;
  FAR const char *start;
  FAR char *endptr;
  double parsed_longitude;
  double parsed_latitude;

  cursor = strstr(json, "\"location\"");
  if (cursor == NULL)
    {
      return -ENOENT;
    }

  start = strchr(cursor, ':');
  if (start == NULL)
    {
      return -EINVAL;
    }

  start = strchr(start, '"');
  if (start == NULL)
    {
      return -EINVAL;
    }

  start++;
  errno = 0;
  parsed_longitude = strtod(start, &endptr);
  if (start == endptr || errno != 0 || *endptr != ',')
    {
      return -EINVAL;
    }

  start = endptr + 1;
  errno = 0;
  parsed_latitude = strtod(start, &endptr);
  if (start == endptr || errno != 0)
    {
      return -EINVAL;
    }

  *longitude = parsed_longitude;
  *latitude = parsed_latitude;
  return OK;
}

static int qiban_geocode_destination(FAR const char *destination_name,
                                     FAR double *end_longitude,
                                     FAR double *end_latitude)
{
  FAR const char *amap_key;
  struct qiban_curl_buffer_s response;
  char url[QIBAN_NAV_AMAP_URL_SIZE];
  CURL *curl;
  CURLcode code;
  FAR char *escaped_address;
  char status[8];
  int written;
  int ret;

  amap_key = getenv(QIBAN_NAV_AMAP_KEY_ENV);
  if (amap_key == NULL || amap_key[0] == '\0')
    {
      fprintf(stderr, "%s is not set\n", QIBAN_NAV_AMAP_KEY_ENV);
      return -EACCES;
    }

  memset(&response, 0, sizeof(response));
  response.capacity = QIBAN_NAV_GEOCODE_BUFFER_SIZE;
  response.data = malloc(response.capacity);
  if (response.data == NULL)
    {
      return -ENOMEM;
    }

  response.data[0] = '\0';

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      fprintf(stderr, "curl_global_init failed: %d\n", code);
      ret = -EIO;
      goto errout;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      ret = -ENOMEM;
      goto errout;
    }

  escaped_address = curl_easy_escape(curl, destination_name, 0);
  if (escaped_address == NULL)
    {
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      ret = -ENOMEM;
      goto errout;
    }

  written = snprintf(url, sizeof(url),
                     "%s?address=%s&output=json&key=%s",
                     QIBAN_NAV_AMAP_GEOCODE_URL,
                     escaped_address,
                     amap_key);
  curl_free(escaped_address);
  if (written < 0 || written >= (int)sizeof(url))
    {
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      ret = -ENAMETOOLONG;
      goto errout;
    }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, qiban_curl_buffer_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_nav_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  code = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  if (code != CURLE_OK)
    {
      fprintf(stderr, "geocode request failed for %s: %s\n",
              destination_name, curl_easy_strerror(code));
      ret = -EIO;
      goto errout;
    }

  if (response.length == 0 || response.truncated)
    {
      fprintf(stderr, "geocode response invalid for %s\n", destination_name);
      ret = -EIO;
      goto errout;
    }

  status[0] = '\0';
  qiban_json_extract_string(response.data, "status", status, sizeof(status));
  if (strcmp(status, "1") != 0)
    {
      fprintf(stderr, "geocode response status invalid for %s\n",
              destination_name);
      ret = -ENOENT;
      goto errout;
    }

  ret = qiban_json_extract_coordinate_pair(response.data,
                                           end_longitude,
                                           end_latitude);
  if (ret < 0)
    {
      fprintf(stderr, "geocode response missing location for %s\n",
              destination_name);
      goto errout;
    }

  free(response.data);
  return OK;

errout:
  free(response.data);
  return ret;
}

/****************************************************************************
 * Route planning: call Amap driving directions API and parse steps
 ****************************************************************************/

static int qiban_parse_route_steps(FAR const char *json,
                                   FAR struct qiban_route_s *route)
{
  FAR const char *paths;
  FAR const char *steps;
  FAR const char *pos;
  FAR const char *end;
  int i;

  route->step_count = 0;
  route->current_step = 0;

  /* Find "paths" array */

  paths = strstr(json, "\"paths\"");
  if (paths == NULL)
    {
      return -ENOENT;
    }

  /* Find "steps" array inside first path */

  steps = strstr(paths, "\"steps\"");
  if (steps == NULL)
    {
      return -ENOENT;
    }

  /* Extract total distance and duration from path */

  pos = strstr(paths, "\"distance\"");
  if (pos != NULL && pos < steps)
    {
      pos = strchr(pos, ':');
      if (pos != NULL)
        {
          route->total_distance_m = atoi(pos + 1);
        }
    }

  pos = strstr(paths, "\"duration\"");
  if (pos != NULL && pos < steps)
    {
      pos = strchr(pos, ':');
      if (pos != NULL)
        {
          route->total_duration_s = atoi(pos + 1);
        }
    }

  /* Parse each step in the steps array */

  pos = steps;
  for (i = 0; i < QIBAN_NAV_MAX_STEPS; i++)
    {
      FAR const char *instruction;
      FAR const char *road;
      FAR const char *distance;
      FAR const char *duration;
      FAR const char *action;
      FAR const char *step_start;
      size_t len;

      /* Find next step object */

      step_start = strstr(pos, "\"instruction\"");
      if (step_start == NULL)
        {
          break;
        }

      /* Find the enclosing braces to limit our search */

      end = strstr(step_start + 13, "\"instruction\"");
      if (end == NULL)
        {
          end = step_start + 2048;
        }

      /* Extract instruction text */

      instruction = strstr(step_start, "\"instruction\"");
      if (instruction != NULL)
        {
          FAR const char *val = strchr(instruction + 13, '"');
          if (val != NULL)
            {
              val++;
              FAR const char *val_end = strchr(val, '"');
              if (val_end != NULL)
                {
                  len = (size_t)(val_end - val);
                  if (len >= sizeof(route->steps[i].instruction))
                    {
                      len = sizeof(route->steps[i].instruction) - 1;
                    }

                  memcpy(route->steps[i].instruction, val, len);
                  route->steps[i].instruction[len] = '\0';
                }
            }
        }

      /* Extract road name */

      road = strstr(step_start, "\"road\"");
      if (road != NULL && road < end)
        {
          FAR const char *val = strchr(road + 6, '"');
          if (val != NULL)
            {
              val++;
              FAR const char *val_end = strchr(val, '"');
              if (val_end != NULL)
                {
                  len = (size_t)(val_end - val);
                  if (len >= sizeof(route->steps[i].road))
                    {
                      len = sizeof(route->steps[i].road) - 1;
                    }

                  memcpy(route->steps[i].road, val, len);
                  route->steps[i].road[len] = '\0';
                }
            }
        }

      /* Extract distance */

      distance = strstr(step_start, "\"distance\"");
      if (distance != NULL && distance < end)
        {
          FAR const char *colon = strchr(distance + 10, ':');
          if (colon != NULL)
            {
              route->steps[i].distance_m = atoi(colon + 1);
            }
        }

      /* Extract duration */

      duration = strstr(step_start, "\"duration\"");
      if (duration != NULL && duration < end)
        {
          FAR const char *colon = strchr(duration + 10, ':');
          if (colon != NULL)
            {
              route->steps[i].duration_s = atoi(colon + 1);
            }
        }

      /* Extract action (turn type) */

      action = strstr(step_start, "\"action\"");
      if (action != NULL && action < end)
        {
          FAR const char *val = strchr(action + 8, '"');
          if (val != NULL)
            {
              val++;
              FAR const char *val_end = strchr(val, '"');
              if (val_end != NULL)
                {
                  len = (size_t)(val_end - val);
                  if (len >= sizeof(route->steps[i].action))
                    {
                      len = sizeof(route->steps[i].action) - 1;
                    }

                  memcpy(route->steps[i].action, val, len);
                  route->steps[i].action[len] = '\0';
                }
            }
        }

      /* If instruction is empty, generate a fallback */

      if (route->steps[i].instruction[0] == '\0')
        {
          if (route->steps[i].road[0] != '\0')
            {
              snprintf(route->steps[i].instruction,
                       sizeof(route->steps[i].instruction),
                       "沿%s继续行驶 %d 米",
                       route->steps[i].road,
                       route->steps[i].distance_m);
            }
          else
            {
              snprintf(route->steps[i].instruction,
                       sizeof(route->steps[i].instruction),
                       "继续行驶 %d 米",
                       route->steps[i].distance_m);
            }
        }

      route->step_count = i + 1;
      pos = step_start + 13;
    }

  return route->step_count > 0 ? OK : -ENOENT;
}

static int qiban_plan_route(double start_lon, double start_lat,
                            double end_lon, double end_lat,
                            FAR struct qiban_route_s *route)
{
  FAR const char *amap_key;
  struct qiban_curl_buffer_s response;
  char url[QIBAN_NAV_ROUTE_BUFFER_SIZE];
  char start_str[32];
  char end_str[32];
  CURL *curl;
  CURLcode code;
  int written;
  int ret;

  amap_key = getenv(QIBAN_NAV_AMAP_KEY_ENV);
  if (amap_key == NULL || amap_key[0] == '\0')
    {
      fprintf(stderr, "%s is not set\n", QIBAN_NAV_AMAP_KEY_ENV);
      return -EACCES;
    }

  memset(route, 0, sizeof(*route));
  memset(&response, 0, sizeof(response));
  response.capacity = QIBAN_NAV_ROUTE_BUFFER_SIZE;
  response.data = malloc(response.capacity);
  if (response.data == NULL)
    {
      return -ENOMEM;
    }

  response.data[0] = '\0';

  snprintf(start_str, sizeof(start_str), "%.6f,%.6f",
           start_lon, start_lat);
  snprintf(end_str, sizeof(end_str), "%.6f,%.6f",
           end_lon, end_lat);

  written = snprintf(url, sizeof(url),
                     "%s?origin=%s&destination=%s"
                     "&strategy=0&extensions=all&key=%s",
                     QIBAN_NAV_AMAP_DRIVING_URL,
                     start_str, end_str,
                     amap_key);

  if (written < 0 || written >= (int)sizeof(url))
    {
      free(response.data);
      return -ENAMETOOLONG;
    }

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      free(response.data);
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      free(response.data);
      return -ENOMEM;
    }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, qiban_curl_buffer_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_nav_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  code = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  if (code != CURLE_OK)
    {
      fprintf(stderr, "route request failed: %s\n",
              curl_easy_strerror(code));
      free(response.data);
      return -EIO;
    }

  if (response.length == 0 || response.truncated)
    {
      fprintf(stderr, "route response invalid\n");
      free(response.data);
      return -EIO;
    }

  /* Check API status */

  {
    char status[8];
    status[0] = '\0';
    qiban_json_extract_string(response.data, "status", status, sizeof(status));
    if (strcmp(status, "1") != 0)
      {
        fprintf(stderr, "route API returned status=%s\n", status);
        free(response.data);
        return -ENOENT;
      }
  }

  ret = qiban_parse_route_steps(response.data, route);
  free(response.data);

  if (ret == OK)
    {
      printf("route planned: %d steps, %d m, %d s\n",
             route->step_count,
             route->total_distance_m,
             route->total_duration_s);
    }

  return ret;
}

static int qiban_write_request(FAR const struct qiban_nav_state_s *state)
{
  struct qiban_nav_state_s safe_state;
  char buffer[512];

  safe_state = *state;
  qiban_nav_state_sanitize(&safe_state);

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"destination\": \"%s\",\n"
           "  \"start_longitude\": %.6f,\n"
           "  \"start_latitude\": %.6f,\n"
           "  \"requested_by\": \"cli\"\n"
           "}\n",
           QIBAN_NAV_SCHEMA_VERSION,
           QIBAN_NAV_SOURCE,
           (long)qiban_now_epoch_s(),
           safe_state.destination,
           safe_state.start_longitude,
           safe_state.start_latitude);
  return qiban_write_text_atomic(QIBAN_NAV_REQUEST_PATH, buffer);
}

static int qiban_write_state(FAR const struct qiban_nav_state_s *state)
{
  struct qiban_nav_state_s safe_state;
  char buffer[768];

  safe_state = *state;
  qiban_nav_state_sanitize(&safe_state);

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"active\": %s,\n"
           "  \"destination\": \"%s\",\n"
           "  \"status\": \"%s\",\n"
           "  \"next_turn\": \"%s\",\n"
           "  \"total_distance_m\": %d,\n"
           "  \"remaining_distance_m\": %d,\n"
           "  \"eta_minutes\": %d,\n"
           "  \"start_longitude\": %.6f,\n"
           "  \"start_latitude\": %.6f,\n"
           "  \"end_longitude\": %.6f,\n"
           "  \"end_latitude\": %.6f\n"
           "}\n",
           QIBAN_NAV_SCHEMA_VERSION,
           QIBAN_NAV_SOURCE,
           (long)qiban_now_epoch_s(),
           safe_state.active ? "true" : "false",
           safe_state.destination,
           safe_state.status,
           safe_state.next_turn,
           safe_state.total_distance_m,
           safe_state.remaining_distance_m,
           safe_state.eta_minutes,
           safe_state.start_longitude,
           safe_state.start_latitude,
           safe_state.end_longitude,
           safe_state.end_latitude);
  return qiban_write_text_atomic(QIBAN_NAV_STATE_PATH, buffer);
}

static int qiban_write_location_state(FAR const char *source,
                                      double longitude,
                                      double latitude,
                                      int accuracy_m)
{
  struct qiban_nav_state_s safe_state;
  char buffer[512];

  qiban_state_set_defaults(&safe_state);
  safe_state.start_longitude = longitude;
  safe_state.start_latitude = latitude;
  qiban_nav_state_sanitize(&safe_state);

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"longitude\": %.6f,\n"
           "  \"latitude\": %.6f,\n"
           "  \"accuracy_m\": %d\n"
           "}\n",
           QIBAN_NAV_SCHEMA_VERSION,
           source,
           (long)qiban_now_epoch_s(),
           safe_state.start_longitude,
           safe_state.start_latitude,
           accuracy_m);
  return qiban_write_text_atomic(QIBAN_LOCATION_STATE_PATH, buffer);
}

static int qiban_publish_pending_state(FAR const struct qiban_nav_state_s *state,
                                       int nav_remaining_m)
{
  int ret;

  ret = qiban_write_request(state);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_int_atomic(QIBAN_NAV_INPUT_PATH, nav_remaining_m);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_state(state);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_location_state("board:qiban_nav_service:start",
                                   state->start_longitude,
                                   state->start_latitude,
                                   30);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

static int qiban_spawn_and_wait(FAR char * const argv[])
{
  pid_t pid;
  int ret;
  int status;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      fprintf(stderr, "failed to spawn %s: %d\n", argv[0], ret);
      return -ret;
    }

  if (waitpid(pid, &status, 0) < 0)
    {
      return -errno;
    }

  if (!WIFEXITED(status))
    {
      return -ECHILD;
    }

  if (WEXITSTATUS(status) != 0)
    {
      return -WEXITSTATUS(status);
    }

  return OK;
}

static int qiban_sync_route_map(FAR const struct qiban_nav_state_s *state)
{
  char start_lon[24];
  char start_lat[24];
  char end_lon[24];
  char end_lat[24];
  char zoom[8];
  FAR char *argv[9];

  snprintf(start_lon, sizeof(start_lon), "%.6f", state->start_longitude);
  snprintf(start_lat, sizeof(start_lat), "%.6f", state->start_latitude);
  snprintf(end_lon, sizeof(end_lon), "%.6f", state->end_longitude);
  snprintf(end_lat, sizeof(end_lat), "%.6f", state->end_latitude);
  snprintf(zoom, sizeof(zoom), "%d", QIBAN_NAV_DEFAULT_ZOOM);

  argv[0] = "qiban_map_service";
  argv[1] = "route";
  argv[2] = start_lon;
  argv[3] = start_lat;
  argv[4] = end_lon;
  argv[5] = end_lat;
  argv[6] = zoom;
  argv[7] = (FAR char *)state->destination;
  argv[8] = NULL;

  return qiban_spawn_and_wait(argv);
}

static int qiban_print_file(FAR const char *path)
{
  char buffer[768];
  FAR FILE *fp;
  size_t nread;

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      fprintf(stderr, "file not found: %s\n", path);
      return -errno;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      fprintf(stderr, "file is empty: %s\n", path);
      return -EIO;
    }

  buffer[nread] = '\0';
  printf("%s", buffer);
  return OK;
}

static bool qiban_read_current_location(FAR double *longitude,
                                        FAR double *latitude)
{
  char buffer[512];
  double parsed_longitude;
  double parsed_latitude;
  FAR FILE *fp;
  size_t nread;

  if (longitude == NULL || latitude == NULL)
    {
      return false;
    }

  fp = fopen(QIBAN_LOCATION_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';
  if (!qiban_json_extract_double(buffer, "longitude", &parsed_longitude) ||
      !qiban_json_extract_double(buffer, "latitude", &parsed_latitude))
    {
      return false;
    }

  if (!qiban_nav_coordinate_is_valid(parsed_longitude, -180.0, 180.0) ||
      !qiban_nav_coordinate_is_valid(parsed_latitude, -90.0, 90.0) ||
      (parsed_longitude == 0.0 && parsed_latitude == 0.0))
    {
      return false;
    }

  *longitude = parsed_longitude;
  *latitude = parsed_latitude;
  return true;
}

static int qiban_clear_navigation(void)
{
  struct qiban_nav_state_s state;
  int ret;

  qiban_state_set_defaults(&state);
  ret = qiban_write_state(&state);
  if (ret < 0)
    {
      return ret;
    }

  unlink(QIBAN_NAV_REQUEST_PATH);
  ret = qiban_write_int_atomic(QIBAN_NAV_INPUT_PATH, 0);
  if (ret < 0)
    {
      return ret;
    }

  printf("navigation cleared\n");
  return OK;
}

static int qiban_start_navigation(FAR const char *destination_name,
                                  bool has_explicit_start_coordinates,
                                  double explicit_start_longitude,
                                  double explicit_start_latitude,
                                  bool has_explicit_end_coordinates,
                                  double explicit_end_longitude,
                                  double explicit_end_latitude)
{
  FAR const struct qiban_destination_s *destination;
  struct qiban_nav_state_s state;
  int ret;

  qiban_state_set_defaults(&state);
  if (has_explicit_start_coordinates)
    {
      state.start_longitude = explicit_start_longitude;
      state.start_latitude = explicit_start_latitude;
      qiban_nav_state_sanitize(&state);
    }
  else if (qiban_read_current_location(&state.start_longitude,
                                       &state.start_latitude))
    {
      qiban_nav_state_sanitize(&state);
    }

  state.active = true;
  snprintf(state.destination, sizeof(state.destination), "%s", destination_name);
  snprintf(state.status, sizeof(state.status), "%s", "路线规划中");

  if (has_explicit_end_coordinates)
    {
      snprintf(state.next_turn, sizeof(state.next_turn), "%s", "沿规划路线行驶");
      state.end_longitude = explicit_end_longitude;
      state.end_latitude = explicit_end_latitude;
      state.total_distance_m = qiban_estimate_distance_m(state.start_longitude,
                                                         state.start_latitude,
                                                         state.end_longitude,
                                                         state.end_latitude);
      state.remaining_distance_m = state.total_distance_m;
      state.eta_minutes = state.total_distance_m / 350;
      if (state.eta_minutes < 3)
        {
          state.eta_minutes = 3;
        }
    }
  else
    {
      destination = qiban_find_destination(destination_name);
      if (destination != NULL)
        {
          snprintf(state.destination, sizeof(state.destination), "%s",
                   destination->name);
          snprintf(state.next_turn, sizeof(state.next_turn), "%s",
                   destination->next_turn);
          state.total_distance_m = destination->total_distance_m;
          state.remaining_distance_m = destination->total_distance_m;
          state.eta_minutes = destination->eta_minutes;
          state.end_longitude = destination->longitude;
          state.end_latitude = destination->latitude;
        }
      else
        {
          ret = qiban_geocode_destination(destination_name,
                                          &state.end_longitude,
                                          &state.end_latitude);
          if (ret < 0)
            {
              snprintf(state.status, sizeof(state.status), "%s",
                       QIBAN_NAV_STATUS_GEOCODE_FAILED);
              snprintf(state.next_turn, sizeof(state.next_turn), "%s",
                       "请执行 start <目的地> <lon> <lat> 完成导航");
              state.total_distance_m = 0;
              state.remaining_distance_m = 0;
              state.eta_minutes = 0;
              (void)qiban_publish_pending_state(&state, 0);
              fprintf(stderr,
                      "geocode failed for '%s'; you can still run:\n",
                      destination_name);
              fprintf(stderr,
                      "qiban_nav_service start %s <lon> <lat>\n",
                      destination_name);
              return ret;
            }

          snprintf(state.next_turn, sizeof(state.next_turn), "%s",
                   "地址已解析，沿规划路线行驶");
          state.total_distance_m =
            qiban_estimate_distance_m(state.start_longitude,
                                      state.start_latitude,
                                      state.end_longitude,
                                      state.end_latitude);
          state.remaining_distance_m = state.total_distance_m;
          state.eta_minutes = state.total_distance_m / 350;
          if (state.eta_minutes < 3)
            {
              state.eta_minutes = 3;
            }
        }
    }

  /* Plan real route via Amap driving directions API */

  {
    int route_ret;

    route_ret = qiban_plan_route(state.start_longitude,
                                 state.start_latitude,
                                 state.end_longitude,
                                 state.end_latitude,
                                 &g_current_route);
    if (route_ret == OK && g_current_route.step_count > 0)
      {
        /* Use real route data */

        state.total_distance_m = g_current_route.total_distance_m;
        state.remaining_distance_m = g_current_route.total_distance_m;
        state.eta_minutes = g_current_route.total_duration_s / 60;
        if (state.eta_minutes < 1)
          {
            state.eta_minutes = 1;
          }

        snprintf(state.next_turn, sizeof(state.next_turn), "%s",
                 g_current_route.steps[0].instruction);
        g_current_route.current_step = 0;
        printf("route: using real route (%d steps, %d m)\n",
               g_current_route.step_count,
               g_current_route.total_distance_m);
      }
    else
      {
        /* Fallback: use straight-line estimate */

        g_current_route.step_count = 0;
        printf("route: API failed, using straight-line estimate\n");
      }
  }

  ret = qiban_publish_pending_state(&state, state.remaining_distance_m);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_sync_route_map(&state);
  if (ret < 0)
    {
      snprintf(state.status, sizeof(state.status), "%s",
               QIBAN_NAV_STATUS_MAP_FAILED);
    }
  else
    {
      snprintf(state.status, sizeof(state.status), "%s", QIBAN_NAV_STATUS_READY);
    }

  if (qiban_write_state(&state) < 0)
    {
      return -EIO;
    }

  printf("navigation ready for %s\n", state.destination);
  printf("remaining=%d m, eta=%d min\n",
         state.remaining_distance_m, state.eta_minutes);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Watch mode: keep navigation state alive by updating remaining distance,
 * ETA, and next-turn hints every second based on current speed.
 ****************************************************************************/

#define QIBAN_WATCH_INTERVAL_S      1
#define QIBAN_WATCH_VEHICLE_PATH    QIBAN_NAV_DIR "/qiban_vehicle_state.json"
#define QIBAN_WATCH_JSON_BUF        1024
#define QIBAN_WATCH_ARRIVAL_THRESHOLD_M  50
#define QIBAN_WATCH_MAP_REFRESH_INTERVAL_S  10
#define QIBAN_WATCH_GPS_FRESHNESS_S        5

static int qiban_watch_read_text(FAR const char *path, FAR char *buf,
                                 size_t buf_size)
{
  int fd;
  ssize_t n;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  n = read(fd, buf, buf_size - 1);
  close(fd);

  if (n < 0)
    {
      return -errno;
    }

  buf[n] = '\0';
  return OK;
}

static bool qiban_watch_json_int(FAR const char *json, FAR const char *key,
                                 FAR int *out)
{
  FAR const char *pos;
  FAR char *endptr;
  char pattern[64];
  long val;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  pos = strstr(json, pattern);
  if (pos == NULL)
    {
      return false;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return false;
    }

  pos++;
  while (*pos == ' ' || *pos == '\t')
    {
      pos++;
    }

  errno = 0;
  val = strtol(pos, &endptr, 10);
  if (pos == endptr || errno != 0)
    {
      return false;
    }

  *out = (int)val;
  return true;
}

static bool qiban_watch_json_bool(FAR const char *json, FAR const char *key,
                                  FAR bool *out)
{
  FAR const char *pos;
  char pattern[64];

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  pos = strstr(json, pattern);
  if (pos == NULL)
    {
      return false;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return false;
    }

  pos++;
  while (*pos == ' ' || *pos == '\t')
    {
      pos++;
    }

  if (strncmp(pos, "true", 4) == 0)
    {
      *out = true;
      return true;
    }

  if (strncmp(pos, "false", 5) == 0)
    {
      *out = false;
      return true;
    }

  return false;
}

static bool qiban_watch_json_string(FAR const char *json,
                                    FAR const char *key,
                                    FAR char *buf, size_t buf_size)
{
  FAR const char *pos;
  FAR const char *start;
  FAR const char *end;
  char pattern[64];
  size_t len;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  pos = strstr(json, pattern);
  if (pos == NULL)
    {
      return false;
    }

  pos = strchr(pos, ':');
  if (pos == NULL)
    {
      return false;
    }

  start = strchr(pos, '"');
  if (start == NULL)
    {
      return false;
    }

  start++;
  end = strchr(start, '"');
  if (end == NULL)
    {
      return false;
    }

  len = (size_t)(end - start);
  if (len >= buf_size)
    {
      len = buf_size - 1;
    }

  memcpy(buf, start, len);
  buf[len] = '\0';
  return true;
}

static bool qiban_watch_json_double(FAR const char *json, FAR const char *key,
                                    FAR double *out)
{
  return qiban_json_extract_double(json, key, out);
}

static int qiban_watch_read_speed(FAR int *speed_kmh)
{
  char buf[QIBAN_WATCH_JSON_BUF];
  int ret;

  ret = qiban_watch_read_text(QIBAN_WATCH_VEHICLE_PATH, buf, sizeof(buf));
  if (ret < 0)
    {
      return ret;
    }

  if (!qiban_watch_json_int(buf, "speed_kmh", speed_kmh))
    {
      return -ENOENT;
    }

  return OK;
}

static int qiban_watch_read_nav(FAR struct qiban_nav_state_s *state)
{
  char buf[QIBAN_WATCH_JSON_BUF];
  int ret;
  int tmp;

  qiban_state_set_defaults(state);

  ret = qiban_watch_read_text(QIBAN_NAV_STATE_PATH, buf, sizeof(buf));
  if (ret < 0)
    {
      return ret;
    }

  if (!qiban_watch_json_bool(buf, "active", &state->active))
    {
      return -ENOENT;
    }

  qiban_watch_json_string(buf, "destination",
                          state->destination, sizeof(state->destination));
  qiban_watch_json_string(buf, "status",
                          state->status, sizeof(state->status));
  qiban_watch_json_string(buf, "next_turn",
                          state->next_turn, sizeof(state->next_turn));

  if (qiban_watch_json_int(buf, "total_distance_m", &tmp))
    {
      state->total_distance_m = tmp;
    }

  if (qiban_watch_json_int(buf, "remaining_distance_m", &tmp))
    {
      state->remaining_distance_m = tmp;
    }

  if (qiban_watch_json_int(buf, "eta_minutes", &tmp))
    {
      state->eta_minutes = tmp;
    }

  qiban_watch_json_double(buf, "start_longitude", &state->start_longitude);
  qiban_watch_json_double(buf, "start_latitude", &state->start_latitude);
  qiban_watch_json_double(buf, "end_longitude", &state->end_longitude);
  qiban_watch_json_double(buf, "end_latitude", &state->end_latitude);
  qiban_nav_state_sanitize(state);
  return OK;
}

static FAR const char *qiban_watch_next_turn_hint(int remaining_m,
                                                   int total_m)
{
  int pct;

  if (total_m <= 0)
    {
      return "已到达目的地";
    }

  pct = (remaining_m * 100) / total_m;

  if (pct > 70)
    {
      return "沿当前道路继续行驶";
    }

  if (pct > 40)
    {
      return "注意前方路口方向";
    }

  if (pct > 15)
    {
      return "接近目的地，注意减速";
    }

  if (remaining_m > QIBAN_WATCH_ARRIVAL_THRESHOLD_M)
    {
      return "即将到达目的地";
    }

  return "已到达目的地";
}

static void qiban_watch_estimate_position(
    FAR const struct qiban_nav_state_s *state,
    FAR double *cur_lon, FAR double *cur_lat)
{
  double progress;

  if (state->total_distance_m <= 0)
    {
      *cur_lon = state->end_longitude;
      *cur_lat = state->end_latitude;
      return;
    }

  /* Linear interpolation: 0.0 = at start, 1.0 = at end */

  progress = 1.0 - (double)state->remaining_distance_m /
                    (double)state->total_distance_m;
  if (progress < 0.0)
    {
      progress = 0.0;
    }

  if (progress > 1.0)
    {
      progress = 1.0;
    }

  *cur_lon = state->start_longitude +
             (state->end_longitude - state->start_longitude) * progress;
  *cur_lat = state->start_latitude +
             (state->end_latitude - state->start_latitude) * progress;
}

static bool qiban_watch_has_fresh_gps(void)
{
  char buf[QIBAN_WATCH_JSON_BUF];
  char source[48];
  int epoch = 0;
  time_t now;
  int ret;

  ret = qiban_watch_read_text(QIBAN_LOCATION_STATE_PATH, buf, sizeof(buf));
  if (ret < 0)
    {
      return false;
    }

  /* Check if source is phone GPS */

  source[0] = '\0';
  qiban_watch_json_string(buf, "source", source, sizeof(source));
  if (strncmp(source, "phone:", 6) != 0)
    {
      return false;
    }

  /* Check if timestamp is fresh */

  if (!qiban_watch_json_int(buf, "generated_at_epoch_s", &epoch))
    {
      return false;
    }

  now = time(NULL);
  if (now <= 0 || epoch <= 0)
    {
      return false;
    }

  return (now - (time_t)epoch) <= QIBAN_WATCH_GPS_FRESHNESS_S;
}

static void qiban_watch_update_location(
    FAR const struct qiban_nav_state_s *state)
{
  double cur_lon;
  double cur_lat;

  /* If phone GPS is fresh, don't overwrite with interpolated position */

  if (qiban_watch_has_fresh_gps())
    {
      return;
    }

  qiban_watch_estimate_position(state, &cur_lon, &cur_lat);
  (void)qiban_write_location_state("board:qiban_nav_service:watch",
                                    cur_lon, cur_lat, 30);
}

static int qiban_watch_refresh_map(FAR const struct qiban_nav_state_s *state)
{
  char lon_buf[24];
  char lat_buf[24];
  char zoom_buf[8];
  double cur_lon;
  double cur_lat;
  pid_t pid;
  int ret;

  qiban_watch_estimate_position(state, &cur_lon, &cur_lat);

  snprintf(lon_buf, sizeof(lon_buf), "%.6f", cur_lon);
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", cur_lat);
  snprintf(zoom_buf, sizeof(zoom_buf), "%d", QIBAN_NAV_DEFAULT_ZOOM);

  {
    FAR char *argv[6];
    argv[0] = "qiban_map_service";
    argv[1] = "amap";
    argv[2] = lon_buf;
    argv[3] = lat_buf;
    argv[4] = zoom_buf;
    argv[5] = NULL;

    ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  }

  if (ret != 0)
    {
      fprintf(stderr, "nav watch: map refresh spawn failed (%d)\n", ret);
      return -ret;
    }

  /* Reap zombie child to prevent PID table exhaustion */

  waitpid(pid, NULL, WNOHANG);

  return OK;
}

static int qiban_watch_step(FAR struct qiban_nav_state_s *state)
{
  int speed_kmh = 0;
  int advance_m;
  int ret;

  /* Read current speed from vehicle_service output */

  ret = qiban_watch_read_speed(&speed_kmh);
  if (ret < 0)
    {
      /* No speed data — keep last known state, just don't advance */

      speed_kmh = 0;
    }

  /* Clamp speed to sane range */

  if (speed_kmh < 0)
    {
      speed_kmh = 0;
    }

  if (speed_kmh > 120)
    {
      speed_kmh = 120;
    }

  /* Advance: distance covered in 1 second = speed_kmh * 1000 / 3600 */

  advance_m = (speed_kmh * 1000) / 3600;
  state->remaining_distance_m -= advance_m;

  if (state->remaining_distance_m < 0)
    {
      state->remaining_distance_m = 0;
    }

  /* Recalculate ETA */

  if (speed_kmh > 0 && state->remaining_distance_m > 0)
    {
      /* ETA in minutes = remaining_m / (speed_m_per_min) */

      state->eta_minutes = state->remaining_distance_m /
                           (speed_kmh * 1000 / 60);
      if (state->eta_minutes < 1)
        {
          state->eta_minutes = 1;
        }
    }
  else if (state->remaining_distance_m <= 0)
    {
      state->eta_minutes = 0;
    }

  /* Update next_turn — use real route steps if available */

  if (g_current_route.step_count > 0)
    {
      int covered_m;
      int step_boundary_m;
      int i;

      /* Calculate how far we've covered from the start */

      covered_m = state->total_distance_m - state->remaining_distance_m;

      /* Find which step we're currently in */

      step_boundary_m = 0;
      for (i = 0; i < g_current_route.step_count; i++)
        {
          step_boundary_m += g_current_route.steps[i].distance_m;
          if (covered_m < step_boundary_m)
            {
              g_current_route.current_step = i;
              break;
            }
        }

      /* If we've passed all steps, use last step */

      if (i >= g_current_route.step_count)
        {
          g_current_route.current_step = g_current_route.step_count - 1;
        }

      /* Show current step instruction */

      snprintf(state->next_turn, sizeof(state->next_turn), "%s",
               g_current_route.steps[g_current_route.current_step].instruction);

      /* If close to step boundary, show next step preview */

      {
        int next_step = g_current_route.current_step + 1;
        if (next_step < g_current_route.step_count)
          {
            int step_remaining = step_boundary_m - covered_m;
            if (step_remaining < 200 && step_remaining > 0)
              {
                snprintf(state->next_turn, sizeof(state->next_turn),
                         "%d 米后 %s",
                         step_remaining,
                         g_current_route.steps[next_step].instruction);
              }
          }
      }
    }
  else
    {
      /* No route steps — use simple progress-based hints */

      snprintf(state->next_turn, sizeof(state->next_turn), "%s",
               qiban_watch_next_turn_hint(state->remaining_distance_m,
                                          state->total_distance_m));
    }

  /* Check arrival */

  if (state->remaining_distance_m <= QIBAN_WATCH_ARRIVAL_THRESHOLD_M)
    {
      state->active = false;
      snprintf(state->status, sizeof(state->status), "已到达 %s",
               state->destination);
      state->remaining_distance_m = 0;
      state->eta_minutes = 0;

      /* Clear the nav input so vehicle_service stops showing nav distance */

      (void)qiban_write_int_atomic(QIBAN_NAV_INPUT_PATH, 0);

      /* Clear route data */

      memset(&g_current_route, 0, sizeof(g_current_route));
    }

  /* Persist updated state */

  ret = qiban_write_state(state);
  if (ret < 0)
    {
      return ret;
    }

  /* Update location so UI can move the "I" marker on the map */

  qiban_watch_update_location(state);

  return OK;
}

static int qiban_run_watch_loop(void)
{
  struct qiban_nav_state_s state;
  int tick = 0;
  int ret;

  printf("nav watch: started, waiting for active navigation...\n");

  for (; ; )
    {
      /* Read current nav state */

      ret = qiban_watch_read_nav(&state);
      if (ret < 0)
        {
          /* File not ready yet — wait and retry */

          sleep(QIBAN_WATCH_INTERVAL_S);
          continue;
        }

      if (!state.active)
        {
          /* No active navigation — idle polling */

          tick = 0;
          sleep(QIBAN_WATCH_INTERVAL_S);
          continue;
        }

      /* Active navigation — run one update step */

      ret = qiban_watch_step(&state);
      if (ret < 0)
        {
          fprintf(stderr, "nav watch: write failed (%d)\n", ret);
        }

      /* Periodically refresh the map centered on current position */

      tick++;
      if (tick % QIBAN_WATCH_MAP_REFRESH_INTERVAL_S == 0)
        {
          (void)qiban_watch_refresh_map(&state);
        }

      /* If we just arrived, log it */

      if (!state.active)
        {
          printf("nav watch: arrived at %s\n", state.destination);
          printf("nav watch: waiting for next navigation...\n");
        }

      sleep(QIBAN_WATCH_INTERVAL_S);
    }

  return OK;  /* unreachable */
}

static void qiban_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s list\n", progname);
  printf("  %s start <destination>\n", progname);
  printf("  %s start <destination> <end_lon> <end_lat>\n", progname);
  printf("  %s start <destination> <start_lon> <start_lat> <end_lon> <end_lat>\n",
         progname);
  printf("  %s watch\n", progname);
  printf("  %s status\n", progname);
  printf("  %s clear\n", progname);
}

int main(int argc, FAR char *argv[])
{
  char destination_name[64];
  bool has_explicit_start_coordinates;
  bool has_explicit_end_coordinates;
  double start_latitude = 0.0;
  double start_longitude = 0.0;
  double end_latitude = 0.0;
  double end_longitude = 0.0;
  int ret;

  ret = qiban_ensure_data_dir();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      qiban_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "list") == 0)
    {
      qiban_print_destinations();
      return OK;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      if (qiban_parse_start_args(argc, argv,
                                 destination_name,
                                 sizeof(destination_name),
                                 &start_longitude,
                                 &start_latitude,
                                 &end_longitude,
                                 &end_latitude,
                                 &has_explicit_start_coordinates,
                                 &has_explicit_end_coordinates) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_start_navigation(destination_name,
                                   has_explicit_start_coordinates,
                                   start_longitude,
                                   start_latitude,
                                   has_explicit_end_coordinates,
                                   end_longitude,
                                   end_latitude);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "watch") == 0)
    {
      ret = qiban_run_watch_loop();
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      ret = qiban_print_file(QIBAN_NAV_STATE_PATH);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "clear") == 0)
    {
      ret = qiban_clear_navigation();
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  qiban_usage(argv[0]);
  return EXIT_FAILURE;
}
