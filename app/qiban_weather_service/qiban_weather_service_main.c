/****************************************************************************
 * qiban_weather_service_main.c
 *
 * Fetch current weather for the Qiban AI dashboard.
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_WEATHER_STATE_PATH      "/data/qiban_weather.json"
#define QIBAN_WEATHER_LOCATION_PATH   "/data/qiban_location_state.json"
#define QIBAN_WEATHER_CONFIG_PATH     "/data/qiban_weather_config.json"
#define QIBAN_WEATHER_URL_SIZE        512
#define QIBAN_WEATHER_JSON_SIZE       4096
#define QIBAN_WEATHER_DEFAULT_LAT     39.9042
#define QIBAN_WEATHER_DEFAULT_LON     116.4074
#define QIBAN_WEATHER_DEFAULT_CITY    "北京"
#define QIBAN_WEATHER_DEFAULT_PERIOD  900

struct qiban_curl_buffer_s
{
  FAR char *data;
  size_t capacity;
  size_t length;
  bool truncated;
};

struct qiban_weather_s
{
  char condition[32];
  char city[32];
  char source[48];
  double longitude;
  double latitude;
  int temperature_c;
  int humidity;
  int wind_speed_kmh;
  int weather_code;
};

static size_t qiban_curl_write_cb(FAR void *ptr, size_t size,
                                  size_t nmemb, FAR void *userdata)
{
  struct qiban_curl_buffer_s *buffer;
  size_t total;
  size_t available;

  buffer = (struct qiban_curl_buffer_s *)userdata;
  total = size * nmemb;
  if (buffer == NULL || buffer->data == NULL || buffer->capacity < 2)
    {
      return 0;
    }

  available = buffer->capacity - 1 - buffer->length;
  if (total > available)
    {
      total = available;
      buffer->truncated = true;
    }

  if (total > 0)
    {
      memcpy(buffer->data + buffer->length, ptr, total);
      buffer->length += total;
      buffer->data[buffer->length] = '\0';
    }

  return size * nmemb;
}

static int qiban_read_file(FAR const char *path, FAR char *buffer,
                           size_t buffer_size)
{
  FAR FILE *fp;
  size_t nread;

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return -errno;
    }

  nread = fread(buffer, 1, buffer_size - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return -EIO;
    }

  buffer[nread] = '\0';
  return OK;
}

static bool qiban_json_find_number(FAR const char *json,
                                   FAR const char *key,
                                   double *out_value)
{
  char pattern[64];
  FAR const char *cursor;
  FAR char *endptr;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  cursor = strstr(json, pattern);
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
  errno = 0;
  *out_value = strtod(cursor, &endptr);
  return cursor != endptr && errno == 0;
}

static bool qiban_json_find_int(FAR const char *json,
                                FAR const char *key,
                                int *out_value)
{
  double value;

  if (!qiban_json_find_number(json, key, &value))
    {
      return false;
    }

  *out_value = (int)value;
  return true;
}

static bool qiban_json_find_string(FAR const char *json,
                                   FAR const char *key,
                                   FAR char *buffer,
                                   size_t buffer_size)
{
  char pattern[64];
  FAR const char *cursor;
  FAR const char *start;
  FAR const char *end;
  size_t length;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  cursor = strstr(json, pattern);
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

static FAR const char *qiban_json_find_object(FAR const char *json,
                                              FAR const char *key)
{
  char pattern[64];
  FAR const char *cursor;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  cursor = strstr(json, pattern);
  if (cursor == NULL)
    {
      return NULL;
    }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
    {
      return NULL;
    }

  cursor++;
  while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' ||
         *cursor == '\t')
    {
      cursor++;
    }

  return *cursor == '{' ? cursor : NULL;
}

static FAR const char *qiban_weather_condition_from_code(int code)
{
  if (code == 0)
    {
      return "clear";
    }

  if (code == 1 || code == 2 || code == 3 ||
      (code >= 45 && code <= 48))
    {
      return "cloudy";
    }

  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) ||
      code == 95 || code == 96 || code == 99)
    {
      return "rain";
    }

  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
    {
      return "snow";
    }

  return "cloudy";
}

static void qiban_weather_set_defaults(FAR struct qiban_weather_s *weather)
{
  memset(weather, 0, sizeof(*weather));
  snprintf(weather->condition, sizeof(weather->condition), "clear");
  snprintf(weather->city, sizeof(weather->city), QIBAN_WEATHER_DEFAULT_CITY);
  snprintf(weather->source, sizeof(weather->source), "open-meteo");
  weather->longitude = QIBAN_WEATHER_DEFAULT_LON;
  weather->latitude = QIBAN_WEATHER_DEFAULT_LAT;
}

static void qiban_weather_load_location(FAR struct qiban_weather_s *weather)
{
  char json[1024];
  double longitude;
  double latitude;

  if (qiban_read_file(QIBAN_WEATHER_LOCATION_PATH, json, sizeof(json)) == OK &&
      qiban_json_find_number(json, "longitude", &longitude) &&
      qiban_json_find_number(json, "latitude", &latitude))
    {
      weather->longitude = longitude;
      weather->latitude = latitude;
      qiban_json_find_string(json, "city", weather->city,
                             sizeof(weather->city));
      return;
    }

  if (qiban_read_file(QIBAN_WEATHER_CONFIG_PATH, json, sizeof(json)) == OK)
    {
      qiban_json_find_number(json, "longitude", &weather->longitude);
      qiban_json_find_number(json, "latitude", &weather->latitude);
      qiban_json_find_string(json, "city", weather->city,
                             sizeof(weather->city));
    }
}

static int qiban_weather_write_state(FAR const struct qiban_weather_s *weather,
                                     FAR const char *status)
{
  char path_tmp[128];
  FAR FILE *fp;
  time_t now;

  snprintf(path_tmp, sizeof(path_tmp), "%s.tmp", QIBAN_WEATHER_STATE_PATH);
  fp = fopen(path_tmp, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  now = time(NULL);
  fprintf(fp,
          "{\n"
          "  \"schema_version\": 2,\n"
          "  \"source\": \"%s\",\n"
          "  \"status\": \"%s\",\n"
          "  \"updated_at\": %ld,\n"
          "  \"updated_at_epoch_s\": %ld,\n"
          "  \"city\": \"%s\",\n"
          "  \"longitude\": %.6f,\n"
          "  \"latitude\": %.6f,\n"
          "  \"condition\": \"%s\",\n"
          "  \"weather_code\": %d,\n"
          "  \"temperature_c\": %d,\n"
          "  \"humidity\": %d,\n"
          "  \"wind_speed_kmh\": %d\n"
          "}\n",
          weather->source,
          status,
          (long)now,
          (long)now,
          weather->city,
          weather->longitude,
          weather->latitude,
          weather->condition,
          weather->weather_code,
          weather->temperature_c,
          weather->humidity,
          weather->wind_speed_kmh);

  fclose(fp);
  if (rename(path_tmp, QIBAN_WEATHER_STATE_PATH) != 0)
    {
      unlink(path_tmp);
      return -errno;
    }

  return OK;
}

static int qiban_weather_fetch_once(void)
{
  struct qiban_weather_s weather;
  struct qiban_curl_buffer_s response;
  char url[QIBAN_WEATHER_URL_SIZE];
  CURL *curl;
  CURLcode code;
  int ret;

  qiban_weather_set_defaults(&weather);
  qiban_weather_load_location(&weather);

  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?"
           "latitude=%.6f&longitude=%.6f&current=temperature_2m,"
           "relative_humidity_2m,weather_code,wind_speed_10m"
           "&wind_speed_unit=kmh&timezone=auto",
           weather.latitude,
           weather.longitude);

  memset(&response, 0, sizeof(response));
  response.capacity = QIBAN_WEATHER_JSON_SIZE;
  response.data = malloc(response.capacity);
  if (response.data == NULL)
    {
      return -ENOMEM;
    }

  response.data[0] = '\0';
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
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, qiban_curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_weather_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  code = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  if (code != CURLE_OK || response.length == 0 || response.truncated)
    {
      qiban_weather_write_state(&weather, "fetch_failed");
      free(response.data);
      return -EIO;
    }

  FAR const char *current = qiban_json_find_object(response.data, "current");
  if (current == NULL ||
      !qiban_json_find_int(current, "temperature_2m",
                           &weather.temperature_c) ||
      !qiban_json_find_int(current, "relative_humidity_2m",
                           &weather.humidity) ||
      !qiban_json_find_int(current, "wind_speed_10m",
                           &weather.wind_speed_kmh) ||
      !qiban_json_find_int(current, "weather_code",
                           &weather.weather_code))
    {
      qiban_weather_write_state(&weather, "parse_failed");
      free(response.data);
      return -EPROTO;
    }

  snprintf(weather.condition, sizeof(weather.condition), "%s",
           qiban_weather_condition_from_code(weather.weather_code));

  ret = qiban_weather_write_state(&weather, "ok");
  free(response.data);
  return ret;
}

static void qiban_weather_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s fetch\n", progname);
  printf("  %s serve [period_seconds]\n", progname);
  printf("  %s config <longitude> <latitude> [city]\n", progname);
  printf("State file: %s\n", QIBAN_WEATHER_STATE_PATH);
}

static int qiban_weather_write_config(int argc, FAR char *argv[])
{
  char tmp_path[128];
  FAR FILE *fp;
  FAR const char *city;
  double longitude;
  double latitude;

  if (argc < 4)
    {
      return -EINVAL;
    }

  longitude = strtod(argv[2], NULL);
  latitude = strtod(argv[3], NULL);
  city = argc > 4 ? argv[4] : QIBAN_WEATHER_DEFAULT_CITY;
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", QIBAN_WEATHER_CONFIG_PATH);

  fp = fopen(tmp_path, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  fprintf(fp,
          "{\n"
          "  \"longitude\": %.6f,\n"
          "  \"latitude\": %.6f,\n"
          "  \"city\": \"%s\"\n"
          "}\n",
          longitude,
          latitude,
          city);
  fclose(fp);

  if (rename(tmp_path, QIBAN_WEATHER_CONFIG_PATH) != 0)
    {
      unlink(tmp_path);
      return -errno;
    }

  return OK;
}

int main(int argc, FAR char *argv[])
{
  int period;
  int ret;

  if (argc < 2)
    {
      qiban_weather_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "fetch") == 0)
    {
      ret = qiban_weather_fetch_once();
      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "serve") == 0)
    {
      period = argc > 2 ? atoi(argv[2]) : QIBAN_WEATHER_DEFAULT_PERIOD;
      if (period < 60)
        {
          period = 60;
        }

      for (; ; )
        {
          ret = qiban_weather_fetch_once();
          if (ret < 0)
            {
              fprintf(stderr, "[weather] fetch failed: %d\n", -ret);
            }

          sleep(period);
        }
    }

  if (strcmp(argv[1], "config") == 0)
    {
      ret = qiban_weather_write_config(argc, argv);
      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  qiban_weather_usage(argv[0]);
  return EXIT_FAILURE;
}
