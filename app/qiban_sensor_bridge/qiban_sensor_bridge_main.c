/****************************************************************************
 * qiban_sensor_bridge_main.c
 *
 * Board-side bridge that ingests external key=value telemetry lines and
 * writes them into /data/qiban_inputs/ for qiban_vehicle_service.
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_SENSOR_BRIDGE_DEFAULT_INPUT   "/dev/ttyS1"
#define QIBAN_SENSOR_BRIDGE_INPUT_DIR       "/data/qiban_inputs"
#define QIBAN_SENSOR_BRIDGE_LOCATION_PATH   "/data/qiban_location_state.json"
#define QIBAN_SENSOR_BRIDGE_TEMP_SUFFIX     ".tmp"
#define QIBAN_SENSOR_BRIDGE_LINE_SIZE       256

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

struct qiban_input_mapping_s
{
  FAR const char *key;
  FAR const char *path;
  int minimum;
  int maximum;
};

static const struct qiban_input_mapping_s g_qiban_input_mappings[] =
{
  {"speed_kmh", QIBAN_SENSOR_BRIDGE_INPUT_DIR "/speed_kmh", 0, 120},
  {"battery_percent", QIBAN_SENSOR_BRIDGE_INPUT_DIR "/battery_percent", 0, 100},
  {"nav_remaining_m", QIBAN_SENSOR_BRIDGE_INPUT_DIR "/nav_remaining_m", 0, 999999},
  {"ride_duration_min", QIBAN_SENSOR_BRIDGE_INPUT_DIR "/ride_duration_min", 0, 24 * 60},
  {"total_distance_km_x10", QIBAN_SENSOR_BRIDGE_INPUT_DIR "/total_distance_km_x10", 0, 999999}
};

static FAR char *qiban_trim(FAR char *text)
{
  FAR char *end;

  while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
    {
      text++;
    }

  if (*text == '\0')
    {
      return text;
    }

  end = text + strlen(text) - 1;
  while (end > text &&
         (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
    {
      *end = '\0';
      end--;
    }

  return text;
}

static int qiban_parse_int(FAR const char *text, FAR int *out_value)
{
  FAR char *endptr;
  long value;

  if (text == NULL || *text == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  value = strtol(text, &endptr, 10);
  if (text == endptr || errno != 0)
    {
      return -EINVAL;
    }

  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' ||
         *endptr == '\r')
    {
      endptr++;
    }

  if (*endptr != '\0')
    {
      return -EINVAL;
    }

  if (value < INT_MIN || value > INT_MAX)
    {
      return -ERANGE;
    }

  *out_value = (int)value;
  return OK;
}

static int qiban_parse_double(FAR const char *text, FAR double *out_value)
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

  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' ||
         *endptr == '\r')
    {
      endptr++;
    }

  if (*endptr != '\0')
    {
      return -EINVAL;
    }

  *out_value = value;
  return OK;
}

static FAR const struct qiban_input_mapping_s *
qiban_find_mapping(FAR const char *key)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_qiban_input_mappings) /
                  sizeof(g_qiban_input_mappings[0]); i++)
    {
      if (strcmp(key, g_qiban_input_mappings[i].key) == 0)
        {
          return &g_qiban_input_mappings[i];
        }
    }

  return NULL;
}

static int qiban_ensure_input_dir(void)
{
  int ret;

  ret = mkdir("/data", 0777);
  if (ret < 0 && errno != EEXIST)
    {
      fprintf(stderr, "failed to create /data: %d\n", errno);
      return -errno;
    }

  ret = mkdir(QIBAN_SENSOR_BRIDGE_INPUT_DIR, 0777);
  if (ret < 0 && errno != EEXIST)
    {
      fprintf(stderr, "failed to create %s: %d\n",
              QIBAN_SENSOR_BRIDGE_INPUT_DIR, errno);
      return -errno;
    }

  return OK;
}

static int qiban_write_input_value(FAR const struct qiban_input_mapping_s *mapping,
                                   int value)
{
  FAR FILE *fp;
  char temp_path[PATH_MAX];
  int close_result;
  int written;

  if (mapping == NULL)
    {
      return -EINVAL;
    }

  if (value < mapping->minimum || value > mapping->maximum)
    {
      fprintf(stderr, "value out of range for %s: %d\n",
              mapping->key, value);
      return -ERANGE;
    }

  written = snprintf(temp_path, sizeof(temp_path), "%s%s",
                     mapping->path, QIBAN_SENSOR_BRIDGE_TEMP_SUFFIX);
  if (written < 0 || written >= (int)sizeof(temp_path))
    {
      return -ENAMETOOLONG;
    }

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      fprintf(stderr, "failed to open %s: %d\n", temp_path, errno);
      return -errno;
    }

  if (fprintf(fp, "%d\n", value) < 0 || ferror(fp) != 0)
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
      fprintf(stderr, "failed to close %s\n", temp_path);
      return -EIO;
    }

  if (rename(temp_path, mapping->path) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, mapping->path, errno);
      return -errno;
    }

  printf("updated %s=%d -> %s\n", mapping->key, value, mapping->path);
  return OK;
}

static int qiban_write_location_state(double longitude, double latitude,
                                      int accuracy_m)
{
  FAR FILE *fp;
  char temp_path[PATH_MAX];
  int close_result;
  int written;

  if (longitude < -180.0 || longitude > 180.0 ||
      latitude < -90.0 || latitude > 90.0)
    {
      fprintf(stderr, "location out of range: %.6f %.6f\n",
              longitude, latitude);
      return -ERANGE;
    }

  written = snprintf(temp_path, sizeof(temp_path), "%s%s",
                     QIBAN_SENSOR_BRIDGE_LOCATION_PATH,
                     QIBAN_SENSOR_BRIDGE_TEMP_SUFFIX);
  if (written < 0 || written >= (int)sizeof(temp_path))
    {
      return -ENAMETOOLONG;
    }

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      fprintf(stderr, "failed to open %s: %d\n", temp_path, errno);
      return -errno;
    }

  if (fprintf(fp,
              "{\n"
              "  \"schema_version\": 1,\n"
              "  \"source\": \"board:qiban_sensor_bridge\",\n"
              "  \"generated_at_epoch_s\": %ld,\n"
              "  \"longitude\": %.6f,\n"
              "  \"latitude\": %.6f,\n"
              "  \"accuracy_m\": %d\n"
              "}\n",
              (long)time(NULL),
              longitude,
              latitude,
              accuracy_m) < 0 || ferror(fp) != 0)
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
      fprintf(stderr, "failed to close %s\n", temp_path);
      return -EIO;
    }

  if (rename(temp_path, QIBAN_SENSOR_BRIDGE_LOCATION_PATH) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, QIBAN_SENSOR_BRIDGE_LOCATION_PATH, errno);
      return -errno;
    }

  printf("updated location=%.6f,%.6f -> %s\n",
         longitude, latitude, QIBAN_SENSOR_BRIDGE_LOCATION_PATH);
  return OK;
}

static int qiban_apply_kv(FAR const char *key, FAR const char *value_text)
{
  FAR const struct qiban_input_mapping_s *mapping;
  int value;
  int ret;

  mapping = qiban_find_mapping(key);
  if (mapping == NULL)
    {
      fprintf(stderr, "unknown key: %s\n", key);
      return -ENOENT;
    }

  ret = qiban_parse_int(value_text, &value);
  if (ret < 0)
    {
      fprintf(stderr, "invalid integer for %s: %s\n", key, value_text);
      return ret;
    }

  return qiban_write_input_value(mapping, value);
}

static int qiban_process_line(FAR char *line)
{
  FAR char *key;
  FAR char *value;
  FAR char *separator;

  key = qiban_trim(line);
  if (*key == '\0' || *key == '#')
    {
      return OK;
    }

  separator = strchr(key, '=');
  if (separator == NULL)
    {
      fprintf(stderr, "ignored malformed line: %s\n", key);
      return -EINVAL;
    }

  *separator = '\0';
  value = separator + 1;

  key = qiban_trim(key);
  value = qiban_trim(value);
  if (*key == '\0' || *value == '\0')
    {
      fprintf(stderr, "ignored incomplete line\n");
      return -EINVAL;
    }

  return qiban_apply_kv(key, value);
}

static int qiban_serve_stream(FAR FILE *stream, FAR const char *source_name)
{
  char line[QIBAN_SENSOR_BRIDGE_LINE_SIZE];

  while (fgets(line, sizeof(line), stream) != NULL)
    {
      int ret;

      ret = qiban_process_line(line);
      if (ret < 0)
        {
          fprintf(stderr, "source %s: line rejected (%d)\n",
                  source_name, -ret);
        }
    }

  if (ferror(stream) != 0)
    {
      fprintf(stderr, "read error from %s\n", source_name);
      return -EIO;
    }

  return OK;
}

static void qiban_usage(FAR const char *progname)
{
  unsigned int i;

  printf("Usage:\n");
  printf("  %s serve [device|-]\n", progname);
  printf("  %s write <key> <value>\n", progname);
  printf("  %s location <longitude> <latitude> [accuracy_m]\n", progname);
  printf("Default device: %s\n", QIBAN_SENSOR_BRIDGE_DEFAULT_INPUT);
  printf("Accepted keys:\n");

  for (i = 0; i < sizeof(g_qiban_input_mappings) /
                  sizeof(g_qiban_input_mappings[0]); i++)
    {
      printf("  %s -> %s\n",
             g_qiban_input_mappings[i].key,
             g_qiban_input_mappings[i].path);
    }
}

int main(int argc, FAR char *argv[])
{
  FAR const char *command;
  FAR const char *input_path;
  FAR FILE *stream;
  int fd;
  int ret;

  ret = qiban_ensure_input_dir();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  command = argc > 1 ? argv[1] : "serve";

  if (strcmp(command, "write") == 0)
    {
      if (argc < 4)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_apply_kv(argv[2], argv[3]);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(command, "location") == 0)
    {
      double longitude;
      double latitude;
      int accuracy_m = 30;

      if (argc < 4)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_double(argv[2], &longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid longitude: %s\n", argv[2]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_double(argv[3], &latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid latitude: %s\n", argv[3]);
          return EXIT_FAILURE;
        }

      if (argc > 4)
        {
          ret = qiban_parse_int(argv[4], &accuracy_m);
          if (ret < 0)
            {
              fprintf(stderr, "invalid accuracy_m: %s\n", argv[4]);
              return EXIT_FAILURE;
            }
        }

      ret = qiban_write_location_state(longitude, latitude, accuracy_m);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(command, "serve") == 0)
    {
      input_path = argc > 2 ? argv[2] : QIBAN_SENSOR_BRIDGE_DEFAULT_INPUT;
      if (strcmp(input_path, "-") == 0)
        {
          return qiban_serve_stream(stdin, "stdin") < 0 ? EXIT_FAILURE : OK;
        }

      fd = open(input_path, O_RDONLY);
      if (fd < 0)
        {
          fprintf(stderr, "failed to open %s: %d\n", input_path, errno);
          return EXIT_FAILURE;
        }

      stream = fdopen(fd, "r");
      if (stream == NULL)
        {
          fprintf(stderr, "fdopen failed for %s: %d\n", input_path, errno);
          close(fd);
          return EXIT_FAILURE;
        }

      ret = qiban_serve_stream(stream, input_path);
      fclose(stream);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  qiban_usage(argv[0]);
  return EXIT_FAILURE;
}
