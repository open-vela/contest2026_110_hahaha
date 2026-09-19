/****************************************************************************
 * qiban_voice_service_main.c
 *
 * Stage-2 board-side voice orchestration skeleton for the "Qiban AI"
 * contest project.
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#if defined(CONFIG_SYSTEM_NXPLAYER) && defined(CONFIG_SYSTEM_NXRECORDER) && \
    defined(CONFIG_AUDIO_FORMAT_PCM) && !defined(CONFIG_AUDIO_EXCLUDE_STOP)
#  define QIBAN_VOICE_HAVE_DIRECT_AUDIO 1
#  include <nuttx/audio/audio.h>
#  include <system/nxplayer.h>
#  include <system/nxrecorder.h>
#else
#  define QIBAN_VOICE_HAVE_DIRECT_AUDIO 0
#endif

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

#define QIBAN_VOICE_DATA_DIR               "/data"
#define QIBAN_VOICE_RUNTIME_DIR            QIBAN_VOICE_DATA_DIR "/qiban_voice"
#define QIBAN_VOICE_RECORDINGS_DIR         QIBAN_VOICE_RUNTIME_DIR "/recordings"
#define QIBAN_VOICE_SCRIPTS_DIR            QIBAN_VOICE_RUNTIME_DIR "/scripts"
#define QIBAN_VOICE_ASR_DIR                QIBAN_VOICE_RUNTIME_DIR "/asr"
#define QIBAN_VOICE_TTS_DIR                QIBAN_VOICE_RUNTIME_DIR "/tts"
#define QIBAN_VOICE_LAST_INTENT_PATH       QIBAN_VOICE_DATA_DIR "/qiban_voice_last_intent.json"
#define QIBAN_VOICE_LAST_AUDIO_PATH        QIBAN_VOICE_DATA_DIR "/qiban_voice_last_audio.json"
#define QIBAN_VOICE_LAST_TTS_PATH          QIBAN_VOICE_DATA_DIR "/qiban_voice_last_tts.json"
#define QIBAN_VOICE_SERVER_CONFIG_PATH     QIBAN_VOICE_DATA_DIR "/qiban_voice_server_config.json"
#define QIBAN_VOICE_LAST_ASR_JOB_PATH      QIBAN_VOICE_DATA_DIR "/qiban_voice_last_asr_job.json"
#define QIBAN_VOICE_LAST_TTS_JOB_PATH      QIBAN_VOICE_DATA_DIR "/qiban_voice_last_tts_job.json"
#define QIBAN_VOICE_NAV_STATE_PATH         QIBAN_VOICE_DATA_DIR "/qiban_nav_state.json"
#define QIBAN_VOICE_ASR_REQUEST_PATH       QIBAN_VOICE_ASR_DIR "/last_request.json"
#define QIBAN_VOICE_TTS_TEXT_PATH          QIBAN_VOICE_TTS_DIR "/last_request.txt"
#define QIBAN_VOICE_TTS_PLACEHOLDER_PCM    QIBAN_VOICE_TTS_DIR "/last_tts_16k_s16_mono.pcm"
#define QIBAN_VOICE_TTS_SERVER_DOWNLOAD    QIBAN_VOICE_TTS_DIR "/server_result_download.pcm"
#define QIBAN_VOICE_RECORD_DEVICE_ENV      "QIBAN_VOICE_RECORD_DEVICE"
#define QIBAN_VOICE_PLAYBACK_DEVICE_ENV    "QIBAN_VOICE_PLAYBACK_DEVICE"
#define QIBAN_VOICE_DEFAULT_RECORD_DEVICE  "/dev/audio/pcm0c"
#define QIBAN_VOICE_DEFAULT_PLAYBACK_DEVICE "/dev/audio/pcm0p"
#define QIBAN_VOICE_FALLBACK_RECORD_DEVICE "/dev/audio/pcm1"
#define QIBAN_VOICE_FALLBACK_PLAYBACK_DEVICE "/dev/audio/pcm1"
#define QIBAN_VOICE_SCHEMA_VERSION         2
#define QIBAN_VOICE_SOURCE                 "board:qiban_voice_service"
#define QIBAN_VOICE_TEXT_SIZE              256
#define QIBAN_VOICE_JSON_SIZE              2048
#define QIBAN_VOICE_CMD_SIZE               1024
#define QIBAN_VOICE_COPY_BUFFER_SIZE       1024
#define QIBAN_VOICE_SERVER_URL_SIZE        384
#define QIBAN_VOICE_SERVER_RESPONSE_SIZE   4096
#define QIBAN_VOICE_DEFAULT_RECORD_SECONDS 4
#define QIBAN_VOICE_SAMPLE_RATE_HZ         16000
#define QIBAN_VOICE_CHANNELS               1
#define QIBAN_VOICE_BITS_PER_SAMPLE        16

enum qiban_voice_action_e
{
  QIBAN_VOICE_ACTION_UNKNOWN = 0,
  QIBAN_VOICE_ACTION_NAVIGATE,
  QIBAN_VOICE_ACTION_CLEAR_NAV,
  QIBAN_VOICE_ACTION_ZOOM_IN,
  QIBAN_VOICE_ACTION_ZOOM_OUT,
  QIBAN_VOICE_ACTION_REFRESH_MAP
};

struct qiban_voice_alias_s
{
  FAR const char *spoken_name;
  FAR const char *canonical_name;
  bool has_coordinates;
  double longitude;
  double latitude;
};

struct qiban_voice_parse_result_s
{
  enum qiban_voice_action_e action;
  char target[QIBAN_VOICE_TEXT_SIZE];
};

struct qiban_curl_buffer_s
{
  FAR char *data;
  size_t capacity;
  size_t length;
  bool truncated;
};

struct qiban_json_workspace_s
{
  char escaped_a[PATH_MAX * 2];
  char escaped_b[PATH_MAX * 2];
  char escaped_c[PATH_MAX * 2];
  char escaped_d[PATH_MAX * 2];
  char escaped_e[PATH_MAX * 2];
  char escaped_f[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_g[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_h[QIBAN_VOICE_TEXT_SIZE * 2];
  char buffer[QIBAN_VOICE_JSON_SIZE];
};

static bool qiban_json_extract_string(FAR const char *json,
                                      FAR const char *key,
                                      FAR char *buffer,
                                      size_t buffer_size);
static bool qiban_json_extract_int(FAR const char *json,
                                   FAR const char *key,
                                   FAR int *value);
static int qiban_load_text_file(FAR const char *path,
                                FAR char *buffer,
                                size_t buffer_size);
static int qiban_import_tts_pcm(FAR const char *source_path);
static int qiban_prepare_navigation_announcement(void);
static int qiban_import_asr_text(FAR char *text);

static const struct qiban_voice_alias_s g_qiban_voice_aliases[] =
{
  {"软件园二期", "软件园二期", true, 116.508789, 39.984674},
  {"软件园一期", "软件园一期", true, 116.498621, 39.988219},
  {"中关村壹号", "中关村壹号", true, 116.470885, 40.044772},
  {"清华大学", "清华大学", true, 116.326980, 40.003202},
  {"清华东门", "清华东门", true, 116.331457, 40.003573},
  {"北京大学东门", "北京大学东门", true, 116.316472, 39.992924},
  {"北大东门", "北京大学东门", true, 116.316472, 39.992924},
  {"北京大学", "北京大学", true, 116.310000, 39.992000},
  {"北大", "北京大学", true, 116.310000, 39.992000}
};

/* Reuse large scratch buffers to keep the task stack small on NuttX. */

static struct qiban_json_workspace_s g_qiban_json_workspace;
static char g_qiban_server_response[QIBAN_VOICE_SERVER_RESPONSE_SIZE];
static char g_qiban_server_request_json[QIBAN_VOICE_JSON_SIZE];

static int qiban_ensure_dir(FAR const char *path)
{
  int ret;

  ret = mkdir(path, 0777);
  if (ret < 0 && errno != EEXIST)
    {
      fprintf(stderr, "failed to create %s: %d\n", path, errno);
      return -errno;
    }

  return OK;
}

