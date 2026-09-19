/****************************************************************************
 * qiban_vehicle_service_main.c
 *
 * Board-side telemetry service scaffold for the "Qiban AI" contest project.
 * This starts as a deterministic simulator so UI and Agent integration can
 * move forward before the real GPIO/ADC/UART data path is available.
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
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_DEFAULT_EXPORT_PATH "/data/qiban_vehicle_state.json"
#define QIBAN_DEFAULT_ADC_PATH "/dev/adc0"
#define QIBAN_DEFAULT_SPEED_PATH "/data/qiban_inputs/speed_kmh"
#define QIBAN_DEFAULT_BATTERY_PATH "/data/qiban_inputs/battery_percent"
#define QIBAN_DEFAULT_NAV_PATH "/data/qiban_inputs/nav_remaining_m"
#define QIBAN_DEFAULT_RIDE_DURATION_PATH "/data/qiban_inputs/ride_duration_min"
#define QIBAN_DEFAULT_TOTAL_DISTANCE_PATH "/data/qiban_inputs/total_distance_km_x10"
#define QIBAN_DEFAULT_MONITOR_SAMPLES 10
#define QIBAN_DEFAULT_INTERVAL_MS 1000
#define QIBAN_VEHICLE_STATE_SCHEMA_VERSION 1
#define QIBAN_VEHICLE_STATE_SOURCE "mock:qiban_vehicle_service"
#define QIBAN_VEHICLE_STATE_SOURCE_GPADC "board:gpadc+builtin_motion"
#define QIBAN_VEHICLE_STATE_SOURCE_FILE "board:file-telemetry"
#define QIBAN_VEHICLE_STATE_SOURCE_FILE_GPADC "board:file-telemetry+gpadc"
#define QIBAN_TEMP_FILE_SUFFIX ".tmp"
#define QIBAN_ADC_MAX_VALUE 4095

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

struct qiban_vehicle_state_s
{
  FAR const char *source;
  int speed_kmh;
  int battery_percent;
  int remaining_range_km;
  int ride_duration_min;
  int total_distance_km_x10;
  int nav_remaining_m;
  int alert_overspeed;
  int alert_low_battery;
  int alert_fatigue;
};

enum qiban_provider_flags_e
{
  QIBAN_PROVIDER_FLAG_NONE         = 0,
  QIBAN_PROVIDER_FLAG_FILE_INPUT   = 1 << 0,
  QIBAN_PROVIDER_FLAG_GPADC_INPUT  = 1 << 1
};

enum qiban_field_flags_e
{
  QIBAN_FIELD_FLAG_NONE             = 0,
  QIBAN_FIELD_FLAG_SPEED            = 1 << 0,
  QIBAN_FIELD_FLAG_BATTERY          = 1 << 1,
  QIBAN_FIELD_FLAG_NAV              = 1 << 2,
  QIBAN_FIELD_FLAG_RIDE_DURATION    = 1 << 3,
  QIBAN_FIELD_FLAG_TOTAL_DISTANCE   = 1 << 4
};

static void qiban_state_init(FAR struct qiban_vehicle_state_s *state)
{
  state->source = QIBAN_VEHICLE_STATE_SOURCE;
  state->speed_kmh = 18;
  state->battery_percent = 78;
  state->remaining_range_km = 34;
  state->ride_duration_min = 0;
  state->total_distance_km_x10 = 12;
  state->nav_remaining_m = 4800;
  state->alert_overspeed = 0;
  state->alert_low_battery = 0;
  state->alert_fatigue = 0;
}

static int qiban_clamp_int(int value, int minimum, int maximum)
{
  if (value < minimum)
    {
      return minimum;
    }

  if (value > maximum)
    {
      return maximum;
    }

  return value;
}

static int qiban_try_parse_int(FAR const char *text, FAR int *out_value)
{
  FAR char *endptr;
  long parsed_value;

  if (text == NULL)
    {
      return -EINVAL;
    }

  while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
    {
      text++;
    }

  if (*text == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  parsed_value = strtol(text, &endptr, 10);
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

  *out_value = (int)parsed_value;
  return OK;
}

static int qiban_read_text_file(FAR const char *path, FAR char *buffer,
                                size_t buffer_size)
{
  int fd;
  ssize_t nbytes;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  nbytes = read(fd, buffer, buffer_size - 1);
  close(fd);

  if (nbytes < 0)
    {
      return -errno;
    }

  buffer[nbytes] = '\0';
  return OK;
}

static int qiban_read_int_file(FAR const char *path, FAR int *out_value)
{
  char buffer[64];
  int ret;

  ret = qiban_read_text_file(path, buffer, sizeof(buffer));
  if (ret < 0)
    {
      return ret;
    }

  return qiban_try_parse_int(buffer, out_value);
}

static int qiban_read_adc_sample(FAR const char *path, FAR int *out_value)
{
  struct adc_msg_s sample;
  int fd;
  ssize_t nbytes;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  ret = ioctl(fd, ANIOC_TRIGGER, 0);
  if (ret < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  nbytes = read(fd, &sample, sizeof(sample));
  if (nbytes < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  close(fd);

  if (nbytes != sizeof(sample))
    {
      return -EIO;
    }

  *out_value = sample.am_data;
  return OK;
}

static int qiban_apply_gpadc_battery(FAR struct qiban_vehicle_state_s *state)
{
  int adc_value;
  int ret;

  ret = qiban_read_adc_sample(QIBAN_DEFAULT_ADC_PATH, &adc_value);
  if (ret < 0)
    {
      return ret;
    }

  adc_value = qiban_clamp_int(adc_value, 0, QIBAN_ADC_MAX_VALUE);
  state->battery_percent = (adc_value * 100) / QIBAN_ADC_MAX_VALUE;
  state->remaining_range_km = state->battery_percent / 2;
  return OK;
}

static int qiban_apply_file_inputs(FAR struct qiban_vehicle_state_s *state)
{
  int value;
  int applied_flags = QIBAN_FIELD_FLAG_NONE;

  if (qiban_read_int_file(QIBAN_DEFAULT_SPEED_PATH, &value) == OK)
    {
      state->speed_kmh = qiban_clamp_int(value, 0, 120);
      applied_flags |= QIBAN_FIELD_FLAG_SPEED;
    }

  if (qiban_read_int_file(QIBAN_DEFAULT_BATTERY_PATH, &value) == OK)
    {
      state->battery_percent = qiban_clamp_int(value, 0, 100);
      state->remaining_range_km = state->battery_percent / 2;
      applied_flags |= QIBAN_FIELD_FLAG_BATTERY;
    }

  if (qiban_read_int_file(QIBAN_DEFAULT_NAV_PATH, &value) == OK)
    {
      state->nav_remaining_m = qiban_clamp_int(value, 0, 999999);
      applied_flags |= QIBAN_FIELD_FLAG_NAV;
    }

  if (qiban_read_int_file(QIBAN_DEFAULT_RIDE_DURATION_PATH, &value) == OK)
    {
      state->ride_duration_min = qiban_clamp_int(value, 0, 24 * 60);
      applied_flags |= QIBAN_FIELD_FLAG_RIDE_DURATION;
    }

  if (qiban_read_int_file(QIBAN_DEFAULT_TOTAL_DISTANCE_PATH, &value) == OK)
    {
      state->total_distance_km_x10 = qiban_clamp_int(value, 0, 999999);
      applied_flags |= QIBAN_FIELD_FLAG_TOTAL_DISTANCE;
    }

  return applied_flags;
}

static void qiban_update_source(FAR struct qiban_vehicle_state_s *state,
                                int provider_flags)
{
  if ((provider_flags & QIBAN_PROVIDER_FLAG_FILE_INPUT) != 0 &&
      (provider_flags & QIBAN_PROVIDER_FLAG_GPADC_INPUT) != 0)
    {
      state->source = QIBAN_VEHICLE_STATE_SOURCE_FILE_GPADC;
    }
  else if ((provider_flags & QIBAN_PROVIDER_FLAG_FILE_INPUT) != 0)
    {
      state->source = QIBAN_VEHICLE_STATE_SOURCE_FILE;
    }
  else if ((provider_flags & QIBAN_PROVIDER_FLAG_GPADC_INPUT) != 0)
    {
      state->source = QIBAN_VEHICLE_STATE_SOURCE_GPADC;
    }
  else
    {
      state->source = QIBAN_VEHICLE_STATE_SOURCE;
    }
}

static void qiban_state_step(FAR struct qiban_vehicle_state_s *state, int tick)
{
  static const int speed_pattern[] = {18, 22, 27, 24, 31, 26, 19, 21};
  int pattern_size = sizeof(speed_pattern) / sizeof(speed_pattern[0]);
  int file_input_flags = QIBAN_FIELD_FLAG_NONE;
  int provider_flags = QIBAN_PROVIDER_FLAG_NONE;

  state->speed_kmh = speed_pattern[tick % pattern_size];
  state->ride_duration_min += 1;
  state->total_distance_km_x10 += state->speed_kmh / 12;

  if (state->nav_remaining_m > 0)
    {
      state->nav_remaining_m -= state->speed_kmh * 8;
      if (state->nav_remaining_m < 0)
        {
          state->nav_remaining_m = 0;
        }
    }

  if (tick > 0 && tick % 4 == 0 && state->battery_percent > 5)
    {
      state->battery_percent -= 1;
    }

  if (state->battery_percent > 0)
    {
      state->remaining_range_km = state->battery_percent / 2;
    }

  file_input_flags = qiban_apply_file_inputs(state);
  if (file_input_flags != 0)
    {
      provider_flags |= QIBAN_PROVIDER_FLAG_FILE_INPUT;
    }

  if ((file_input_flags & QIBAN_FIELD_FLAG_BATTERY) == 0 &&
      qiban_apply_gpadc_battery(state) == OK)
    {
      provider_flags |= QIBAN_PROVIDER_FLAG_GPADC_INPUT;
    }

  state->alert_overspeed = state->speed_kmh > 25;
  state->alert_low_battery = state->battery_percent <= 20;
  state->alert_fatigue = state->ride_duration_min >= 45;
  qiban_update_source(state, provider_flags);
}

static void qiban_state_print(FAR const struct qiban_vehicle_state_s *state)
{
  printf("speed=%d km/h, battery=%d%%, range=%d km, ride=%d min, "
         "distance=%d.%d km, nav_remaining=%d m, alerts=[%s%s%s]\n",
         state->speed_kmh,
         state->battery_percent,
         state->remaining_range_km,
         state->ride_duration_min,
         state->total_distance_km_x10 / 10,
         state->total_distance_km_x10 % 10,
         state->nav_remaining_m,
         state->alert_overspeed ? "overspeed " : "",
         state->alert_low_battery ? "low_battery " : "",
         state->alert_fatigue ? "fatigue" : "");
}

static int qiban_state_export_to_file(FAR FILE *fp,
                                      FAR const struct qiban_vehicle_state_s *state)
{
  time_t generated_at_epoch_s;

  generated_at_epoch_s = time(NULL);
  if (generated_at_epoch_s < 0)
    {
      generated_at_epoch_s = 0;
    }

  fprintf(fp,
          "{\n"
          "  \"schema_version\": %d,\n"
          "  \"source\": \"%s\",\n"
          "  \"generated_at_epoch_s\": %ld,\n"
          "  \"speed_kmh\": %d,\n"
          "  \"battery_percent\": %d,\n"
          "  \"remaining_range_km\": %d,\n"
          "  \"ride_duration_min\": %d,\n"
          "  \"total_distance_km_x10\": %d,\n"
          "  \"nav_remaining_m\": %d,\n"
          "  \"alerts\": {\n"
          "    \"overspeed\": %s,\n"
          "    \"low_battery\": %s,\n"
          "    \"fatigue\": %s\n"
          "  }\n"
          "}\n",
          QIBAN_VEHICLE_STATE_SCHEMA_VERSION,
          state->source,
          (long)generated_at_epoch_s,
          state->speed_kmh,
          state->battery_percent,
          state->remaining_range_km,
          state->ride_duration_min,
          state->total_distance_km_x10,
          state->nav_remaining_m,
          state->alert_overspeed ? "true" : "false",
          state->alert_low_battery ? "true" : "false",
          state->alert_fatigue ? "true" : "false");

  if (ferror(fp) != 0)
    {
      return -EIO;
    }

  return OK;
}

static int qiban_state_export(FAR const char *path,
                              FAR const struct qiban_vehicle_state_s *state)
{
  FAR FILE *fp;
  char temp_path[PATH_MAX];
  int ret;
  int written;

  written = snprintf(temp_path, sizeof(temp_path), "%s%s",
                     path, QIBAN_TEMP_FILE_SUFFIX);
  if (written < 0 || written >= (int)sizeof(temp_path))
    {
      fprintf(stderr, "export path too long: %s\n", path);
      return -ENAMETOOLONG;
    }

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      fprintf(stderr, "failed to open %s: %d\n", temp_path, errno);
      return -errno;
    }

  ret = qiban_state_export_to_file(fp, state);
  if (ret < 0)
    {
      fclose(fp);
      unlink(temp_path);
      fprintf(stderr, "failed to write %s: %d\n", temp_path, -ret);
      return ret;
    }

  if (fclose(fp) != 0)
    {
      fprintf(stderr, "failed to close %s: %d\n", temp_path, errno);
      unlink(temp_path);
      return -errno;
    }

  if (rename(temp_path, path) != 0)
    {
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, path, errno);
      unlink(temp_path);
      return -errno;
    }

  return ret;
}

static int qiban_parse_positive_int(FAR const char *text, int fallback)
{
  int parsed_value;

  if (text == NULL || *text == '\0')
    {
      return fallback;
    }

  parsed_value = atoi(text);
  if (parsed_value <= 0)
    {
      return fallback;
    }

  return parsed_value;
}

static int qiban_is_integer_string(FAR const char *text)
{
  FAR const char *cursor = text;

  if (cursor == NULL || *cursor == '\0')
    {
      return 0;
    }

  while (*cursor != '\0')
    {
      if (*cursor < '0' || *cursor > '9')
        {
          return 0;
        }

      cursor++;
    }

  return 1;
}

static int qiban_has_arg(int argc, FAR char *argv[], FAR const char *expected)
{
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], expected) == 0)
        {
          return 1;
        }
    }

  return 0;
}

static int qiban_run_export_loop(FAR struct qiban_vehicle_state_s *state,
                                 FAR const char *path,
                                 int interval_ms,
                                 int samples)
{
  int tick_value = 0;

  while (samples < 0 || tick_value < samples)
    {
      int ret;

      qiban_state_step(state, tick_value);
      ret = qiban_state_export(path, state);
      if (ret < 0)
        {
          return ret;
        }

      tick_value++;
      if (samples >= 0 && tick_value >= samples)
        {
          break;
        }

      usleep((unsigned int)interval_ms * 1000U);
    }

  return OK;
}

static void qiban_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s once\n", progname);
  printf("  %s once --verbose\n", progname);
  printf("  %s monitor [samples] [interval_ms] [--verbose]\n", progname);
  printf("  %s export [path]\n", progname);
  printf("  %s serve [path] [interval_ms] [--verbose]\n", progname);
  printf("ADC input: tries %s for battery percent before falling back to mock\n",
         QIBAN_DEFAULT_ADC_PATH);
  printf("Optional file inputs:\n");
  printf("  speed:        %s\n", QIBAN_DEFAULT_SPEED_PATH);
  printf("  battery:      %s\n", QIBAN_DEFAULT_BATTERY_PATH);
  printf("  navigation:   %s\n", QIBAN_DEFAULT_NAV_PATH);
  printf("  ride minutes: %s\n", QIBAN_DEFAULT_RIDE_DURATION_PATH);
  printf("  total km x10: %s\n", QIBAN_DEFAULT_TOTAL_DISTANCE_PATH);
}

int main(int argc, FAR char *argv[])
{
  struct qiban_vehicle_state_s state;
  FAR const char *command;
  int verbose;

  qiban_state_init(&state);

  command = argc > 1 ? argv[1] : "once";
  verbose = qiban_has_arg(argc, argv, "--verbose");

  if (strcmp(command, "once") == 0)
    {
      qiban_state_step(&state, 0);
      if (verbose)
        {
          qiban_state_print(&state);
        }
      return OK;
    }

  if (strcmp(command, "monitor") == 0)
    {
      int samples =
        argc > 2 ? qiban_parse_positive_int(argv[2],
                                            QIBAN_DEFAULT_MONITOR_SAMPLES)
                 : QIBAN_DEFAULT_MONITOR_SAMPLES;
      int interval_ms =
        argc > 3 ? qiban_parse_positive_int(argv[3],
                                            QIBAN_DEFAULT_INTERVAL_MS)
                 : QIBAN_DEFAULT_INTERVAL_MS;
      int i;

      for (i = 0; i < samples; i++)
        {
          qiban_state_step(&state, i);
          if (verbose)
            {
              printf("[sample %d] ", i + 1);
              qiban_state_print(&state);
            }
          usleep((unsigned int)interval_ms * 1000U);
        }

      return OK;
    }

  if (strcmp(command, "export") == 0)
    {
      FAR const char *path =
        argc > 2 ? argv[2] : QIBAN_DEFAULT_EXPORT_PATH;
      int ret;

      qiban_state_step(&state, 0);
      ret = qiban_state_export(path, &state);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("exported board-side state to %s\n", path);
      return OK;
    }

  if (strcmp(command, "serve") == 0)
    {
      FAR const char *path = QIBAN_DEFAULT_EXPORT_PATH;
      int interval_ms = QIBAN_DEFAULT_INTERVAL_MS;
      int ret;

      if (argc > 2)
        {
          if (qiban_is_integer_string(argv[2]) != 0)
            {
              interval_ms = qiban_parse_positive_int(argv[2],
                                                     QIBAN_DEFAULT_INTERVAL_MS);
            }
          else
            {
              path = argv[2];
            }
        }

      if (argc > 3)
        {
          interval_ms = qiban_parse_positive_int(argv[3],
                                                 QIBAN_DEFAULT_INTERVAL_MS);
        }

      if (verbose)
        {
          printf("serving board-side state to %s every %d ms\n",
                 path, interval_ms);
        }

      ret = qiban_run_export_loop(&state, path, interval_ms, -1);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      return OK;
    }

  qiban_usage(argv[0]);
  return EXIT_FAILURE;
}
