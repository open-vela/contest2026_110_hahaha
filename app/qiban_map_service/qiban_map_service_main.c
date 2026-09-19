/****************************************************************************
 * qiban_map_service_main.c
 *
 * Board-side map cache service for the "Qiban AI" contest project.
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_MAP_DIR              "/data/qiban_map"
#define QIBAN_MAP_IMAGE_PATH       QIBAN_MAP_DIR "/current.png"
#define QIBAN_MAP_STATE_PATH       QIBAN_MAP_DIR "/state.json"
#define QIBAN_MAP_SESSION_PATH     QIBAN_MAP_DIR "/session.json"
#define QIBAN_MAP_LOCK_PATH        QIBAN_MAP_DIR "/render.lock"
#define QIBAN_OFFLINE_ROOT_DIR     "/sdcard"
#define QIBAN_OFFLINE_TILE_DIR     QIBAN_OFFLINE_ROOT_DIR "/map"
#define QIBAN_OFFLINE_NAV_DIR      QIBAN_OFFLINE_ROOT_DIR "/nav"
#define QIBAN_OFFLINE_GRAPH_PATH   QIBAN_OFFLINE_NAV_DIR "/road_graph.bin"
#define QIBAN_MAP_DEFAULT_TITLE    "Map"
#define QIBAN_MAP_DEFAULT_STATUS   "waiting for download"
#define QIBAN_MAP_AMAP_TITLE       "Amap"
#define QIBAN_MAP_ROUTE_TITLE      "Route"
#define QIBAN_MAP_OFFLINE_TITLE    "Offline Map"
#define QIBAN_MAP_AMAP_KEY_ENV     "QIBAN_AMAP_KEY"
#define QIBAN_MAP_AMAP_URL_SIZE    512
#define QIBAN_MAP_AMAP_MIN_ZOOM    1
#define QIBAN_MAP_AMAP_MAX_ZOOM    17
#define QIBAN_MAP_OFFLINE_MIN_ZOOM 8
#define QIBAN_MAP_OFFLINE_MAX_ZOOM 18
#define QIBAN_MAP_AMAP_SIZE        "240*320"
#define QIBAN_MAP_AMAP_SCALE       1
#define QIBAN_MAP_JSON_SIZE        1024
#define QIBAN_MAP_COPY_BUFFER      1024
#define QIBAN_MAP_TILE_STYLE       "streets-v2"
#define QIBAN_MAP_TILE_URL_SIZE    512
#define QIBAN_MAP_TILE_PATH_SIZE   256
#define QIBAN_MAP_DEFAULT_TILE_MIN_LON 112.88000000
#define QIBAN_MAP_DEFAULT_TILE_MIN_LAT 28.17000000
#define QIBAN_MAP_DEFAULT_TILE_MAX_LON 113.02000000
#define QIBAN_MAP_DEFAULT_TILE_MAX_LAT 28.31000000
#define QIBAN_MAP_DEFAULT_TILE_MIN_ZOOM 8
#define QIBAN_MAP_DEFAULT_TILE_MAX_ZOOM 18
#define QIBAN_MAP_DEFAULT_LONGITUDE 112.938814
#define QIBAN_MAP_DEFAULT_LATITUDE  28.228209
#define QIBAN_MAP_DEFAULT_ZOOM      17

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

struct qiban_map_state_s
{
  FAR const char *source;
  FAR const char *title;
  FAR const char *status;
  FAR const char *mode;
  FAR const char *image_path;
  double center_longitude;
  double center_latitude;
  int zoom;
  bool has_route;
  double route_start_longitude;
  double route_start_latitude;
  double route_end_longitude;
  double route_end_latitude;
  int64_t updated_at;
};

struct qiban_map_session_s
{
  double center_longitude;
  double center_latitude;
  int zoom;
  bool has_route;
  double route_start_longitude;
  double route_start_latitude;
  double route_end_longitude;
  double route_end_latitude;
  char mode[16];
  char title[48];
};

struct qiban_curl_sink_s
{
  FAR FILE *fp;
  size_t bytes_written;
};

struct qiban_map_lock_s
{
  int fd;
  bool locked;
};

static int qiban_mkdir_p(FAR const char *path)
{
  char buffer[PATH_MAX];
  FAR char *cursor;
  size_t length;

  if (path == NULL || *path == '\0')
    {
      return -EINVAL;
    }

  length = strnlen(path, sizeof(buffer));
  if (length == 0 || length >= sizeof(buffer))
    {
      return -ENAMETOOLONG;
    }

  memcpy(buffer, path, length);
  buffer[length] = '\0';

  for (cursor = buffer + 1; *cursor != '\0'; cursor++)
    {
      if (*cursor != '/')
        {
          continue;
        }

      *cursor = '\0';
      if (mkdir(buffer, 0777) < 0 && errno != EEXIST)
        {
          return -errno;
        }

      *cursor = '/';
    }

  if (mkdir(buffer, 0777) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  return OK;
}

static int qiban_ensure_parent_dir(FAR const char *path)
{
  char dir_path[PATH_MAX];
  FAR char *slash;
  size_t length;

  if (path == NULL || *path == '\0')
    {
      return -EINVAL;
    }

  length = strnlen(path, sizeof(dir_path));
  if (length == 0 || length >= sizeof(dir_path))
    {
      return -ENAMETOOLONG;
    }

  memcpy(dir_path, path, length);
  dir_path[length] = '\0';
  slash = strrchr(dir_path, '/');
  if (slash == NULL)
    {
      return -EINVAL;
    }

  if (slash == dir_path)
    {
      dir_path[1] = '\0';
    }
  else
    {
      *slash = '\0';
    }

  return qiban_mkdir_p(dir_path);
}

static int qiban_ensure_map_dir(void)
{
  return qiban_mkdir_p(QIBAN_MAP_DIR);
}

static int qiban_ensure_offline_dir(void)
{
  int ret;

  ret = qiban_mkdir_p(QIBAN_OFFLINE_TILE_DIR);
  if (ret < 0)
    {
      return ret;
    }

  return qiban_mkdir_p(QIBAN_OFFLINE_NAV_DIR);
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

static int64_t qiban_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

static int qiban_map_acquire_lock(FAR struct qiban_map_lock_s *lock)
{
  int fd;

  lock->fd = -1;
  lock->locked = false;

  fd = open(QIBAN_MAP_LOCK_PATH, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    {
      if (errno == EEXIST)
        {
          return -EBUSY;
        }

      return -errno;
    }

  lock->fd = fd;
  lock->locked = true;
  return OK;
}

static void qiban_map_release_lock(FAR struct qiban_map_lock_s *lock)
{
  if (lock == NULL || !lock->locked)
    {
      return;
    }

  close(lock->fd);
  unlink(QIBAN_MAP_LOCK_PATH);
  lock->fd = -1;
  lock->locked = false;
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

static int qiban_parse_coordinate(FAR const char *text, double minimum,
                                  double maximum, FAR double *out_value)
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

  if (value < minimum || value > maximum)
    {
      return -ERANGE;
    }

  *out_value = value;
  return OK;
}

static bool qiban_json_extract_string(FAR const char *json,
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

static bool qiban_json_extract_double(FAR const char *json,
                                      FAR const char *key,
                                      FAR double *out_value)
{
  char pattern[64];
  FAR const char *cursor;
  FAR char *endptr;
  double value;

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
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
         *cursor == '\r')
    {
      cursor++;
    }

  errno = 0;
  value = strtod(cursor, &endptr);
  if (cursor == endptr || errno != 0)
    {
      return false;
    }

  *out_value = value;
  return true;
}

static bool qiban_json_extract_int(FAR const char *json,
                                   FAR const char *key,
                                   FAR int *out_value)
{
  char pattern[64];
  FAR const char *cursor;
  FAR char *endptr;
  long value;

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
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
         *cursor == '\r')
    {
      cursor++;
    }

  errno = 0;
  value = strtol(cursor, &endptr, 10);
  if (cursor == endptr || errno != 0)
    {
      return false;
    }

  if (value < INT_MIN || value > INT_MAX)
    {
      return false;
    }

  *out_value = (int)value;
  return true;
}

static bool qiban_json_extract_bool(FAR const char *json,
                                    FAR const char *key,
                                    FAR bool *out_value)
{
  char pattern[64];
  FAR const char *cursor;

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
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
         *cursor == '\r')
    {
      cursor++;
    }

  if (strncmp(cursor, "true", 4) == 0 || *cursor == '1')
    {
      *out_value = true;
      return true;
    }

  if (strncmp(cursor, "false", 5) == 0 || *cursor == '0')
    {
      *out_value = false;
      return true;
    }

  return false;
}

static void qiban_json_write_string(FAR FILE *fp, FAR const char *text)
{
  FAR const unsigned char *cursor;

  fputc('"', fp);
  for (cursor = (FAR const unsigned char *)text;
       cursor != NULL && *cursor != '\0';
       cursor++)
    {
      if (*cursor == '"' || *cursor == '\\')
        {
          fputc('\\', fp);
          fputc(*cursor, fp);
        }
      else if (*cursor == '\n')
        {
          fputs("\\n", fp);
        }
      else if (*cursor == '\r')
        {
          fputs("\\r", fp);
        }
      else if (*cursor == '\t')
        {
          fputs("\\t", fp);
        }
      else
        {
          fputc(*cursor, fp);
        }
    }

  fputc('"', fp);
}

static int qiban_write_state(FAR const struct qiban_map_state_s *state)
{
  char temp_path[PATH_MAX];
  FAR FILE *fp;
  int ret;

  ret = qiban_make_temp_path(QIBAN_MAP_STATE_PATH, temp_path,
                             sizeof(temp_path));
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

  fputs("{\n  \"schema_version\": 1,\n  \"source\": ", fp);
  qiban_json_write_string(fp, state->source);
  fputs(",\n  \"title\": ", fp);
  qiban_json_write_string(fp, state->title);
  fputs(",\n  \"status\": ", fp);
  qiban_json_write_string(fp, state->status);
  fputs(",\n  \"mode\": ", fp);
  qiban_json_write_string(fp, state->mode);
  fputs(",\n  \"image_path\": ", fp);
  qiban_json_write_string(fp, state->image_path);
  fprintf(fp,
          ",\n  \"center_longitude\": %.6f,\n"
          "  \"center_latitude\": %.6f,\n"
          "  \"zoom\": %d,\n"
          "  \"has_route\": %s,\n"
          "  \"route_start_longitude\": %.6f,\n"
          "  \"route_start_latitude\": %.6f,\n"
          "  \"route_end_longitude\": %.6f,\n"
          "  \"route_end_latitude\": %.6f,\n"
          "  \"updated_at\": %lld\n}\n",
          state->center_longitude,
          state->center_latitude,
          state->zoom,
          state->has_route ? "true" : "false",
          state->route_start_longitude,
          state->route_start_latitude,
          state->route_end_longitude,
          state->route_end_latitude,
          (long long)state->updated_at);

  if (ferror(fp) != 0 || fclose(fp) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to write %s\n", temp_path);
      return -EIO;
    }

  if (rename(temp_path, QIBAN_MAP_STATE_PATH) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, QIBAN_MAP_STATE_PATH, errno);
      return -errno;
    }

  return OK;
}

static int qiban_publish_state(FAR const char *source,
                               FAR const char *title,
                               FAR const char *status,
                               bool with_image,
                               FAR const struct qiban_map_session_s *session)
{
  struct qiban_map_state_s state;

  state.source = source;
  state.title = title != NULL ? title : QIBAN_MAP_DEFAULT_TITLE;
  state.status = status != NULL ? status : QIBAN_MAP_DEFAULT_STATUS;
  state.mode = session != NULL ? session->mode : "point";
  state.image_path = with_image ? QIBAN_MAP_IMAGE_PATH : "";
  state.center_longitude = session != NULL ? session->center_longitude :
                           QIBAN_MAP_DEFAULT_LONGITUDE;
  state.center_latitude = session != NULL ? session->center_latitude :
                          QIBAN_MAP_DEFAULT_LATITUDE;
  state.zoom = session != NULL ? session->zoom : QIBAN_MAP_DEFAULT_ZOOM;
  state.has_route = session != NULL ? session->has_route : false;
  state.route_start_longitude = session != NULL ?
                                session->route_start_longitude :
                                QIBAN_MAP_DEFAULT_LONGITUDE;
  state.route_start_latitude = session != NULL ?
                               session->route_start_latitude :
                               QIBAN_MAP_DEFAULT_LATITUDE;
  state.route_end_longitude = session != NULL ?
                              session->route_end_longitude :
                              QIBAN_MAP_DEFAULT_LONGITUDE;
  state.route_end_latitude = session != NULL ?
                             session->route_end_latitude :
                             QIBAN_MAP_DEFAULT_LATITUDE;
  state.updated_at = qiban_now_ms();
  return qiban_write_state(&state);
}

static void qiban_session_set_defaults(FAR struct qiban_map_session_s *session)
{
  session->center_longitude = QIBAN_MAP_DEFAULT_LONGITUDE;
  session->center_latitude = QIBAN_MAP_DEFAULT_LATITUDE;
  session->zoom = QIBAN_MAP_DEFAULT_ZOOM;
  session->has_route = false;
  session->route_start_longitude = QIBAN_MAP_DEFAULT_LONGITUDE;
  session->route_start_latitude = QIBAN_MAP_DEFAULT_LATITUDE;
  session->route_end_longitude = QIBAN_MAP_DEFAULT_LONGITUDE;
  session->route_end_latitude = QIBAN_MAP_DEFAULT_LATITUDE;
  snprintf(session->mode, sizeof(session->mode), "%s", "point");
  snprintf(session->title, sizeof(session->title), "%s", QIBAN_MAP_AMAP_TITLE);
}

static int qiban_write_session(FAR const struct qiban_map_session_s *session)
{
  char temp_path[PATH_MAX];
  FAR FILE *fp;
  int ret;

  ret = qiban_make_temp_path(QIBAN_MAP_SESSION_PATH, temp_path,
                             sizeof(temp_path));
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

  fputs("{\n  \"schema_version\": 1,\n  \"mode\": ", fp);
  qiban_json_write_string(fp, session->mode);
  fputs(",\n  \"title\": ", fp);
  qiban_json_write_string(fp, session->title);
  fprintf(fp,
          ",\n  \"center_longitude\": %.6f,\n  \"center_latitude\": %.6f,\n"
          "  \"zoom\": %d,\n  \"has_route\": %s,\n"
          "  \"route_start_longitude\": %.6f,\n"
          "  \"route_start_latitude\": %.6f,\n"
          "  \"route_end_longitude\": %.6f,\n"
          "  \"route_end_latitude\": %.6f\n}\n",
          session->center_longitude,
          session->center_latitude,
          session->zoom,
          session->has_route ? "true" : "false",
          session->route_start_longitude,
          session->route_start_latitude,
          session->route_end_longitude,
          session->route_end_latitude);

  if (ferror(fp) != 0 || fclose(fp) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to write %s\n", temp_path);
      return -EIO;
    }

  if (rename(temp_path, QIBAN_MAP_SESSION_PATH) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, QIBAN_MAP_SESSION_PATH, errno);
      return -errno;
    }

  return OK;
}

static int qiban_load_session(FAR struct qiban_map_session_s *session)
{
  char buffer[QIBAN_MAP_JSON_SIZE];
  FAR FILE *fp;
  size_t nread;

  qiban_session_set_defaults(session);

  fp = fopen(QIBAN_MAP_SESSION_PATH, "r");
  if (fp == NULL)
    {
      return -errno;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return -EIO;
    }

  buffer[nread] = '\0';

  qiban_json_extract_string(buffer, "mode", session->mode,
                            sizeof(session->mode));
  qiban_json_extract_string(buffer, "title", session->title,
                            sizeof(session->title));
  qiban_json_extract_double(buffer, "center_longitude",
                            &session->center_longitude);
  qiban_json_extract_double(buffer, "center_latitude",
                            &session->center_latitude);
  qiban_json_extract_int(buffer, "zoom", &session->zoom);
  qiban_json_extract_bool(buffer, "has_route", &session->has_route);
  qiban_json_extract_double(buffer, "route_start_longitude",
                            &session->route_start_longitude);
  qiban_json_extract_double(buffer, "route_start_latitude",
                            &session->route_start_latitude);
  qiban_json_extract_double(buffer, "route_end_longitude",
                            &session->route_end_longitude);
  qiban_json_extract_double(buffer, "route_end_latitude",
                            &session->route_end_latitude);

  return OK;
}

static int qiban_copy_file(FAR const char *src_path, FAR const char *dst_path)
{
  char buffer[QIBAN_MAP_COPY_BUFFER];
  ssize_t nread;
  int src_fd;
  int dst_fd;

  src_fd = open(src_path, O_RDONLY);
  if (src_fd < 0)
    {
      fprintf(stderr, "failed to open %s: %d\n", src_path, errno);
      return -errno;
    }

  dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (dst_fd < 0)
    {
      fprintf(stderr, "failed to open %s: %d\n", dst_path, errno);
      close(src_fd);
      return -errno;
    }

  while ((nread = read(src_fd, buffer, sizeof(buffer))) > 0)
    {
      ssize_t offset = 0;

      while (offset < nread)
        {
          ssize_t nwritten = write(dst_fd, buffer + offset, nread - offset);
          if (nwritten < 0)
            {
              close(src_fd);
              close(dst_fd);
              unlink(dst_path);
              return -errno;
            }

          offset += nwritten;
        }
    }

  close(src_fd);
  close(dst_fd);

  if (nread < 0)
    {
      unlink(dst_path);
      return -errno;
    }

  return OK;
}

static bool qiban_path_looks_like_url(FAR const char *path)
{
  FAR const char *scheme_sep;

  if (path == NULL || *path == '\0')
    {
      return false;
    }

  scheme_sep = strstr(path, "://");
  if (scheme_sep == NULL)
    {
      return false;
    }

  return scheme_sep > path;
}

static size_t qiban_curl_write_cb(FAR void *ptr, size_t size, size_t nmemb,
                                  FAR void *userdata)
{
  struct qiban_curl_sink_s *sink;
  size_t total;

  sink = (struct qiban_curl_sink_s *)userdata;
  total = size * nmemb;

  if (sink == NULL || sink->fp == NULL)
    {
      return 0;
    }

  if (fwrite(ptr, 1, total, sink->fp) != total)
    {
      return 0;
    }

  sink->bytes_written += total;
  return total;
}

static int qiban_fetch_file(FAR const char *url, FAR const char *dst_path)
{
  char temp_path[PATH_MAX];
  struct qiban_curl_sink_s sink;
  CURL *curl;
  CURLcode code;
  int ret;

  ret = qiban_make_temp_path(dst_path, temp_path, sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_parent_dir(dst_path);
  if (ret < 0)
    {
      return ret;
    }

  sink.fp = fopen(temp_path, "wb");
  sink.bytes_written = 0;
  if (sink.fp == NULL)
    {
      fprintf(stderr, "failed to open %s: %d\n", temp_path, errno);
      return -errno;
    }

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      fclose(sink.fp);
      unlink(temp_path);
      fprintf(stderr, "curl_global_init failed: %d\n", code);
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      fclose(sink.fp);
      unlink(temp_path);
      return -ENOMEM;
    }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, qiban_curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_map_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  code = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  if (fclose(sink.fp) != 0)
    {
      unlink(temp_path);
      return -EIO;
    }

  if (code != CURLE_OK || sink.bytes_written == 0)
    {
      unlink(temp_path);
      fprintf(stderr, "download failed for %s: %s\n",
              url, curl_easy_strerror(code));
      return -EIO;
    }

  if (rename(temp_path, dst_path) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, dst_path, errno);
      return -errno;
    }

  return OK;
}

static int qiban_fetch_png(FAR const char *url)
{
  return qiban_fetch_file(url, QIBAN_MAP_IMAGE_PATH);
}

static int qiban_lon_to_tile_x(double longitude, int zoom)
{
  return (int)floor((longitude + 180.0) / 360.0 * (double)(1U << zoom));
}

static int qiban_lat_to_tile_y(double latitude, int zoom)
{
  double lat_rad;
  double scale;

  lat_rad = latitude * M_PI / 180.0;
  scale = (double)(1U << zoom);
  return (int)floor((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI)
                    * 0.5 * scale);
}

static int qiban_build_tile_path(FAR char *buffer, size_t buffer_size,
                                 int zoom, int tile_x, int tile_y)
{
  int written;

  written = snprintf(buffer, buffer_size, "%s/%d/%d/%d/tile.png",
                     QIBAN_OFFLINE_TILE_DIR, zoom, tile_x, tile_y);
  if (written < 0 || written >= (int)buffer_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static int qiban_build_maptiler_tile_url(FAR char *buffer, size_t buffer_size,
                                         FAR const char *key,
                                         FAR const char *style,
                                         int zoom, int tile_x, int tile_y)
{
  int written;

  written = snprintf(buffer, buffer_size,
                     "https://api.maptiler.com/maps/%s/256/%d/%d/%d.png?key=%s",
                     style != NULL ? style : QIBAN_MAP_TILE_STYLE,
                     zoom, tile_x, tile_y, key);
  if (written < 0 || written >= (int)buffer_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static int qiban_fetch_offline_graph(FAR const char *url)
{
  char temp_path[PATH_MAX];
  int ret;

  if (qiban_path_looks_like_url(url))
    {
      return qiban_fetch_file(url, QIBAN_OFFLINE_GRAPH_PATH);
    }

  ret = qiban_make_temp_path(QIBAN_OFFLINE_GRAPH_PATH, temp_path,
                             sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_parent_dir(QIBAN_OFFLINE_GRAPH_PATH);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_copy_file(url, temp_path);
  if (ret < 0)
    {
      unlink(temp_path);
      return ret;
    }

  if (rename(temp_path, QIBAN_OFFLINE_GRAPH_PATH) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, QIBAN_OFFLINE_GRAPH_PATH, errno);
      return -errno;
    }

  return OK;
}

static int qiban_fetch_maptiler_tiles(FAR const char *key,
                                      FAR const char *style,
                                      double min_longitude,
                                      double min_latitude,
                                      double max_longitude,
                                      double max_latitude,
                                      int zoom_min,
                                      int zoom_max)
{
  char tile_path[QIBAN_MAP_TILE_PATH_SIZE];
  char url[QIBAN_MAP_TILE_URL_SIZE];
  int downloaded;
  int tile_x;
  int tile_x_end;
  int tile_y;
  int tile_y_end;
  int ret;
  int zoom;

  if (key == NULL || *key == '\0')
    {
      return -EINVAL;
    }

  if (min_longitude >= max_longitude || min_latitude >= max_latitude)
    {
      return -ERANGE;
    }

  if (zoom_min < QIBAN_MAP_OFFLINE_MIN_ZOOM)
    {
      zoom_min = QIBAN_MAP_OFFLINE_MIN_ZOOM;
    }

  if (zoom_max > QIBAN_MAP_OFFLINE_MAX_ZOOM)
    {
      zoom_max = QIBAN_MAP_OFFLINE_MAX_ZOOM;
    }

  if (zoom_min > zoom_max)
    {
      return -ERANGE;
    }

  downloaded = 0;
  for (zoom = zoom_min; zoom <= zoom_max; zoom++)
    {
      tile_x = qiban_lon_to_tile_x(min_longitude, zoom);
      tile_x_end = qiban_lon_to_tile_x(max_longitude, zoom);
      tile_y = qiban_lat_to_tile_y(max_latitude, zoom);
      tile_y_end = qiban_lat_to_tile_y(min_latitude, zoom);
      printf("offline tiles z=%d x=%d..%d y=%d..%d\n",
             zoom, tile_x, tile_x_end, tile_y, tile_y_end);

      for (; tile_x <= tile_x_end; tile_x++)
        {
          int current_y;

          for (current_y = tile_y; current_y <= tile_y_end; current_y++)
            {
              ret = qiban_build_tile_path(tile_path, sizeof(tile_path),
                                          zoom, tile_x, current_y);
              if (ret < 0)
                {
                  return ret;
                }

              ret = qiban_build_maptiler_tile_url(url, sizeof(url), key,
                                                  style, zoom, tile_x,
                                                  current_y);
              if (ret < 0)
                {
                  return ret;
                }

              ret = qiban_fetch_file(url, tile_path);
              if (ret < 0)
                {
                  fprintf(stderr,
                          "failed tile z=%d x=%d y=%d: %d\n",
                          zoom, tile_x, current_y, ret);
                  return ret;
                }

              downloaded++;
            }
        }
    }

  printf("downloaded offline tiles: %d\n", downloaded);
  return OK;
}

static int qiban_fetch_amap(double longitude, double latitude, int zoom,
                            FAR const char *title)
{
  struct qiban_map_session_s session;

  qiban_session_set_defaults(&session);
  session.center_longitude = longitude;
  session.center_latitude = latitude;
  session.zoom = zoom;
  session.has_route = false;
  snprintf(session.mode, sizeof(session.mode), "%s", "point");
  snprintf(session.title, sizeof(session.title), "%s",
           title != NULL ? title : QIBAN_MAP_AMAP_TITLE);
  return qiban_write_session(&session);
}

static double qiban_map_pan_delta(int zoom)
{
  int zoom_factor;

  zoom_factor = QIBAN_MAP_AMAP_MAX_ZOOM - zoom + 1;
  if (zoom_factor < 1)
    {
      zoom_factor = 1;
    }

  return 0.0025 * (double)zoom_factor;
}

static int qiban_render_session(FAR const struct qiban_map_session_s *session)
{
  FAR const char *amap_key;
  char url[QIBAN_MAP_AMAP_URL_SIZE];
  int written;
  int ret;

  if (session->zoom < QIBAN_MAP_AMAP_MIN_ZOOM ||
      session->zoom > QIBAN_MAP_AMAP_MAX_ZOOM)
    {
      fprintf(stderr, "zoom out of range: %d\n", session->zoom);
      return -ERANGE;
    }

  amap_key = getenv(QIBAN_MAP_AMAP_KEY_ENV);
  if (amap_key == NULL || amap_key[0] == '\0')
    {
      fprintf(stderr, "%s is not set\n", QIBAN_MAP_AMAP_KEY_ENV);
      return -EACCES;
    }

  if (session->has_route)
    {
      written = snprintf(
        url, sizeof(url),
        "http://restapi.amap.com/v3/staticmap?"
        "location=%.6f,%.6f&zoom=%d&size=%s&scale=%d&traffic=1&"
        "paths=8,0x2563eb,1,,:%.6f,%.6f;%.6f,%.6f&"
        "markers=mid,0x16a34a,S:%.6f,%.6f%%7Cmid,0xdc2626,E:%.6f,%.6f&key=%s",
        session->center_longitude,
        session->center_latitude,
        session->zoom,
        QIBAN_MAP_AMAP_SIZE,
        QIBAN_MAP_AMAP_SCALE,
        session->route_start_longitude,
        session->route_start_latitude,
        session->route_end_longitude,
        session->route_end_latitude,
        session->route_start_longitude,
        session->route_start_latitude,
        session->route_end_longitude,
        session->route_end_latitude,
        amap_key);
    }
  else
    {
      written = snprintf(
        url, sizeof(url),
        "http://restapi.amap.com/v3/staticmap?"
        "location=%.6f,%.6f&zoom=%d&size=%s&scale=%d&traffic=1&"
        "markers=mid,0xFF0000,A:%.6f,%.6f&key=%s",
        session->center_longitude,
        session->center_latitude,
        session->zoom,
        QIBAN_MAP_AMAP_SIZE,
        QIBAN_MAP_AMAP_SCALE,
        session->center_longitude,
        session->center_latitude,
        amap_key);
    }

  if (written < 0 || written >= (int)sizeof(url))
    {
      return -ENAMETOOLONG;
    }

  ret = qiban_fetch_png(url);
  if (ret < 0)
    {
      qiban_publish_state(session->has_route ? "wifi:amap-route" : "wifi:amap",
                          session->title,
                          session->has_route ? "route download failed"
                                             : "amap download failed",
                          false,
                          session);
      return ret;
    }

  return qiban_publish_state(session->has_route ? "wifi:amap-route"
                                                : "wifi:amap",
                             session->title,
                             session->has_route ? "route map ready"
                                                : "amap map ready",
                             true,
                             session);
}

static int qiban_fetch_current_session(void)
{
  struct qiban_map_session_s session;
  struct qiban_map_lock_s lock;
  int ret;

  ret = qiban_map_acquire_lock(&lock);
  if (ret < 0)
    {
      qiban_publish_state("wifi:amap", "Map", "map busy", false, NULL);
      return ret;
    }

  ret = qiban_load_session(&session);
  if (ret < 0)
    {
      qiban_session_set_defaults(&session);
      ret = qiban_write_session(&session);
      if (ret < 0)
        {
          qiban_map_release_lock(&lock);
          return ret;
        }
    }

  ret = qiban_render_session(&session);
  qiban_map_release_lock(&lock);
  return ret;
}

static int qiban_fetch_route(double start_longitude, double start_latitude,
                             double end_longitude, double end_latitude,
                             int zoom, FAR const char *title)
{
  struct qiban_map_session_s session;

  qiban_session_set_defaults(&session);
  session.center_longitude = (start_longitude + end_longitude) / 2.0;
  session.center_latitude = (start_latitude + end_latitude) / 2.0;
  session.zoom = zoom;
  session.has_route = true;
  session.route_start_longitude = start_longitude;
  session.route_start_latitude = start_latitude;
  session.route_end_longitude = end_longitude;
  session.route_end_latitude = end_latitude;
  snprintf(session.mode, sizeof(session.mode), "%s", "route");
  snprintf(session.title, sizeof(session.title), "%s",
           title != NULL ? title : QIBAN_MAP_ROUTE_TITLE);
  return qiban_write_session(&session);
}

static int qiban_pan_session(FAR const char *direction, int step_count)
{
  struct qiban_map_session_s session;
  double delta;
  int ret;

  ret = qiban_load_session(&session);
  if (ret < 0)
    {
      return ret;
    }

  if (step_count < 1)
    {
      step_count = 1;
    }

  delta = qiban_map_pan_delta(session.zoom) * (double)step_count;
  if (strcmp(direction, "left") == 0)
    {
      session.center_longitude -= delta;
    }
  else if (strcmp(direction, "right") == 0)
    {
      session.center_longitude += delta;
    }
  else if (strcmp(direction, "up") == 0)
    {
      session.center_latitude += delta;
    }
  else if (strcmp(direction, "down") == 0)
    {
      session.center_latitude -= delta;
    }
  else
    {
      return -EINVAL;
    }

  if (session.center_longitude < -180.0)
    {
      session.center_longitude = -180.0;
    }
  else if (session.center_longitude > 180.0)
    {
      session.center_longitude = 180.0;
    }

  if (session.center_latitude < -90.0)
    {
      session.center_latitude = -90.0;
    }
  else if (session.center_latitude > 90.0)
    {
      session.center_latitude = 90.0;
    }

  ret = qiban_write_session(&session);
  if (ret < 0)
    {
      return ret;
    }

  return qiban_render_session(&session);
}

static int qiban_zoom_session(FAR const char *direction, int step_count)
{
  struct qiban_map_session_s session;
  int ret;

  ret = qiban_load_session(&session);
  if (ret < 0)
    {
      return ret;
    }

  if (step_count < 1)
    {
      step_count = 1;
    }

  if (strcmp(direction, "in") == 0)
    {
      session.zoom += step_count;
    }
  else if (strcmp(direction, "out") == 0)
    {
      session.zoom -= step_count;
    }
  else
    {
      return -EINVAL;
    }

  if (session.zoom < QIBAN_MAP_AMAP_MIN_ZOOM)
    {
      session.zoom = QIBAN_MAP_AMAP_MIN_ZOOM;
    }
  else if (session.zoom > QIBAN_MAP_AMAP_MAX_ZOOM)
    {
      session.zoom = QIBAN_MAP_AMAP_MAX_ZOOM;
    }

  ret = qiban_write_session(&session);
  if (ret < 0)
    {
      return ret;
    }

  return qiban_render_session(&session);
}

static int qiban_publish_local_png(FAR const char *src_path)
{
  char temp_path[PATH_MAX];
  struct qiban_map_lock_s lock;
  int ret;

  ret = qiban_map_acquire_lock(&lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_make_temp_path(QIBAN_MAP_IMAGE_PATH, temp_path,
                             sizeof(temp_path));
  if (ret < 0)
    {
      qiban_map_release_lock(&lock);
      return ret;
    }

  ret = qiban_copy_file(src_path, temp_path);
  if (ret < 0)
    {
      qiban_map_release_lock(&lock);
      return ret;
    }

  if (rename(temp_path, QIBAN_MAP_IMAGE_PATH) != 0)
    {
      unlink(temp_path);
      fprintf(stderr, "failed to rename %s to %s: %d\n",
              temp_path, QIBAN_MAP_IMAGE_PATH, errno);
      qiban_map_release_lock(&lock);
      return -errno;
    }

  qiban_map_release_lock(&lock);
  return OK;
}

static int qiban_print_status(void)
{
  char buffer[QIBAN_MAP_JSON_SIZE];
  FAR FILE *fp;
  size_t nread;

  fp = fopen(QIBAN_MAP_STATE_PATH, "r");
  if (fp == NULL)
    {
      fprintf(stderr, "state file not found: %s\n", QIBAN_MAP_STATE_PATH);
      return -errno;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      fprintf(stderr, "state file is empty: %s\n", QIBAN_MAP_STATE_PATH);
      return -EIO;
    }

  buffer[nread] = '\0';
  printf("%s", buffer);
  return OK;
}

static int qiban_clear_map(void)
{
  struct qiban_map_lock_s lock;
  int ret;

  ret = qiban_map_acquire_lock(&lock);
  if (ret < 0)
    {
      return ret;
    }

  unlink(QIBAN_MAP_IMAGE_PATH);
  unlink(QIBAN_MAP_SESSION_PATH);
  ret = qiban_publish_state("board:clear", "Map", "cleared", false, NULL);
  qiban_map_release_lock(&lock);
  return ret;
}

static void qiban_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s fetch <png_url> [title]\n", progname);
  printf("  %s amap <longitude> <latitude> [zoom] [title]\n", progname);
  printf("  %s route <start_lon> <start_lat> <end_lon> <end_lat> [zoom] [title]\n",
         progname);
  printf("  %s pan <left|right|up|down> [step]\n", progname);
  printf("  %s zoom <in|out> [step]\n", progname);
  printf("  %s refresh\n", progname);
  printf("  %s publish <local_png_path> [title]\n", progname);
  printf("  %s offline-graph <bin_url_or_path>\n", progname);
  printf("  %s offline-maptiler <maptiler_key> [style]\n", progname);
  printf("  %s offline-maptiler-area <maptiler_key> <min_lon> <min_lat> <max_lon> <max_lat> [zoom_min] [zoom_max] [style]\n",
         progname);
  printf("  %s status\n", progname);
  printf("  %s clear\n", progname);
  printf("Output image: %s\n", QIBAN_MAP_IMAGE_PATH);
  printf("State file:   %s\n", QIBAN_MAP_STATE_PATH);
  printf("Session file: %s\n", QIBAN_MAP_SESSION_PATH);
  printf("Offline map:  %s\n", QIBAN_OFFLINE_TILE_DIR);
  printf("Offline nav:  %s\n", QIBAN_OFFLINE_GRAPH_PATH);
}

int main(int argc, FAR char *argv[])
{
  FAR const char *command;
  FAR const char *title;
  FAR const char *direction;
  FAR const char *style;
  double end_latitude;
  double end_longitude;
  double latitude;
  double max_latitude;
  double max_longitude;
  double min_latitude;
  double min_longitude;
  double longitude;
  double start_latitude;
  double start_longitude;
  int step_count;
  int zoom;
  int zoom_max;
  int zoom_min;
  int ret;

  ret = qiban_ensure_map_dir();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  ret = qiban_ensure_offline_dir();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      qiban_usage(argv[0]);
      return EXIT_FAILURE;
    }

  command = argv[1];
  if (strcmp(command, "fetch") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      title = argc > 3 ? argv[3] : QIBAN_MAP_DEFAULT_TITLE;
      ret = qiban_fetch_png(argv[2]);
      if (ret < 0)
        {
          qiban_publish_state("wifi:curl", title, "download failed", false,
                              NULL);
          return EXIT_FAILURE;
        }

      ret = qiban_publish_state("wifi:curl", title, "map ready", true, NULL);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("downloaded map to %s\n", QIBAN_MAP_IMAGE_PATH);
      return OK;
    }

  if (strcmp(command, "amap") == 0)
    {
      if (argc < 4)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[2], -180.0, 180.0, &longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid longitude: %s\n", argv[2]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[3], -90.0, 90.0, &latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid latitude: %s\n", argv[3]);
          return EXIT_FAILURE;
        }

      zoom = 15;
      title = QIBAN_MAP_AMAP_TITLE;

      if (argc > 4)
        {
          ret = qiban_parse_int(argv[4], &zoom);
          if (ret < 0)
            {
              title = argv[4];
            }
        }

      if (argc > 5)
        {
          title = argv[5];
        }

      ret = qiban_fetch_amap(longitude, latitude, zoom, title);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      ret = qiban_fetch_current_session();
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("downloaded amap image to %s\n", QIBAN_MAP_IMAGE_PATH);
      return OK;
    }

  if (strcmp(command, "route") == 0)
    {
      if (argc < 6)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[2], -180.0, 180.0, &start_longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid start longitude: %s\n", argv[2]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[3], -90.0, 90.0, &start_latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid start latitude: %s\n", argv[3]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[4], -180.0, 180.0, &end_longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid end longitude: %s\n", argv[4]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[5], -90.0, 90.0, &end_latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid end latitude: %s\n", argv[5]);
          return EXIT_FAILURE;
        }

      zoom = QIBAN_MAP_DEFAULT_ZOOM - 1;
      title = QIBAN_MAP_ROUTE_TITLE;

      if (argc > 6)
        {
          ret = qiban_parse_int(argv[6], &zoom);
          if (ret < 0)
            {
              title = argv[6];
            }
        }

      if (argc > 7)
        {
          title = argv[7];
        }

      ret = qiban_fetch_route(start_longitude, start_latitude,
                              end_longitude, end_latitude,
                              zoom, title);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      ret = qiban_fetch_current_session();
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("downloaded route image to %s\n", QIBAN_MAP_IMAGE_PATH);
      return OK;
    }

  if (strcmp(command, "pan") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      direction = argv[2];
      step_count = 1;
      if (argc > 3)
        {
          qiban_parse_int(argv[3], &step_count);
        }

      ret = qiban_pan_session(direction, step_count);
      if (ret < 0)
        {
          fprintf(stderr, "pan failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("panned map %s\n", direction);
      return OK;
    }

  if (strcmp(command, "zoom") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      direction = argv[2];
      step_count = 1;
      if (argc > 3)
        {
          qiban_parse_int(argv[3], &step_count);
        }

      ret = qiban_zoom_session(direction, step_count);
      if (ret < 0)
        {
          fprintf(stderr, "zoom failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("zoomed map %s\n", direction);
      return OK;
    }

  if (strcmp(command, "refresh") == 0)
    {
      ret = qiban_fetch_current_session();
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("refreshed map image to %s\n", QIBAN_MAP_IMAGE_PATH);
      return OK;
    }

  if (strcmp(command, "publish") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      title = argc > 3 ? argv[3] : QIBAN_MAP_DEFAULT_TITLE;
      ret = qiban_publish_local_png(argv[2]);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      ret = qiban_publish_state("board:file-copy", title, "local map ready",
                                true, NULL);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("published map to %s\n", QIBAN_MAP_IMAGE_PATH);
      return OK;
    }

  if (strcmp(command, "offline-graph") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_fetch_offline_graph(argv[2]);
      if (ret < 0)
        {
          qiban_publish_state("wifi:offline-nav", QIBAN_MAP_OFFLINE_TITLE,
                              "offline graph download failed", false, NULL);
          return EXIT_FAILURE;
        }

      qiban_publish_state("wifi:offline-nav", QIBAN_MAP_OFFLINE_TITLE,
                          "offline graph ready", true, NULL);
      printf("downloaded road graph to %s\n", QIBAN_OFFLINE_GRAPH_PATH);
      return OK;
    }

  if (strcmp(command, "offline-maptiler") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      style = argc > 3 ? argv[3] : QIBAN_MAP_TILE_STYLE;
      ret = qiban_fetch_maptiler_tiles(argv[2], style,
                                       QIBAN_MAP_DEFAULT_TILE_MIN_LON,
                                       QIBAN_MAP_DEFAULT_TILE_MIN_LAT,
                                       QIBAN_MAP_DEFAULT_TILE_MAX_LON,
                                       QIBAN_MAP_DEFAULT_TILE_MAX_LAT,
                                       QIBAN_MAP_DEFAULT_TILE_MIN_ZOOM,
                                       QIBAN_MAP_DEFAULT_TILE_MAX_ZOOM);
      if (ret < 0)
        {
          qiban_publish_state("wifi:offline-tile", QIBAN_MAP_OFFLINE_TITLE,
                              "offline tile download failed", false, NULL);
          return EXIT_FAILURE;
        }

      qiban_publish_state("wifi:offline-tile", QIBAN_MAP_OFFLINE_TITLE,
                          "offline tiles ready", true, NULL);
      printf("downloaded tiles to %s\n", QIBAN_OFFLINE_TILE_DIR);
      return OK;
    }

  if (strcmp(command, "offline-maptiler-area") == 0)
    {
      if (argc < 7)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[3], -180.0, 180.0, &min_longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid min longitude: %s\n", argv[3]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[4], -90.0, 90.0, &min_latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid min latitude: %s\n", argv[4]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[5], -180.0, 180.0, &max_longitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid max longitude: %s\n", argv[5]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_coordinate(argv[6], -90.0, 90.0, &max_latitude);
      if (ret < 0)
        {
          fprintf(stderr, "invalid max latitude: %s\n", argv[6]);
          return EXIT_FAILURE;
        }

      zoom_min = QIBAN_MAP_DEFAULT_TILE_MIN_ZOOM;
      zoom_max = QIBAN_MAP_DEFAULT_TILE_MAX_ZOOM;
      style = QIBAN_MAP_TILE_STYLE;

      if (argc > 7)
        {
          qiban_parse_int(argv[7], &zoom_min);
        }

      if (argc > 8)
        {
          qiban_parse_int(argv[8], &zoom_max);
        }

      if (argc > 9)
        {
          style = argv[9];
        }

      ret = qiban_fetch_maptiler_tiles(argv[2], style,
                                       min_longitude, min_latitude,
                                       max_longitude, max_latitude,
                                       zoom_min, zoom_max);
      if (ret < 0)
        {
          qiban_publish_state("wifi:offline-tile", QIBAN_MAP_OFFLINE_TITLE,
                              "offline tile download failed", false, NULL);
          return EXIT_FAILURE;
        }

      qiban_publish_state("wifi:offline-tile", QIBAN_MAP_OFFLINE_TITLE,
                          "offline tiles ready", true, NULL);
      printf("downloaded tiles to %s\n", QIBAN_OFFLINE_TILE_DIR);
      return OK;
    }

  if (strcmp(command, "status") == 0)
    {
      return qiban_print_status() < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(command, "clear") == 0)
    {
      return qiban_clear_map() < 0 ? EXIT_FAILURE : OK;
    }

  qiban_usage(argv[0]);
  return EXIT_FAILURE;
}