static int qiban_ensure_runtime_tree(void)
{
  int ret;

  ret = qiban_ensure_dir(QIBAN_VOICE_DATA_DIR);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_dir(QIBAN_VOICE_RUNTIME_DIR);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_dir(QIBAN_VOICE_RECORDINGS_DIR);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_dir(QIBAN_VOICE_SCRIPTS_DIR);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_ensure_dir(QIBAN_VOICE_ASR_DIR);
  if (ret < 0)
    {
      return ret;
    }

  return qiban_ensure_dir(QIBAN_VOICE_TTS_DIR);
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

static int qiban_write_text_atomic(FAR const char *path,
                                   FAR const char *content)
{
  char temp_path[PATH_MAX];
  FAR FILE *fp;
  int ret;

  ret = qiban_make_temp_path(path, temp_path, sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  if (fputs(content, fp) == EOF || ferror(fp) != 0)
    {
      fclose(fp);
      unlink(temp_path);
      return -EIO;
    }

  if (fclose(fp) != 0)
    {
      unlink(temp_path);
      return -EIO;
    }

  if (rename(temp_path, path) != 0)
    {
      unlink(temp_path);
      return -errno;
    }

  return OK;
}

static int qiban_copy_file_atomic(FAR const char *src_path,
                                  FAR const char *dst_path)
{
  char temp_path[PATH_MAX];
  char buffer[QIBAN_VOICE_COPY_BUFFER_SIZE];
  FAR FILE *src_fp;
  FAR FILE *dst_fp;
  size_t nread;
  int ret;

  ret = qiban_make_temp_path(dst_path, temp_path, sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  src_fp = fopen(src_path, "rb");
  if (src_fp == NULL)
    {
      return -errno;
    }

  dst_fp = fopen(temp_path, "wb");
  if (dst_fp == NULL)
    {
      fclose(src_fp);
      return -errno;
    }

  while ((nread = fread(buffer, 1, sizeof(buffer), src_fp)) > 0)
    {
      if (fwrite(buffer, 1, nread, dst_fp) != nread)
        {
          fclose(src_fp);
          fclose(dst_fp);
          unlink(temp_path);
          return -EIO;
        }
    }

  if (ferror(src_fp) != 0)
    {
      fclose(src_fp);
      fclose(dst_fp);
      unlink(temp_path);
      return -EIO;
    }

  fclose(src_fp);
  if (fclose(dst_fp) != 0)
    {
      unlink(temp_path);
      return -EIO;
    }

  if (rename(temp_path, dst_path) != 0)
    {
      unlink(temp_path);
      return -errno;
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

static int qiban_join_args(int argc, FAR char *argv[], int start_index,
                           FAR char *buffer, size_t buffer_size)
{
  size_t used = 0;
  int index;

  if (start_index >= argc || buffer_size == 0)
    {
      return -EINVAL;
    }

  buffer[0] = '\0';
  for (index = start_index; index < argc; index++)
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

static bool qiban_starts_with(FAR const char *text, FAR const char *prefix)
{
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool qiban_path_exists(FAR const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

static FAR const char *
qiban_voice_select_existing_device(FAR const char *env_name,
                                   FAR const char *preferred,
                                   FAR const char *fallback,
                                   FAR const char *alt0,
                                   FAR const char *alt1)
{
  FAR const char *env_value;

  env_value = getenv(env_name);
  if (env_value != NULL && *env_value != '\0' && qiban_path_exists(env_value))
    {
      return env_value;
    }

  if (preferred != NULL && qiban_path_exists(preferred))
    {
      return preferred;
    }

  if (fallback != NULL && qiban_path_exists(fallback))
    {
      return fallback;
    }

  if (alt0 != NULL && qiban_path_exists(alt0))
    {
      return alt0;
    }

  if (alt1 != NULL && qiban_path_exists(alt1))
    {
      return alt1;
    }

  if (env_value != NULL && *env_value != '\0')
    {
      return env_value;
    }

  if (preferred != NULL)
    {
      return preferred;
    }

  if (fallback != NULL)
    {
      return fallback;
    }

  if (alt0 != NULL)
    {
      return alt0;
    }

  return alt1;
}

static FAR const char *qiban_voice_get_record_device(void)
{
  return qiban_voice_select_existing_device(QIBAN_VOICE_RECORD_DEVICE_ENV,
                                            QIBAN_VOICE_DEFAULT_RECORD_DEVICE,
                                            QIBAN_VOICE_FALLBACK_RECORD_DEVICE,
                                            "/dev/audio/pcm10c",
                                            "/dev/audio/pcm1c");
}

static FAR const char *qiban_voice_get_playback_device(void)
{
  return qiban_voice_select_existing_device(QIBAN_VOICE_PLAYBACK_DEVICE_ENV,
                                            QIBAN_VOICE_DEFAULT_PLAYBACK_DEVICE,
                                            QIBAN_VOICE_FALLBACK_PLAYBACK_DEVICE,
                                            NULL,
                                            NULL);
}

static int qiban_parse_positive_int(FAR const char *text, FAR int *value)
{
  FAR char *endptr;
  long parsed;

  if (text == NULL || value == NULL)
    {
      return -EINVAL;
    }

  errno = 0;
  parsed = strtol(text, &endptr, 10);
  if (errno != 0 || endptr == text || *endptr != '\0' || parsed <= 0)
    {
      return -EINVAL;
    }

  *value = (int)parsed;
  return OK;
}

static FAR const char *qiban_action_name(enum qiban_voice_action_e action)
{
  switch (action)
    {
      case QIBAN_VOICE_ACTION_NAVIGATE:
        return "navigate";
      case QIBAN_VOICE_ACTION_CLEAR_NAV:
        return "clear_navigation";
      case QIBAN_VOICE_ACTION_ZOOM_IN:
        return "zoom_in";
      case QIBAN_VOICE_ACTION_ZOOM_OUT:
        return "zoom_out";
      case QIBAN_VOICE_ACTION_REFRESH_MAP:
        return "refresh_map";
      default:
        return "unknown";
    }
}

static FAR const struct qiban_voice_alias_s *
qiban_find_alias(FAR const char *spoken_name)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_qiban_voice_aliases) / sizeof(g_qiban_voice_aliases[0]);
       i++)
    {
      if (strcmp(spoken_name, g_qiban_voice_aliases[i].spoken_name) == 0)
        {
          return &g_qiban_voice_aliases[i];
        }
    }

  return NULL;
}

static int qiban_json_escape(FAR const char *input, FAR char *buffer,
                             size_t buffer_size)
{
  size_t used = 0;

  while (*input != '\0')
    {
      FAR const char *replacement = NULL;
      char single[2];
      size_t needed;

      switch (*input)
        {
          case '\\':
            replacement = "\\\\";
            break;
          case '"':
            replacement = "\\\"";
            break;
          case '\n':
            replacement = "\\n";
            break;
          case '\r':
            replacement = "\\r";
            break;
          case '\t':
            replacement = "\\t";
            break;
          default:
            single[0] = *input;
            single[1] = '\0';
            replacement = single;
            break;
        }

      needed = strlen(replacement);
      if (used + needed + 1 > buffer_size)
        {
          return -ENOSPC;
        }

      memcpy(buffer + used, replacement, needed);
      used += needed;
      input++;
    }

  buffer[used] = '\0';
  return OK;
}

static int qiban_write_last_intent(FAR const char *intent_text,
                                   enum qiban_voice_action_e action,
                                   FAR const char *target,
                                   FAR const char *status)
{
  FAR struct qiban_json_workspace_s *ws = &g_qiban_json_workspace;

  if (qiban_json_escape(intent_text != NULL ? intent_text : "",
                        ws->escaped_f, sizeof(ws->escaped_f)) < 0 ||
      qiban_json_escape(target != NULL ? target : "",
                        ws->escaped_g, sizeof(ws->escaped_g)) < 0 ||
      qiban_json_escape(status != NULL ? status : "",
                        ws->escaped_h, sizeof(ws->escaped_h)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(ws->buffer, sizeof(ws->buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"intent_text\": \"%s\",\n"
           "  \"action\": \"%s\",\n"
           "  \"target\": \"%s\",\n"
           "  \"status\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           ws->escaped_f,
           qiban_action_name(action),
           ws->escaped_g,
           ws->escaped_h);

  return qiban_write_text_atomic(QIBAN_VOICE_LAST_INTENT_PATH, ws->buffer);
}

static int qiban_write_last_audio(FAR const char *mode,
                                  FAR const char *status,
                                  FAR const char *device_path,
                                  FAR const char *pcm_path,
                                  FAR const char *command_path,
                                  FAR const char *start_command_path,
                                  FAR const char *stop_command_path,
                                  int duration_s,
                                  FAR const char *recognized_text)
{
  FAR struct qiban_json_workspace_s *ws = &g_qiban_json_workspace;

  if (qiban_json_escape(mode != NULL ? mode : "",
                        ws->escaped_f, sizeof(ws->escaped_f)) < 0 ||
      qiban_json_escape(status != NULL ? status : "",
                        ws->escaped_g, sizeof(ws->escaped_g)) < 0 ||
      qiban_json_escape(device_path != NULL ? device_path : "",
                        ws->escaped_a, sizeof(ws->escaped_a)) < 0 ||
      qiban_json_escape(pcm_path != NULL ? pcm_path : "",
                        ws->escaped_e, sizeof(ws->escaped_e)) < 0 ||
      qiban_json_escape(command_path != NULL ? command_path : "",
                        ws->escaped_b, sizeof(ws->escaped_b)) < 0 ||
      qiban_json_escape(start_command_path != NULL ? start_command_path : "",
                        ws->escaped_c, sizeof(ws->escaped_c)) < 0 ||
      qiban_json_escape(stop_command_path != NULL ? stop_command_path : "",
                        ws->escaped_d, sizeof(ws->escaped_d)) < 0 ||
      qiban_json_escape(recognized_text != NULL ? recognized_text : "",
                        ws->escaped_h, sizeof(ws->escaped_h)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(ws->buffer, sizeof(ws->buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"mode\": \"%s\",\n"
           "  \"status\": \"%s\",\n"
           "  \"device\": \"%s\",\n"
           "  \"pcm_path\": \"%s\",\n"
           "  \"command_path\": \"%s\",\n"
           "  \"start_command_path\": \"%s\",\n"
           "  \"stop_command_path\": \"%s\",\n"
           "  \"duration_s\": %d,\n"
           "  \"sample_rate_hz\": %d,\n"
           "  \"channels\": %d,\n"
           "  \"bits_per_sample\": %d,\n"
           "  \"recognized_text\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           ws->escaped_f,
           ws->escaped_g,
           ws->escaped_a,
           ws->escaped_e,
           ws->escaped_b,
           ws->escaped_c,
           ws->escaped_d,
           duration_s,
           QIBAN_VOICE_SAMPLE_RATE_HZ,
           QIBAN_VOICE_CHANNELS,
           QIBAN_VOICE_BITS_PER_SAMPLE,
           ws->escaped_h);

  return qiban_write_text_atomic(QIBAN_VOICE_LAST_AUDIO_PATH, ws->buffer);
}

static int qiban_write_last_tts(FAR const char *text,
                                FAR const char *status,
                                FAR const char *text_path,
                                FAR const char *placeholder_pcm_path,
                                FAR const char *play_command_path)
{
  FAR struct qiban_json_workspace_s *ws = &g_qiban_json_workspace;

  if (qiban_json_escape(text != NULL ? text : "",
                        ws->escaped_f, sizeof(ws->escaped_f)) < 0 ||
      qiban_json_escape(status != NULL ? status : "",
                        ws->escaped_g, sizeof(ws->escaped_g)) < 0 ||
      qiban_json_escape(text_path != NULL ? text_path : "",
                        ws->escaped_a, sizeof(ws->escaped_a)) < 0 ||
      qiban_json_escape(placeholder_pcm_path != NULL ? placeholder_pcm_path : "",
                        ws->escaped_b, sizeof(ws->escaped_b)) < 0 ||
      qiban_json_escape(play_command_path != NULL ? play_command_path : "",
                        ws->escaped_c, sizeof(ws->escaped_c)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(ws->buffer, sizeof(ws->buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"text\": \"%s\",\n"
           "  \"status\": \"%s\",\n"
           "  \"request_text_path\": \"%s\",\n"
           "  \"placeholder_pcm_path\": \"%s\",\n"
           "  \"play_command_path\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           ws->escaped_f,
           ws->escaped_g,
           ws->escaped_a,
           ws->escaped_b,
           ws->escaped_c);

  return qiban_write_text_atomic(QIBAN_VOICE_LAST_TTS_PATH, ws->buffer);
}

static int qiban_write_asr_request(FAR const char *mode,
                                   FAR const char *status,
                                   FAR const char *pcm_path,
                                   int duration_s)
{
  FAR struct qiban_json_workspace_s *ws = &g_qiban_json_workspace;
  FAR const char *callback_command =
    "qiban_voice_service import-asr <recognized_text>";

  if (qiban_json_escape(mode != NULL ? mode : "",
                        ws->escaped_f, sizeof(ws->escaped_f)) < 0 ||
      qiban_json_escape(status != NULL ? status : "",
                        ws->escaped_g, sizeof(ws->escaped_g)) < 0 ||
      qiban_json_escape(pcm_path != NULL ? pcm_path : "",
                        ws->escaped_a, sizeof(ws->escaped_a)) < 0 ||
      qiban_json_escape(callback_command,
                        ws->escaped_h, sizeof(ws->escaped_h)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(ws->buffer, sizeof(ws->buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"mode\": \"%s\",\n"
           "  \"status\": \"%s\",\n"
           "  \"pcm_path\": \"%s\",\n"
           "  \"duration_s\": %d,\n"
           "  \"sample_rate_hz\": %d,\n"
           "  \"channels\": %d,\n"
           "  \"bits_per_sample\": %d,\n"
           "  \"callback_command\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           ws->escaped_f,
           ws->escaped_g,
           ws->escaped_a,
           duration_s,
           QIBAN_VOICE_SAMPLE_RATE_HZ,
           QIBAN_VOICE_CHANNELS,
           QIBAN_VOICE_BITS_PER_SAMPLE,
           ws->escaped_h);

  return qiban_write_text_atomic(QIBAN_VOICE_ASR_REQUEST_PATH, ws->buffer);
}

static FAR const char *qiban_server_job_path(FAR const char *job_type)
{
  return strcmp(job_type, "asr") == 0
           ? QIBAN_VOICE_LAST_ASR_JOB_PATH
           : QIBAN_VOICE_LAST_TTS_JOB_PATH;
}

static int qiban_build_server_url(FAR const char *base_url,
                                  FAR const char *suffix,
                                  FAR char *buffer,
                                  size_t buffer_size)
{
  size_t base_len;

  if (base_url == NULL || suffix == NULL || buffer == NULL || buffer_size == 0)
    {
      return -EINVAL;
    }

  base_len = strlen(base_url);
  while (base_len > 0 && base_url[base_len - 1] == '/')
    {
      base_len--;
    }

  if (snprintf(buffer, buffer_size, "%.*s%s",
               (int)base_len, base_url, suffix) >= (int)buffer_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static size_t qiban_curl_buffer_write_cb(FAR void *ptr, size_t size,
                                         size_t nmemb, FAR void *userdata)
{
  struct qiban_curl_buffer_s *buffer;
  size_t total;
  size_t remaining;
  size_t to_copy;

  buffer = (struct qiban_curl_buffer_s *)userdata;
  total = size * nmemb;
  if (buffer == NULL || buffer->capacity == 0)
    {
      return 0;
    }

  remaining = buffer->capacity - 1 - buffer->length;
  to_copy = total <= remaining ? total : remaining;
  if (to_copy > 0)
    {
      memcpy(buffer->data + buffer->length, ptr, to_copy);
      buffer->length += to_copy;
      buffer->data[buffer->length] = '\0';
    }

  if (to_copy < total)
    {
      buffer->truncated = true;
    }

  return total;
}

static int qiban_configure_curl_common(CURL *curl,
                                       FAR const char *url,
                                       FAR struct qiban_curl_buffer_s *response)
{
  if (curl == NULL || url == NULL || response == NULL)
    {
      return -EINVAL;
    }

  response->length = 0;
  response->truncated = false;
  if (response->capacity > 0)
    {
      response->data[0] = '\0';
    }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, qiban_curl_buffer_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_voice_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  return OK;
}

static int qiban_write_server_config(FAR const char *server_url,
                                     FAR const char *device_id)
{
  char escaped_url[QIBAN_VOICE_SERVER_URL_SIZE * 2];
  char escaped_device[QIBAN_VOICE_TEXT_SIZE * 2];
  char buffer[QIBAN_VOICE_JSON_SIZE];

  if (qiban_json_escape(server_url, escaped_url, sizeof(escaped_url)) < 0 ||
      qiban_json_escape(device_id, escaped_device, sizeof(escaped_device)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"server_url\": \"%s\",\n"
           "  \"device_id\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           escaped_url,
           escaped_device);

  return qiban_write_text_atomic(QIBAN_VOICE_SERVER_CONFIG_PATH, buffer);
}

static int qiban_load_server_config(FAR char *server_url, size_t server_url_size,
                                    FAR char *device_id, size_t device_id_size)
{
  char json[QIBAN_VOICE_JSON_SIZE];
  int ret;

  ret = qiban_load_text_file(QIBAN_VOICE_SERVER_CONFIG_PATH,
                             json, sizeof(json));
  if (ret < 0)
    {
      return ret;
    }

  if (!qiban_json_extract_string(json, "server_url",
                                 server_url, server_url_size) ||
      !qiban_json_extract_string(json, "device_id",
                                 device_id, device_id_size))
    {
      return -EINVAL;
    }

  return OK;
}

static int qiban_write_last_server_job(FAR const char *job_type,
                                       FAR const char *job_id,
                                       FAR const char *request_id,
                                       FAR const char *status,
                                       FAR const char *server_url,
                                       FAR const char *device_id)
{
  char escaped_job_type[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_job_id[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_request_id[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_status[QIBAN_VOICE_TEXT_SIZE * 2];
  char escaped_server_url[QIBAN_VOICE_SERVER_URL_SIZE * 2];
  char escaped_device_id[QIBAN_VOICE_TEXT_SIZE * 2];
  char buffer[QIBAN_VOICE_JSON_SIZE];

  if (qiban_json_escape(job_type != NULL ? job_type : "",
                        escaped_job_type, sizeof(escaped_job_type)) < 0 ||
      qiban_json_escape(job_id != NULL ? job_id : "",
                        escaped_job_id, sizeof(escaped_job_id)) < 0 ||
      qiban_json_escape(request_id != NULL ? request_id : "",
                        escaped_request_id, sizeof(escaped_request_id)) < 0 ||
      qiban_json_escape(status != NULL ? status : "",
                        escaped_status, sizeof(escaped_status)) < 0 ||
      qiban_json_escape(server_url != NULL ? server_url : "",
                        escaped_server_url, sizeof(escaped_server_url)) < 0 ||
      qiban_json_escape(device_id != NULL ? device_id : "",
                        escaped_device_id, sizeof(escaped_device_id)) < 0)
    {
      return -ENOSPC;
    }

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": %d,\n"
           "  \"source\": \"%s\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"job_type\": \"%s\",\n"
           "  \"job_id\": \"%s\",\n"
           "  \"request_id\": \"%s\",\n"
           "  \"status\": \"%s\",\n"
           "  \"server_url\": \"%s\",\n"
           "  \"device_id\": \"%s\"\n"
           "}\n",
           QIBAN_VOICE_SCHEMA_VERSION,
           QIBAN_VOICE_SOURCE,
           (long)qiban_now_epoch_s(),
           escaped_job_type,
           escaped_job_id,
           escaped_request_id,
           escaped_status,
           escaped_server_url,
           escaped_device_id);

  return qiban_write_text_atomic(qiban_server_job_path(job_type), buffer);
}

static int qiban_load_last_server_job(FAR const char *job_type,
                                      FAR char *job_id, size_t job_id_size,
                                      FAR char *server_url, size_t server_url_size)
{
  int ret;

  ret = qiban_load_text_file(qiban_server_job_path(job_type),
                             g_qiban_server_request_json,
                             sizeof(g_qiban_server_request_json));
  if (ret < 0)
    {
      return ret;
    }

  if (!qiban_json_extract_string(g_qiban_server_request_json,
                                 "job_id", job_id, job_id_size) ||
      !qiban_json_extract_string(g_qiban_server_request_json,
                                 "server_url", server_url,
                                 server_url_size))
    {
      return -EINVAL;
    }

  return OK;
}

static int qiban_submit_asr_job(FAR char *job_id, size_t job_id_size)
{
  char server_url[QIBAN_VOICE_SERVER_URL_SIZE];
  char device_id[QIBAN_VOICE_TEXT_SIZE];
  char pcm_path[PATH_MAX];
  char request_id[QIBAN_VOICE_TEXT_SIZE];
  char url[QIBAN_VOICE_SERVER_URL_SIZE + 64];
  struct qiban_curl_buffer_s response;
  CURL *curl;
  curl_mime *mime;
  curl_mimepart *part;
  CURLcode code;
  long http_code = 0;
  int duration_s = QIBAN_VOICE_DEFAULT_RECORD_SECONDS;
  int ret;

  ret = qiban_load_server_config(server_url, sizeof(server_url),
                                 device_id, sizeof(device_id));
  if (ret < 0)
    {
      fprintf(stderr, "relay server config missing\n");
      return ret;
    }

  ret = qiban_load_text_file(QIBAN_VOICE_ASR_REQUEST_PATH,
                             g_qiban_server_request_json,
                             sizeof(g_qiban_server_request_json));
  if (ret < 0)
    {
      fprintf(stderr, "failed to read %s: %d\n",
              QIBAN_VOICE_ASR_REQUEST_PATH, -ret);
      return ret;
    }

  if (!qiban_json_extract_string(g_qiban_server_request_json, "pcm_path",
                                 pcm_path, sizeof(pcm_path)))
    {
      return -EINVAL;
    }

  (void)qiban_json_extract_int(g_qiban_server_request_json,
                               "duration_s", &duration_s);
  if (!qiban_path_exists(pcm_path))
    {
      fprintf(stderr, "pcm not found: %s\n", pcm_path);
      return -ENOENT;
    }

  snprintf(request_id, sizeof(request_id), "asr-%ld",
           (long)qiban_now_epoch_s());
  ret = qiban_build_server_url(server_url, "/api/asr/jobs",
                               url, sizeof(url));
  if (ret < 0)
    {
      return ret;
    }

  response.data = g_qiban_server_response;
  response.capacity = sizeof(g_qiban_server_response);
  response.length = 0;
  response.truncated = false;

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      return -ENOMEM;
    }

  mime = curl_mime_init(curl);
  if (mime == NULL)
    {
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      return -ENOMEM;
    }

#define QIBAN_ADD_MIME_TEXT(name, value)                                  \
  do                                                                       \
    {                                                                      \
      part = curl_mime_addpart(mime);                                      \
      curl_mime_name(part, (name));                                        \
      curl_mime_data(part, (value), CURL_ZERO_TERMINATED);                 \
    }                                                                      \
  while (0)

  QIBAN_ADD_MIME_TEXT("device_id", device_id);
  QIBAN_ADD_MIME_TEXT("request_id", request_id);
  snprintf(g_qiban_server_response, sizeof(g_qiban_server_response),
           "%d", duration_s);
  QIBAN_ADD_MIME_TEXT("duration_s", g_qiban_server_response);
  QIBAN_ADD_MIME_TEXT("sample_rate_hz", "16000");
  QIBAN_ADD_MIME_TEXT("channels", "1");
  QIBAN_ADD_MIME_TEXT("bits_per_sample", "16");
  snprintf(g_qiban_server_response, sizeof(g_qiban_server_response),
           "{\"source\":\"qiban_voice_service.listen\"}");
  QIBAN_ADD_MIME_TEXT("metadata", g_qiban_server_response);

  part = curl_mime_addpart(mime);
  curl_mime_name(part, "audio_file");
  curl_mime_filedata(part, pcm_path);
  curl_mime_filename(part, "request_audio.pcm");
  curl_mime_type(part, "application/octet-stream");

  qiban_configure_curl_common(curl, url, &response);
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_mime_free(mime);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

#undef QIBAN_ADD_MIME_TEXT

  if (code != CURLE_OK)
    {
      fprintf(stderr, "submit asr failed: %s\n", curl_easy_strerror(code));
      return -EIO;
    }

  if (http_code < 200 || http_code >= 300)
    {
      fprintf(stderr, "submit asr http %ld: %s\n", http_code, response.data);
      return -EIO;
    }

  if (!qiban_json_extract_string(response.data, "id",
                                 job_id, job_id_size))
    {
      return -EINVAL;
    }

  (void)qiban_write_last_server_job("asr", job_id, request_id, "pending",
                                    server_url, device_id);
  (void)qiban_write_last_audio("listen",
                               "asr_server_job_submitted",
                               qiban_voice_get_record_device(),
                               pcm_path, "", "", "", duration_s, "");
  printf("ASR job submitted.\n");
  printf("job id: %s\n", job_id);
  printf("poll next: qiban_voice_service poll-asr %s\n", job_id);
  return OK;
}

static int qiban_submit_tts_job(FAR const char *text,
                                FAR char *job_id,
                                size_t job_id_size)
{
  char server_url[QIBAN_VOICE_SERVER_URL_SIZE];
  char device_id[QIBAN_VOICE_TEXT_SIZE];
  char request_id[QIBAN_VOICE_TEXT_SIZE];
  char escaped_text[QIBAN_VOICE_TEXT_SIZE * 2];
  char url[QIBAN_VOICE_SERVER_URL_SIZE + 64];
  char payload[QIBAN_VOICE_JSON_SIZE];
  struct qiban_curl_buffer_s response;
  struct curl_slist *headers = NULL;
  CURL *curl;
  CURLcode code;
  long http_code = 0;
  int ret;

  if (text == NULL || *text == '\0')
    {
      return -EINVAL;
    }

  ret = qiban_load_server_config(server_url, sizeof(server_url),
                                 device_id, sizeof(device_id));
  if (ret < 0)
    {
      return ret;
    }

  snprintf(request_id, sizeof(request_id), "tts-%ld",
           (long)qiban_now_epoch_s());
  ret = qiban_json_escape(text, escaped_text, sizeof(escaped_text));
  if (ret < 0)
    {
      return ret;
    }

  snprintf(payload, sizeof(payload),
           "{"
           "\"device_id\":\"%s\","
           "\"request_id\":\"%s\","
           "\"text\":\"%s\","
           "\"metadata\":{\"source\":\"qiban_voice_service.speak\"}"
           "}",
           device_id, request_id, escaped_text);

  ret = qiban_build_server_url(server_url, "/api/tts/jobs",
                               url, sizeof(url));
  if (ret < 0)
    {
      return ret;
    }

  response.data = g_qiban_server_response;
  response.capacity = sizeof(g_qiban_server_response);
  response.length = 0;
  response.truncated = false;

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      return -ENOMEM;
    }

  headers = curl_slist_append(headers, "Content-Type: application/json");
  qiban_configure_curl_common(curl, url, &response);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  if (code != CURLE_OK)
    {
      fprintf(stderr, "submit tts failed: %s\n", curl_easy_strerror(code));
      return -EIO;
    }

  if (http_code < 200 || http_code >= 300)
    {
      fprintf(stderr, "submit tts http %ld: %s\n", http_code, response.data);
      return -EIO;
    }

  if (!qiban_json_extract_string(response.data, "id",
                                 job_id, job_id_size))
    {
      return -EINVAL;
    }

  (void)qiban_write_last_server_job("tts", job_id, request_id, "pending",
                                    server_url, device_id);
  (void)qiban_write_last_tts(text,
                             "tts_server_job_submitted",
                             QIBAN_VOICE_TTS_TEXT_PATH,
                             "",
                             "");
  printf("TTS job submitted.\n");
  printf("job id: %s\n", job_id);
  printf("poll next: qiban_voice_service poll-tts %s\n", job_id);
  return OK;
}

static int qiban_http_download_file(FAR const char *url,
                                    FAR const char *output_path)
{
  char temp_path[PATH_MAX];
  CURL *curl;
  CURLcode code;
  long http_code = 0;
  FILE *fp;
  int ret;

  ret = qiban_make_temp_path(output_path, temp_path, sizeof(temp_path));
  if (ret < 0)
    {
      return ret;
    }

  fp = fopen(temp_path, "wb");
  if (fp == NULL)
    {
      return -errno;
    }

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      fclose(fp);
      unlink(temp_path);
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      fclose(fp);
      unlink(temp_path);
      return -ENOMEM;
    }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "qiban_voice_service/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  fclose(fp);
  if (code != CURLE_OK || http_code < 200 || http_code >= 300)
    {
      unlink(temp_path);
      return -EIO;
    }

  if (rename(temp_path, output_path) != 0)
    {
      unlink(temp_path);
      return -errno;
    }

  return OK;
}

static int qiban_poll_server_job(FAR const char *job_type,
                                 FAR const char *requested_job_id)
{
  char server_url[QIBAN_VOICE_SERVER_URL_SIZE];
  char device_id[QIBAN_VOICE_TEXT_SIZE];
  char job_id[QIBAN_VOICE_TEXT_SIZE];
  char url[QIBAN_VOICE_SERVER_URL_SIZE + 96];
  char status[QIBAN_VOICE_TEXT_SIZE];
  char recognized_text[QIBAN_VOICE_TEXT_SIZE];
  struct qiban_curl_buffer_s response;
  CURL *curl;
  CURLcode code;
  long http_code = 0;
  int ret;

  ret = qiban_load_server_config(server_url, sizeof(server_url),
                                 device_id, sizeof(device_id));
  if (ret < 0)
    {
      return ret;
    }

  if (requested_job_id != NULL && *requested_job_id != '\0')
    {
      snprintf(job_id, sizeof(job_id), "%s", requested_job_id);
    }
  else
    {
      ret = qiban_load_last_server_job(job_type, job_id, sizeof(job_id),
                                       server_url, sizeof(server_url));
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = qiban_build_server_url(server_url, "/api/jobs/", url, sizeof(url));
  if (ret < 0)
    {
      return ret;
    }

  if (strlen(url) + strlen(job_id) + 1 >= sizeof(url))
    {
      return -ENAMETOOLONG;
    }

  strcat(url, job_id);
  response.data = g_qiban_server_response;
  response.capacity = sizeof(g_qiban_server_response);
  response.length = 0;
  response.truncated = false;

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK)
    {
      return -EIO;
    }

  curl = curl_easy_init();
  if (curl == NULL)
    {
      curl_global_cleanup();
      return -ENOMEM;
    }

  qiban_configure_curl_common(curl, url, &response);
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  if (code != CURLE_OK || http_code < 200 || http_code >= 300)
    {
      return -EIO;
    }

  if (!qiban_json_extract_string(response.data, "status",
                                 status, sizeof(status)))
    {
      return -EINVAL;
    }

  (void)qiban_write_last_server_job(job_type, job_id, "", status,
                                    server_url, device_id);

  if (strcmp(status, "done") == 0)
    {
      if (strcmp(job_type, "asr") == 0)
        {
          if (!qiban_json_extract_string(response.data, "recognized_text",
                                         recognized_text,
                                         sizeof(recognized_text)))
            {
              fprintf(stderr, "recognized_text missing in job result\n");
              return -EINVAL;
            }

          return qiban_import_asr_text(recognized_text);
        }
      else
        {
          char result_url[QIBAN_VOICE_SERVER_URL_SIZE + 128];

          ret = qiban_build_server_url(server_url, "/api/jobs/",
                                       result_url, sizeof(result_url));
          if (ret < 0)
            {
              return ret;
            }

          if (snprintf(result_url + strlen(result_url),
                       sizeof(result_url) - strlen(result_url),
                       "%s/result-file", job_id) >=
              (int)(sizeof(result_url) - strlen(result_url)))
            {
              return -ENAMETOOLONG;
            }

          ret = qiban_http_download_file(result_url,
                                         QIBAN_VOICE_TTS_SERVER_DOWNLOAD);
          if (ret < 0)
            {
              return ret;
            }

          return qiban_import_tts_pcm(QIBAN_VOICE_TTS_SERVER_DOWNLOAD);
        }
    }

  if (strcmp(status, "failed") == 0)
    {
      fprintf(stderr, "%s job failed: %s\n", job_type, response.data);
      return -EIO;
    }

  printf("%s job %s status: %s\n", job_type, job_id, status);
  return OK;
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

static bool qiban_json_extract_int(FAR const char *json,
                                   FAR const char *key,
                                   FAR int *value)
{
  FAR const char *cursor;
  FAR char *endptr;
  long parsed;

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
  while (*cursor == ' ' || *cursor == '\t')
    {
      cursor++;
    }

  errno = 0;
  parsed = strtol(cursor, &endptr, 10);
  if (cursor == endptr || errno != 0)
    {
      return false;
    }

  *value = (int)parsed;
  return true;
}

static bool qiban_json_extract_bool(FAR const char *json,
                                    FAR const char *key,
                                    FAR bool *value)
{
  FAR const char *cursor;

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
  while (*cursor == ' ' || *cursor == '\t')
    {
      cursor++;
    }

  if (strncmp(cursor, "true", 4) == 0)
    {
      *value = true;
      return true;
    }

  if (strncmp(cursor, "false", 5) == 0)
    {
      *value = false;
      return true;
    }

  return false;
}

static int qiban_load_text_file(FAR const char *path,
                                FAR char *buffer,
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

static int qiban_forward_navigation(FAR const char *requested_name)
{
  FAR const struct qiban_voice_alias_s *alias;
  char longitude[24];
  char latitude[24];
  FAR char *argv[6];

  alias = qiban_find_alias(requested_name);
  argv[0] = "qiban_nav_service";
  argv[1] = "start";

  if (alias != NULL && alias->has_coordinates)
    {
      snprintf(longitude, sizeof(longitude), "%.6f", alias->longitude);
      snprintf(latitude, sizeof(latitude), "%.6f", alias->latitude);
      argv[2] = (FAR char *)alias->canonical_name;
      argv[3] = longitude;
      argv[4] = latitude;
      argv[5] = NULL;
    }
  else
    {
      argv[2] = (FAR char *)requested_name;
      argv[3] = NULL;
    }

  return qiban_spawn_and_wait(argv);
}

static int qiban_forward_clear(void)
{
  FAR char *argv[] =
  {
    "qiban_nav_service",
    "clear",
    NULL
  };

  return qiban_spawn_and_wait(argv);
}

static int qiban_forward_zoom(FAR const char *direction)
{
  FAR char *argv[] =
  {
    "qiban_map_service",
    "zoom",
    (FAR char *)direction,
    NULL
  };

  return qiban_spawn_and_wait(argv);
}

static int qiban_forward_refresh(void)
{
  FAR char *argv[] =
  {
    "qiban_map_service",
    "refresh",
    NULL
  };

  return qiban_spawn_and_wait(argv);
}

static int qiban_parse_intent(FAR char *text,
                              FAR struct qiban_voice_parse_result_s *result)
{
  FAR char *trimmed;

  if (text == NULL || result == NULL)
    {
      return -EINVAL;
    }

  trimmed = qiban_trim(text);
  if (*trimmed == '\0')
    {
      return -EINVAL;
    }

  result->action = QIBAN_VOICE_ACTION_UNKNOWN;
  result->target[0] = '\0';

  if (strcmp(trimmed, "取消导航") == 0 ||
      strcmp(trimmed, "结束导航") == 0 ||
      strcmp(trimmed, "停止导航") == 0)
    {
      result->action = QIBAN_VOICE_ACTION_CLEAR_NAV;
      return OK;
    }

  if (strcmp(trimmed, "放大地图") == 0)
    {
      result->action = QIBAN_VOICE_ACTION_ZOOM_IN;
      return OK;
    }

  if (strcmp(trimmed, "缩小地图") == 0)
    {
      result->action = QIBAN_VOICE_ACTION_ZOOM_OUT;
      return OK;
    }

  if (strcmp(trimmed, "刷新地图") == 0)
    {
      result->action = QIBAN_VOICE_ACTION_REFRESH_MAP;
      return OK;
    }

  if (qiban_starts_with(trimmed, "帮我导航到"))
    {
      trimmed += strlen("帮我导航到");
    }
  else if (qiban_starts_with(trimmed, "开始导航到"))
    {
      trimmed += strlen("开始导航到");
    }
  else if (qiban_starts_with(trimmed, "导航到"))
    {
      trimmed += strlen("导航到");
    }
  else if (qiban_starts_with(trimmed, "带我去"))
    {
      trimmed += strlen("带我去");
    }
  else if (qiban_starts_with(trimmed, "去"))
    {
      trimmed += strlen("去");
    }
  else
    {
      return -ENOENT;
    }

  trimmed = qiban_trim(trimmed);
  if (*trimmed == '\0')
    {
      return -EINVAL;
    }

  snprintf(result->target, sizeof(result->target), "%s", trimmed);
  result->action = QIBAN_VOICE_ACTION_NAVIGATE;
  return OK;
}

static int qiban_execute_result(FAR const char *intent_text,
                                FAR const struct qiban_voice_parse_result_s *result)
{
  int announce_ret;
  int ret;

  switch (result->action)
    {
      case QIBAN_VOICE_ACTION_NAVIGATE:
        ret = qiban_forward_navigation(result->target);
        if (ret >= 0)
          {
            announce_ret = qiban_prepare_navigation_announcement();
            if (announce_ret < 0)
              {
                fprintf(stderr, "navigation announcement failed: %d\n",
                        -announce_ret);
              }

            (void)qiban_write_last_intent(intent_text, result->action,
                                          result->target,
                                          announce_ret < 0
                                            ? "navigation_forwarded_announce_failed"
                                            : "navigation_forwarded_announce_queued");
            return ret;
          }

        (void)qiban_write_last_intent(intent_text, result->action,
                                      result->target, "navigation_failed");
        return ret;

      case QIBAN_VOICE_ACTION_CLEAR_NAV:
        ret = qiban_forward_clear();
        (void)qiban_write_last_intent(intent_text, result->action, "",
                                      ret < 0 ? "clear_failed"
                                              : "clear_forwarded");
        return ret;

      case QIBAN_VOICE_ACTION_ZOOM_IN:
        ret = qiban_forward_zoom("in");
        (void)qiban_write_last_intent(intent_text, result->action, "in",
                                      ret < 0 ? "zoom_failed"
                                              : "zoom_forwarded");
        return ret;

      case QIBAN_VOICE_ACTION_ZOOM_OUT:
        ret = qiban_forward_zoom("out");
        (void)qiban_write_last_intent(intent_text, result->action, "out",
                                      ret < 0 ? "zoom_failed"
                                              : "zoom_forwarded");
        return ret;

      case QIBAN_VOICE_ACTION_REFRESH_MAP:
        ret = qiban_forward_refresh();
        (void)qiban_write_last_intent(intent_text, result->action, "",
                                      ret < 0 ? "refresh_failed"
                                              : "refresh_forwarded");
        return ret;

      default:
        (void)qiban_write_last_intent(intent_text, result->action, "",
                                      "intent_not_supported");
        return -ENOSYS;
    }
}

static int qiban_build_record_paths(time_t stamp,
                                    FAR char *pcm_path, size_t pcm_size,
                                    FAR char *start_cmd_path, size_t start_size,
                                    FAR char *stop_cmd_path, size_t stop_size)
{
  int written;

  written = snprintf(pcm_path, pcm_size, "%s/voice_%ld.pcm",
                     QIBAN_VOICE_RECORDINGS_DIR, (long)stamp);
  if (written < 0 || written >= (int)pcm_size)
    {
      return -ENAMETOOLONG;
    }

  written = snprintf(start_cmd_path, start_size, "%s/record_%ld_start.cmd",
                     QIBAN_VOICE_SCRIPTS_DIR, (long)stamp);
  if (written < 0 || written >= (int)start_size)
    {
      return -ENAMETOOLONG;
    }

  written = snprintf(stop_cmd_path, stop_size, "%s/record_%ld_stop.cmd",
                     QIBAN_VOICE_SCRIPTS_DIR, (long)stamp);
  if (written < 0 || written >= (int)stop_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static int qiban_build_play_cmd_path(time_t stamp,
                                     FAR const char *prefix,
                                     FAR char *cmd_path, size_t cmd_size)
{
  int written;

  written = snprintf(cmd_path, cmd_size, "%s/%s_%ld.cmd",
                     QIBAN_VOICE_SCRIPTS_DIR, prefix, (long)stamp);
  if (written < 0 || written >= (int)cmd_size)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static int qiban_prepare_record_session(FAR const char *mode,
                                        int duration_s,
                                        FAR const char *requested_pcm_path)
{
  FAR const char *record_device;
  char pcm_path[PATH_MAX];
  char start_cmd_path[PATH_MAX];
  char stop_cmd_path[PATH_MAX];
  char start_cmd[QIBAN_VOICE_CMD_SIZE];
  char stop_cmd[QIBAN_VOICE_CMD_SIZE];
  time_t stamp;
  int ret;

  stamp = qiban_now_epoch_s();
  ret = qiban_build_record_paths(stamp, pcm_path, sizeof(pcm_path),
                                 start_cmd_path, sizeof(start_cmd_path),
                                 stop_cmd_path, sizeof(stop_cmd_path));
  if (ret < 0)
    {
      return ret;
    }

  if (requested_pcm_path != NULL && *requested_pcm_path != '\0')
    {
      snprintf(pcm_path, sizeof(pcm_path), "%s", requested_pcm_path);
    }

  record_device = qiban_voice_get_record_device();
  snprintf(start_cmd, sizeof(start_cmd),
           "device %s\n"
           "recordraw %s %d %d %d 0\n",
           record_device,
           pcm_path,
           QIBAN_VOICE_CHANNELS,
           QIBAN_VOICE_BITS_PER_SAMPLE,
           QIBAN_VOICE_SAMPLE_RATE_HZ);

  snprintf(stop_cmd, sizeof(stop_cmd),
           "stop\n"
           "q\n");

  ret = qiban_write_text_atomic(start_cmd_path, start_cmd);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_text_atomic(stop_cmd_path, stop_cmd);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_last_audio(mode,
                               strcmp(mode, "listen") == 0
                                 ? "listen_prepared"
                                 : "record_prepared",
                               record_device,
                               pcm_path,
                               "",
                               start_cmd_path,
                               stop_cmd_path,
                               duration_s,
                               "");
  if (ret < 0)
    {
      return ret;
    }

  if (strcmp(mode, "listen") == 0)
    {
      ret = qiban_write_asr_request(mode,
                                    "asr_request_prepared",
                                    pcm_path,
                                    duration_s);
      if (ret < 0)
        {
          return ret;
        }
    }

  printf("%s prepared.\n", strcmp(mode, "listen") == 0 ? "Listening" : "Recording");
  printf("device: %s\n", record_device);
  printf("pcm: %s\n", pcm_path);
  printf("start cmd: %s\n", start_cmd_path);
  printf("stop cmd: %s\n", stop_cmd_path);
  if (strcmp(mode, "listen") == 0)
    {
      printf("asr request: %s\n", QIBAN_VOICE_ASR_REQUEST_PATH);
      if (qiban_path_exists(QIBAN_VOICE_SERVER_CONFIG_PATH))
        {
          printf("  5. after recording, run: qiban_voice_service submit-asr\n");
        }
    }
  printf("board steps:\n");
  printf("  1. run: nxrecorder\n");
  printf("  2. paste start cmd file contents\n");
  printf("  3. speak for about %d seconds\n", duration_s);
  printf("  4. paste stop cmd file contents\n");
  return OK;
}

static int qiban_prepare_playback_session(FAR const char *pcm_path,
                                          FAR const char *status_tag,
                                          FAR const char *cmd_prefix)
{
  FAR const char *playback_device;
  char cmd_path[PATH_MAX];
  char cmd_content[QIBAN_VOICE_CMD_SIZE];
  int ret;

  if (pcm_path == NULL || *pcm_path == '\0')
    {
      return -EINVAL;
    }

  if (!qiban_path_exists(pcm_path))
    {
      playback_device = qiban_voice_get_playback_device();
      (void)qiban_write_last_audio("playback", "pcm_not_found",
                                   playback_device, pcm_path, "", "", "", 0, "");
      fprintf(stderr, "pcm not found: %s\n", pcm_path);
      return -ENOENT;
    }

  ret = qiban_build_play_cmd_path(qiban_now_epoch_s(),
                                  cmd_prefix, cmd_path, sizeof(cmd_path));
  if (ret < 0)
    {
      return ret;
    }

  playback_device = qiban_voice_get_playback_device();
  snprintf(cmd_content, sizeof(cmd_content),
           "device %s\n"
           "playraw %s %d %d %d 0\n"
           "q\n",
           playback_device,
           pcm_path,
           QIBAN_VOICE_CHANNELS,
           QIBAN_VOICE_BITS_PER_SAMPLE,
           QIBAN_VOICE_SAMPLE_RATE_HZ);

  ret = qiban_write_text_atomic(cmd_path, cmd_content);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_write_last_audio("playback", status_tag,
                               playback_device, pcm_path, cmd_path, "", "", 0, "");
  if (ret < 0)
    {
      return ret;
    }

  printf("Playback prepared.\n");
  printf("device: %s\n", playback_device);
  printf("pcm: %s\n", pcm_path);
  printf("cmd: %s\n", cmd_path);
  printf("board steps:\n");
  printf("  1. run: nxplayer\n");
  printf("  2. paste cmd file contents\n");
  return OK;
}

static int qiban_record_pcm_direct(FAR const char *pcm_path, int duration_s)
{
#if QIBAN_VOICE_HAVE_DIRECT_AUDIO
  FAR const char *record_device;
  FAR struct nxrecorder_s *recorder;
  int ret;

  recorder = nxrecorder_create();
  if (recorder == NULL)
    {
      return -ENOMEM;
    }

  record_device = qiban_voice_get_record_device();
  ret = nxrecorder_setdevice(recorder, record_device);
  if (ret < 0)
    {
      fprintf(stderr, "nxrecorder_setdevice(%s) failed: %d\n",
              record_device, ret);
      goto out;
    }

  ret = nxrecorder_recordinternal(recorder, pcm_path, AUDIO_FMT_PCM,
                                  QIBAN_VOICE_CHANNELS,
                                  QIBAN_VOICE_BITS_PER_SAMPLE,
                                  QIBAN_VOICE_SAMPLE_RATE_HZ,
                                  0);
  if (ret < 0)
    {
      goto out;
    }

  sleep((unsigned int)duration_s);
  ret = nxrecorder_stop(recorder);

out:
  nxrecorder_release(recorder);
  return ret;
#else
  (void)pcm_path;
  (void)duration_s;
  return -ENOSYS;
#endif
}

static int qiban_play_pcm_direct(FAR const char *pcm_path)
{
#if QIBAN_VOICE_HAVE_DIRECT_AUDIO
  FAR const char *playback_device;
  FAR struct nxplayer_s *player;
  int ret;

  player = nxplayer_create();
  if (player == NULL)
    {
      return -ENOMEM;
    }

  playback_device = qiban_voice_get_playback_device();
  ret = nxplayer_setdevice(player, playback_device);
  if (ret < 0)
    {
      fprintf(stderr, "nxplayer_setdevice(%s) failed: %d\n",
              playback_device, ret);
      goto out;
    }

  ret = nxplayer_playraw(player, pcm_path, AUDIO_FMT_PCM, AUDIO_FMT_UNDEF,
                         QIBAN_VOICE_CHANNELS,
                         QIBAN_VOICE_BITS_PER_SAMPLE,
                         QIBAN_VOICE_SAMPLE_RATE_HZ,
                         0);

out:
  nxplayer_release(player);
  return ret;
#else
  (void)pcm_path;
  return -ENOSYS;
#endif
}

static int qiban_run_record_playback_session(int duration_s,
                                             FAR const char *requested_pcm_path)
{
  FAR const char *record_device;
  FAR const char *playback_device;
  char pcm_path[PATH_MAX];
  char start_cmd_path[PATH_MAX];
  char stop_cmd_path[PATH_MAX];
  int ret;

  ret = qiban_build_record_paths(qiban_now_epoch_s(),
                                 pcm_path, sizeof(pcm_path),
                                 start_cmd_path, sizeof(start_cmd_path),
                                 stop_cmd_path, sizeof(stop_cmd_path));
  if (ret < 0)
    {
      return ret;
    }

  if (requested_pcm_path != NULL && *requested_pcm_path != '\0')
    {
      snprintf(pcm_path, sizeof(pcm_path), "%s", requested_pcm_path);
    }

  record_device = qiban_voice_get_record_device();
  playback_device = qiban_voice_get_playback_device();
  unlink(pcm_path);
  (void)qiban_write_last_audio("record_playback", "recording",
                               record_device, pcm_path, "", "", "", duration_s, "");

  ret = qiban_record_pcm_direct(pcm_path, duration_s);
  if (ret < 0)
    {
      (void)qiban_write_last_audio("record_playback", "record_failed",
                                   record_device, pcm_path, "", "", "", duration_s, "");
      fprintf(stderr, "record playback: record failed on %s: %d\n",
              record_device, ret);
      return ret;
    }

  if (!qiban_path_exists(pcm_path))
    {
      (void)qiban_write_last_audio("record_playback", "pcm_not_found",
                                   record_device, pcm_path, "", "", "", duration_s, "");
      return -ENOENT;
    }

  (void)qiban_write_last_audio("record_playback", "playback_started",
                               playback_device, pcm_path, "", "", "", duration_s, "");

  ret = qiban_play_pcm_direct(pcm_path);
  if (ret < 0)
    {
      (void)qiban_write_last_audio("record_playback", "playback_failed",
                                   playback_device, pcm_path, "", "", "", duration_s, "");
      fprintf(stderr, "record playback: play failed on %s: %d\n",
              playback_device, ret);
      return ret;
    }

  return qiban_write_last_audio("record_playback", "record_playback_done",
                                playback_device, pcm_path, "", "", "", duration_s, "");
}

static int qiban_prepare_tts_request(FAR const char *text)
{
  char play_cmd_path[PATH_MAX];
  char server_job_id[QIBAN_VOICE_TEXT_SIZE];
  FAR const char *status;
  FAR const char *pcm_path;
  char cmd_content[QIBAN_VOICE_CMD_SIZE];
  int ret;

  ret = qiban_write_text_atomic(QIBAN_VOICE_TTS_TEXT_PATH, text);
  if (ret < 0)
    {
      return ret;
    }

  play_cmd_path[0] = '\0';
  pcm_path = "";
  status = "tts_request_queued";

  if (qiban_path_exists(QIBAN_VOICE_TTS_PLACEHOLDER_PCM))
    {
      ret = qiban_build_play_cmd_path(qiban_now_epoch_s(),
                                      "tts_play", play_cmd_path,
                                      sizeof(play_cmd_path));
      if (ret < 0)
        {
          return ret;
        }

      snprintf(cmd_content, sizeof(cmd_content),
               "device %s\n"
               "playraw %s %d %d %d 0\n"
               "q\n",
               qiban_voice_get_playback_device(),
               QIBAN_VOICE_TTS_PLACEHOLDER_PCM,
               QIBAN_VOICE_CHANNELS,
               QIBAN_VOICE_BITS_PER_SAMPLE,
               QIBAN_VOICE_SAMPLE_RATE_HZ);

      ret = qiban_write_text_atomic(play_cmd_path, cmd_content);
      if (ret < 0)
        {
          return ret;
        }

      (void)qiban_write_last_audio("playback",
                                   "tts_placeholder_playback_prepared",
                                   qiban_voice_get_playback_device(),
                                   QIBAN_VOICE_TTS_PLACEHOLDER_PCM,
                                   play_cmd_path, "", "", 0, "");
      pcm_path = QIBAN_VOICE_TTS_PLACEHOLDER_PCM;
      status = "tts_placeholder_playback_prepared";
    }

  ret = qiban_write_last_tts(text, status,
                             QIBAN_VOICE_TTS_TEXT_PATH,
                             pcm_path,
                             play_cmd_path);
  if (ret < 0)
    {
      return ret;
    }

  printf("TTS request prepared.\n");
  printf("text file: %s\n", QIBAN_VOICE_TTS_TEXT_PATH);
  if (pcm_path[0] != '\0')
    {
      printf("placeholder pcm: %s\n", pcm_path);
      printf("playback cmd prepared under %s\n", QIBAN_VOICE_SCRIPTS_DIR);
    }
  else
    {
      printf("no synthesized pcm found yet at %s\n",
             QIBAN_VOICE_TTS_PLACEHOLDER_PCM);
    }

  if (qiban_path_exists(QIBAN_VOICE_SERVER_CONFIG_PATH))
    {
      if (qiban_submit_tts_job(text, server_job_id,
                               sizeof(server_job_id)) < 0)
        {
          fprintf(stderr, "relay server submit failed for TTS request\n");
        }
    }

  return OK;
}

static int qiban_import_tts_pcm(FAR const char *source_path)
{
  char current_text[QIBAN_VOICE_TEXT_SIZE];
  int ret;

  if (source_path == NULL || *source_path == '\0')
    {
      return -EINVAL;
    }

  if (!qiban_path_exists(source_path))
    {
      fprintf(stderr, "tts pcm not found: %s\n", source_path);
      return -ENOENT;
    }

  ret = qiban_copy_file_atomic(source_path, QIBAN_VOICE_TTS_PLACEHOLDER_PCM);
  if (ret < 0)
    {
      return ret;
    }

  ret = qiban_load_text_file(QIBAN_VOICE_TTS_TEXT_PATH,
                             current_text, sizeof(current_text));
  if (ret < 0)
    {
      current_text[0] = '\0';
    }

  ret = qiban_write_last_tts(current_text,
                             "tts_pcm_imported",
                             QIBAN_VOICE_TTS_TEXT_PATH,
                             QIBAN_VOICE_TTS_PLACEHOLDER_PCM,
                             "");
  if (ret < 0)
    {
      return ret;
    }

  printf("TTS PCM imported.\n");
  printf("source: %s\n", source_path);
  printf("board pcm: %s\n", QIBAN_VOICE_TTS_PLACEHOLDER_PCM);
  printf("next step:\n");
  printf("  qiban_voice_service play %s\n",
         QIBAN_VOICE_TTS_PLACEHOLDER_PCM);
  return OK;
}

static int qiban_prepare_navigation_announcement(void)
{
  char nav_json[QIBAN_VOICE_JSON_SIZE];
  char destination[QIBAN_VOICE_TEXT_SIZE];
  char next_turn[QIBAN_VOICE_TEXT_SIZE];
  char spoken_text[QIBAN_VOICE_TEXT_SIZE];
  int remaining_distance_m = 0;
  int eta_minutes = 0;
  bool active = false;
  int ret;

  ret = qiban_load_text_file(QIBAN_VOICE_NAV_STATE_PATH,
                             nav_json, sizeof(nav_json));
  if (ret < 0)
    {
      fprintf(stderr, "failed to read %s: %d\n",
              QIBAN_VOICE_NAV_STATE_PATH, -ret);
      return ret;
    }

  if (!qiban_json_extract_bool(nav_json, "active", &active) || !active)
    {
      fprintf(stderr, "navigation is not active\n");
      return -ENOENT;
    }

  if (!qiban_json_extract_string(nav_json, "destination",
                                 destination, sizeof(destination)))
    {
      return -EINVAL;
    }

  if (!qiban_json_extract_string(nav_json, "next_turn",
                                 next_turn, sizeof(next_turn)))
    {
      snprintf(next_turn, sizeof(next_turn), "%s", "请沿当前路线继续行驶");
    }

  (void)qiban_json_extract_int(nav_json, "remaining_distance_m",
                               &remaining_distance_m);
  (void)qiban_json_extract_int(nav_json, "eta_minutes", &eta_minutes);

  if (remaining_distance_m > 0 && eta_minutes > 0)
    {
      snprintf(spoken_text, sizeof(spoken_text),
               "正在前往%s。%s。剩余%d米，预计%d分钟到达。",
               destination, next_turn, remaining_distance_m, eta_minutes);
    }
  else
    {
      snprintf(spoken_text, sizeof(spoken_text),
               "正在前往%s。%s。",
               destination, next_turn);
    }

  return qiban_prepare_tts_request(spoken_text);
}

static int qiban_handle_mock_asr(FAR char *text)
{
  struct qiban_voice_parse_result_s result;
  char audio_json[QIBAN_VOICE_JSON_SIZE];
  char device_path[PATH_MAX];
  char pcm_path[PATH_MAX];
  int duration_s;
  int load_ret;
  int ret;

  snprintf(device_path, sizeof(device_path), "%s",
           qiban_voice_get_record_device());
  pcm_path[0] = '\0';
  duration_s = 0;
  load_ret = qiban_load_text_file(QIBAN_VOICE_LAST_AUDIO_PATH,
                                  audio_json, sizeof(audio_json));
  if (load_ret == OK)
    {
      (void)qiban_json_extract_string(audio_json, "device",
                                      device_path, sizeof(device_path));
      (void)qiban_json_extract_string(audio_json, "pcm_path",
                                      pcm_path, sizeof(pcm_path));
      (void)qiban_json_extract_int(audio_json, "duration_s", &duration_s);
    }

  ret = qiban_parse_intent(text, &result);
  if (ret < 0)
    {
      (void)qiban_write_last_audio("mock_asr",
                                   "intent_not_supported",
                                   device_path,
                                   pcm_path, "", "", "", duration_s, text);
      (void)qiban_write_last_intent(text, QIBAN_VOICE_ACTION_UNKNOWN, "",
                                    "intent_not_supported");
      fprintf(stderr, "unsupported intent: %s\n", text);
      return ret;
    }

  ret = qiban_execute_result(text, &result);
  (void)qiban_write_last_audio("mock_asr",
                               ret < 0 ? "intent_forward_failed"
                                       : "intent_forwarded",
                               device_path,
                               pcm_path, "", "", "", duration_s, text);
  return ret;
}

static int qiban_import_asr_text(FAR char *text)
{
  struct qiban_voice_parse_result_s result;
  char audio_json[QIBAN_VOICE_JSON_SIZE];
  char device_path[PATH_MAX];
  char pcm_path[PATH_MAX];
  int duration_s;
  int ret;

  snprintf(device_path, sizeof(device_path), "%s",
           qiban_voice_get_record_device());
  pcm_path[0] = '\0';
  duration_s = 0;
  ret = qiban_load_text_file(QIBAN_VOICE_LAST_AUDIO_PATH,
                             audio_json, sizeof(audio_json));
  if (ret == OK)
    {
      (void)qiban_json_extract_string(audio_json, "device",
                                      device_path, sizeof(device_path));
      (void)qiban_json_extract_string(audio_json, "pcm_path",
                                      pcm_path, sizeof(pcm_path));
      (void)qiban_json_extract_int(audio_json, "duration_s", &duration_s);
    }

  ret = qiban_parse_intent(text, &result);
  if (ret < 0)
    {
      (void)qiban_write_last_audio("import_asr",
                                   "intent_not_supported",
                                   device_path,
                                   pcm_path, "", "", "", duration_s, text);
      (void)qiban_write_last_intent(text, QIBAN_VOICE_ACTION_UNKNOWN, "",
                                    "intent_not_supported");
      fprintf(stderr, "unsupported intent: %s\n", text);
      return ret;
    }

  ret = qiban_execute_result(text, &result);
  (void)qiban_write_last_audio("import_asr",
                               ret < 0 ? "intent_forward_failed"
                                       : "intent_forwarded",
                               device_path,
                               pcm_path, "", "", "", duration_s, text);
  return ret;
}

static int qiban_print_file(FAR const char *title, FAR const char *path)
{
  char buffer[QIBAN_VOICE_JSON_SIZE];
  FAR FILE *fp;
  size_t nread;

  printf("[%s]\n", title);

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      printf("missing: %s\n", path);
      return -errno;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      printf("empty: %s\n", path);
      return -EIO;
    }

  buffer[nread] = '\0';
  printf("%s", buffer);
  return OK;
}

static int qiban_print_status_group(bool show_intent,
                                    bool show_audio,
                                    bool show_tts)
{
  int ret = -ENOENT;

  if (show_intent && qiban_print_file("intent", QIBAN_VOICE_LAST_INTENT_PATH) == OK)
    {
      ret = OK;
    }

  if (show_audio && qiban_print_file("audio", QIBAN_VOICE_LAST_AUDIO_PATH) == OK)
    {
      ret = OK;
    }

  if (show_tts && qiban_print_file("tts", QIBAN_VOICE_LAST_TTS_PATH) == OK)
    {
      ret = OK;
    }

  return ret;
}

static void qiban_print_aliases(void)
{
  unsigned int i;

  printf("Known aliases:\n");
  for (i = 0; i < sizeof(g_qiban_voice_aliases) / sizeof(g_qiban_voice_aliases[0]);
       i++)
    {
      printf("  %s -> %s", g_qiban_voice_aliases[i].spoken_name,
             g_qiban_voice_aliases[i].canonical_name);
      if (g_qiban_voice_aliases[i].has_coordinates)
        {
          printf(" (%.6f, %.6f)",
                 g_qiban_voice_aliases[i].longitude,
                 g_qiban_voice_aliases[i].latitude);
        }

      printf("\n");
    }
}

static void qiban_print_examples(void)
{
  printf("Examples:\n");
  printf("  qiban_voice_service server-config http://example.com:8787 r528-demo-001\n");
  printf("  qiban_voice_service intent 导航到清华大学\n");
  printf("  qiban_voice_service nav 软件园二期\n");
  printf("  qiban_voice_service record 4\n");
  printf("  qiban_voice_service listen 4\n");
  printf("  qiban_voice_service submit-asr\n");
  printf("  qiban_voice_service poll-asr\n");
  printf("  qiban_voice_service play /data/qiban_voice/recordings/voice_xxx.pcm\n");
  printf("  qiban_voice_service mock-asr 导航到北大东门\n");
  printf("  qiban_voice_service import-asr 导航到清华大学\n");
  printf("  qiban_voice_service speak 前方右转\n");
  printf("  qiban_voice_service poll-tts\n");
  printf("  qiban_voice_service import-tts /data/tts/infer_out.pcm\n");
  printf("  qiban_voice_service announce-nav\n");
  printf("  qiban_voice_service status\n");
}

static void qiban_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s examples\n", progname);
  printf("  %s aliases\n", progname);
  printf("  %s server-config <server_url> <device_id>\n", progname);
  printf("  %s server-config clear\n", progname);
  printf("  %s intent <text>\n", progname);
  printf("  %s nav <destination>\n", progname);
  printf("  %s clear\n", progname);
  printf("  %s zoom <in|out>\n", progname);
  printf("  %s refresh\n", progname);
  printf("  %s record [seconds] [pcm_path]\n", progname);
  printf("  %s listen [seconds] [pcm_path]\n", progname);
  printf("  %s record-play [seconds] [pcm_path]\n", progname);
  printf("  %s submit-asr\n", progname);
  printf("  %s poll-asr [job_id]\n", progname);
  printf("  %s play <pcm_path>\n", progname);
  printf("  %s mock-asr <text>\n", progname);
  printf("  %s import-asr <text>\n", progname);
  printf("  %s speak <text>\n", progname);
  printf("  %s submit-tts [text]\n", progname);
  printf("  %s poll-tts [job_id]\n", progname);
  printf("  %s import-tts <pcm_path>\n", progname);
  printf("  %s announce-nav\n", progname);
  printf("  %s audio-status\n", progname);
  printf("  %s tts-status\n", progname);
  printf("  %s status [intent|audio|tts|all]\n", progname);
}

int main(int argc, FAR char *argv[])
{
  struct qiban_voice_parse_result_s result;
  char text[QIBAN_VOICE_TEXT_SIZE];
  int duration_s;
  int ret;

  ret = qiban_ensure_runtime_tree();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      qiban_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "examples") == 0)
    {
      qiban_print_examples();
      return OK;
    }

  if (strcmp(argv[1], "aliases") == 0)
    {
      qiban_print_aliases();
      return OK;
    }

  if (strcmp(argv[1], "server-config") == 0)
    {
      if (argc == 3 && strcmp(argv[2], "clear") == 0)
        {
          unlink(QIBAN_VOICE_SERVER_CONFIG_PATH);
          printf("relay server config cleared.\n");
          return OK;
        }

      if (argc < 4)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_write_server_config(argv[2], argv[3]);
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      printf("relay server configured.\n");
      printf("server: %s\n", argv[2]);
      printf("device: %s\n", argv[3]);
      return OK;
    }

  if (strcmp(argv[1], "audio-status") == 0)
    {
      ret = qiban_print_status_group(false, true, false);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "tts-status") == 0)
    {
      ret = qiban_print_status_group(false, false, true);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      if (argc == 2 || strcmp(argv[2], "all") == 0)
        {
          ret = qiban_print_status_group(true, true, true);
        }
      else if (strcmp(argv[2], "intent") == 0)
        {
          ret = qiban_print_status_group(true, false, false);
        }
      else if (strcmp(argv[2], "audio") == 0)
        {
          ret = qiban_print_status_group(false, true, false);
        }
      else if (strcmp(argv[2], "tts") == 0)
        {
          ret = qiban_print_status_group(false, false, true);
        }
      else
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "refresh") == 0)
    {
      result.action = QIBAN_VOICE_ACTION_REFRESH_MAP;
      result.target[0] = '\0';
      ret = qiban_execute_result("refresh", &result);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "clear") == 0)
    {
      result.action = QIBAN_VOICE_ACTION_CLEAR_NAV;
      result.target[0] = '\0';
      ret = qiban_execute_result("clear", &result);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "zoom") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      if (strcmp(argv[2], "in") == 0)
        {
          result.action = QIBAN_VOICE_ACTION_ZOOM_IN;
          snprintf(result.target, sizeof(result.target), "%s", "in");
        }
      else if (strcmp(argv[2], "out") == 0)
        {
          result.action = QIBAN_VOICE_ACTION_ZOOM_OUT;
          snprintf(result.target, sizeof(result.target), "%s", "out");
        }
      else
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_execute_result("zoom", &result);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "nav") == 0)
    {
      if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      result.action = QIBAN_VOICE_ACTION_NAVIGATE;
      snprintf(result.target, sizeof(result.target), "%s", text);
      ret = qiban_execute_result(text, &result);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "intent") == 0)
    {
      if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_parse_intent(text, &result);
      if (ret < 0)
        {
          (void)qiban_write_last_intent(text, QIBAN_VOICE_ACTION_UNKNOWN, "",
                                        "intent_not_supported");
          fprintf(stderr, "unsupported intent: %s\n", text);
          return EXIT_FAILURE;
        }

      ret = qiban_execute_result(text, &result);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "record") == 0 || strcmp(argv[1], "listen") == 0)
    {
      FAR const char *pcm_path = NULL;
      FAR const char *mode = argv[1];

      duration_s = QIBAN_VOICE_DEFAULT_RECORD_SECONDS;
      if (argc >= 3)
        {
          if (qiban_parse_positive_int(argv[2], &duration_s) < 0)
            {
              qiban_usage(argv[0]);
              return EXIT_FAILURE;
            }
        }

      if (argc >= 4)
        {
          pcm_path = argv[3];
        }

      ret = qiban_prepare_record_session(mode, duration_s, pcm_path);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "record-play") == 0)
    {
      FAR const char *pcm_path = NULL;

      duration_s = QIBAN_VOICE_DEFAULT_RECORD_SECONDS;
      if (argc >= 3)
        {
          if (qiban_parse_positive_int(argv[2], &duration_s) < 0)
            {
              qiban_usage(argv[0]);
              return EXIT_FAILURE;
            }
        }

      if (argc >= 4)
        {
          pcm_path = argv[3];
        }

      ret = qiban_run_record_playback_session(duration_s, pcm_path);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "play") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_prepare_playback_session(argv[2],
                                           "playback_prepared",
                                           "play");
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "submit-asr") == 0)
    {
      ret = qiban_submit_asr_job(text, sizeof(text));
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "poll-asr") == 0)
    {
      ret = qiban_poll_server_job("asr", argc >= 3 ? argv[2] : NULL);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "mock-asr") == 0)
    {
      if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_handle_mock_asr(text);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "import-asr") == 0)
    {
      if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_import_asr_text(text);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "submit-tts") == 0)
    {
      if (argc >= 3)
        {
          if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
            {
              qiban_usage(argv[0]);
              return EXIT_FAILURE;
            }
        }
      else
        {
          ret = qiban_load_text_file(QIBAN_VOICE_TTS_TEXT_PATH,
                                     text, sizeof(text));
          if (ret < 0)
            {
              return EXIT_FAILURE;
            }
        }

      ret = qiban_submit_tts_job(qiban_trim(text), text, sizeof(text));
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "poll-tts") == 0)
    {
      ret = qiban_poll_server_job("tts", argc >= 3 ? argv[2] : NULL);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "import-tts") == 0)
    {
      if (argc < 3)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_import_tts_pcm(argv[2]);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "announce-nav") == 0)
    {
      ret = qiban_prepare_navigation_announcement();
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  if (strcmp(argv[1], "speak") == 0)
    {
      if (qiban_join_args(argc, argv, 2, text, sizeof(text)) < 0)
        {
          qiban_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = qiban_prepare_tts_request(text);
      return ret < 0 ? EXIT_FAILURE : OK;
    }

  qiban_usage(argv[0]);
  return EXIT_FAILURE;
}
