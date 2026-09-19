/****************************************************************************
 * qiban_music_service_main.c
 *
 * Music playback service for the Qiban AI dashboard.
 * Supports three audio sources:
 *   - sdcard:     Local WAV/PCM files from /data/music/
 *   - wifi:       HTTP PCM stream from a configurable URL
 *   - bluetooth:  Bluetooth A2DP sink (stub, needs BT stack)
 *
 * Writes playback state to /data/qiban_music_state.json for the UI.
 *
 * Usage:
 *   qiban_music_service &                    # start daemon
 *   qiban_music_service play                 # resume / start playback
 *   qiban_music_service pause                # pause playback
 *   qiban_music_service next                 # next track
 *   qiban_music_service prev                 # previous track
 *   qiban_music_service stop                 # stop playback
 *   qiban_music_service source sdcard|wifi|bluetooth
 *   qiban_music_service volume <0-100>
 *   qiban_music_service status               # print state JSON
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <nuttx/audio/audio.h>
#include <netutils/netlib.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#ifndef ERROR
#  define ERROR (-1)
#endif

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define MUSIC_STATE_PATH          "/data/qiban_music_state.json"
#define MUSIC_COMMAND_PATH        "/data/qiban_music_command"
#define MUSIC_DIR                 "/data/music"
#define MUSIC_DIR_ALT             "/mnt/sdcard/music"
#define MUSIC_AUDIO_DEV_ENV       "QIBAN_MUSIC_AUDIO_DEVICE"
#define MUSIC_AUDIO_DEV_DEFAULT   "/dev/audio/pcm0p"
#define MUSIC_AUDIO_DEV_FALLBACK  "/dev/audio/pcm1"
#define MUSIC_WIFI_IFNAME         "wlan0"
#define MUSIC_TEMP_SUFFIX         ".tmp"
#define MUSIC_MAX_TRACKS          64
#define MUSIC_MAX_PATH            128
#define MUSIC_BUF_SIZE            4096
#define MUSIC_HTTP_BUF_SIZE       8192
#define MUSIC_PROBE_PERIOD_SEC    5

#define MUSIC_SOURCE_SDCARD    0
#define MUSIC_SOURCE_WIFI      1
#define MUSIC_SOURCE_BT        2

#define MUSIC_CMD_NONE         0
#define MUSIC_CMD_PLAY         1
#define MUSIC_CMD_PAUSE        2
#define MUSIC_CMD_STOP         3
#define MUSIC_CMD_NEXT         4
#define MUSIC_CMD_PREV         5

/* WAV header sizes */

#define WAV_HEADER_SIZE        44

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wav_header_s
{
  char riff_id[4];       /* "RIFF" */
  uint32_t file_size;    /* file size - 8 */
  char wave_id[4];       /* "WAVE" */
  char fmt_id[4];        /* "fmt " */
  uint32_t fmt_size;     /* 16 for PCM */
  uint16_t audio_fmt;    /* 1 = PCM */
  uint16_t channels;     /* 1 = mono, 2 = stereo */
  uint32_t sample_rate;  /* e.g. 44100, 16000 */
  uint32_t byte_rate;    /* sample_rate * channels * bits/8 */
  uint16_t block_align;  /* channels * bits/8 */
  uint16_t bits_per_sample; /* 8, 16 */
  char data_id[4];       /* "data" */
  uint32_t data_size;    /* PCM data size */
};

struct track_info_s
{
  char path[MUSIC_MAX_PATH];
  char name[64];
  int duration_sec;
};

struct music_state_s
{
  char title[64];
  char artist[64];
  char album[64];
  char source[16];
  char source_status[32];
  int source_id;
  int playing;
  int duration_sec;
  int position_sec;
  int volume;
  int track_index;
  int track_count;
  int sdcard_available;
  int wifi_connected;
  int bluetooth_connected;
  struct track_info_s tracks[MUSIC_MAX_TRACKS];
};

struct music_context_s
{
  struct music_state_s state;
  int audio_fd;
  int command;
  int running;
  int need_reload;
  pthread_mutex_t lock;
  pthread_t play_thread;
  char wifi_url[256];
  char audio_dev[32];
};

/****************************************************************************
 * Private data
 ****************************************************************************/

static struct music_context_s g_music_ctx;
static bool g_music_missing_dir_logged;

/****************************************************************************
 * Forward declarations
 ****************************************************************************/

static int music_state_save(FAR struct music_state_s *state);
static int music_scan_sdcard(FAR struct music_state_s *state);
static int music_open_audio(FAR struct music_context_s *ctx);
static void music_close_audio(FAR struct music_context_s *ctx);
static int music_play_wav(FAR struct music_context_s *ctx,
                          FAR const char *path);
static int music_play_stream(FAR struct music_context_s *ctx,
                             FAR const char *url);
static void *music_playback_thread(FAR void *arg);
static void music_probe_sources(FAR struct music_context_s *ctx);

/****************************************************************************
 * Helpers
 ****************************************************************************/

static int clamp_int(int val, int lo, int hi)
{
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}

static bool music_path_exists(FAR const char *path)
{
  struct stat st;
  return path != NULL && stat(path, &st) == 0;
}

static FAR const char *music_resolve_audio_device(void)
{
  FAR const char *env = getenv(MUSIC_AUDIO_DEV_ENV);

  if (env != NULL && *env != '\0')
    {
      return env;
    }

  if (music_path_exists(MUSIC_AUDIO_DEV_DEFAULT))
    {
      return MUSIC_AUDIO_DEV_DEFAULT;
    }

  if (music_path_exists(MUSIC_AUDIO_DEV_FALLBACK))
    {
      return MUSIC_AUDIO_DEV_FALLBACK;
    }

  return MUSIC_AUDIO_DEV_DEFAULT;
}

static int music_write_json(FAR const char *path, FAR const char *json)
{
  char tmp_path[128];
  FAR FILE *fp;

  snprintf(tmp_path, sizeof(tmp_path), "%s%s", path, MUSIC_TEMP_SUFFIX);
  fp = fopen(tmp_path, "w");
  if (fp == NULL)
    {
      return ERROR;
    }

  fputs(json, fp);
  fclose(fp);

  if (rename(tmp_path, path) != 0)
    {
      unlink(tmp_path);
      return ERROR;
    }

  return OK;
}

static int music_read_file(FAR const char *path, FAR char *buf,
                           size_t buflen)
{
  FAR FILE *fp;
  size_t n;

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return ERROR;
    }

  n = fread(buf, 1, buflen - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  return OK;
}

static int music_command_save(FAR const char *cmd, FAR const char *arg)
{
  char line[256];

  if (cmd == NULL)
    {
      return ERROR;
    }

  if (arg != NULL)
    {
      snprintf(line, sizeof(line), "%s %s\n", cmd, arg);
    }
  else
    {
      snprintf(line, sizeof(line), "%s\n", cmd);
    }

  return music_write_json(MUSIC_COMMAND_PATH, line);
}

static int music_command_load(FAR char *cmd, size_t cmdlen,
                              FAR char *arg, size_t arglen)
{
  char line[256];
  FAR char *cursor;
  FAR char *space;
  FAR char *newline;

  if (music_read_file(MUSIC_COMMAND_PATH, line, sizeof(line)) != OK)
    {
      return ERROR;
    }

  unlink(MUSIC_COMMAND_PATH);

  newline = strchr(line, '\n');
  if (newline != NULL)
    {
      *newline = '\0';
    }

  cursor = line;
  while (*cursor == ' ')
    {
      cursor++;
    }

  if (*cursor == '\0')
    {
      return ERROR;
    }

  space = strchr(cursor, ' ');
  if (space != NULL)
    {
      *space = '\0';
      space++;
      while (*space == ' ')
        {
          space++;
        }
    }

  snprintf(cmd, cmdlen, "%s", cursor);
  if (space != NULL && *space != '\0')
    {
      snprintf(arg, arglen, "%s", space);
    }
  else if (arglen > 0)
    {
      arg[0] = '\0';
    }

  return OK;
}

static int music_json_extract_int(FAR const char *json,
                                  FAR const char *key, FAR int *out)
{
  char pattern[64];
  FAR char *p;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    {
      return ERROR;
    }

  p = strchr(p, ':');
  if (p == NULL)
    {
      return ERROR;
    }

  p++;
  while (*p == ' ')
    {
      p++;
    }

  *out = atoi(p);
  return OK;
}

static int music_json_extract_string(FAR const char *json,
                                     FAR const char *key,
                                     FAR char *out, size_t outlen)
{
  char pattern[64];
  FAR char *p;
  FAR char *end;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    {
      return ERROR;
    }

  p = strchr(p, ':');
  if (p == NULL)
    {
      return ERROR;
    }

  p++;
  while (*p == ' ')
    {
      p++;
    }

  if (*p == '"')
    {
      p++;
    }

  end = strchr(p, '"');
  if (end == NULL)
    {
      end = p + strlen(p);
    }

  size_t len = end - p;
  if (len >= outlen)
    {
      len = outlen - 1;
    }

  memcpy(out, p, len);
  out[len] = '\0';
  return OK;
}

/****************************************************************************
 * WAV file parsing
 ****************************************************************************/

static int wav_parse_header(int fd, FAR struct wav_header_s *hdr)
{
  ssize_t n;

  n = read(fd, hdr, WAV_HEADER_SIZE);
  if (n != WAV_HEADER_SIZE)
    {
      return ERROR;
    }

  if (memcmp(hdr->riff_id, "RIFF", 4) != 0 ||
      memcmp(hdr->wave_id, "WAVE", 4) != 0)
    {
      return ERROR;
    }

  return OK;
}

static int wav_get_duration(FAR const struct wav_header_s *hdr)
{
  if (hdr->byte_rate == 0)
    {
      return 0;
    }

  return (int)(hdr->data_size / hdr->byte_rate);
}

/****************************************************************************
 * Audio device
 ****************************************************************************/

static int music_open_audio(FAR struct music_context_s *ctx)
{
  if (ctx->audio_fd >= 0)
    {
      return OK;
    }

  ctx->audio_fd = open(ctx->audio_dev, O_WRONLY);
  if (ctx->audio_fd < 0)
    {
      fprintf(stderr, "[music] failed to open %s: %d\n",
              ctx->audio_dev, errno);
      return ERROR;
    }

  return OK;
}

static void music_close_audio(FAR struct music_context_s *ctx)
{
  if (ctx->audio_fd >= 0)
    {
      close(ctx->audio_fd);
      ctx->audio_fd = -1;
    }
}

static int music_audio_configure(int fd, uint16_t channels,
                                 uint32_t sample_rate,
                                 uint16_t bits_per_sample)
{
  struct audio_caps_desc_s cap_desc;

  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_channels = channels;
  cap_desc.caps.ac_format.hw = AUDIO_FMT_PCM;
  cap_desc.caps.ac_controls.hw[0] = sample_rate;
  cap_desc.caps.ac_controls.b[2] = bits_per_sample;

  if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc) < 0)
    {
      fprintf(stderr, "[music] AUDIOIOC_CONFIGURE failed: %d\n", errno);
      return ERROR;
    }

  return OK;
}

static int music_audio_start(int fd)
{
  if (ioctl(fd, AUDIOIOC_START, 0) < 0)
    {
      return ERROR;
    }

  return OK;
}

static int music_audio_stop(int fd)
{
  if (ioctl(fd, AUDIOIOC_STOP, 0) < 0)
    {
      return ERROR;
    }

  return OK;
}

/****************************************************************************
 * SD card: scan music directory
 ****************************************************************************/

static int music_scan_sdcard(FAR struct music_state_s *state)
{
  FAR const char *dir_path;
  FAR DIR *dir;
  FAR struct dirent *entry;
  int count = 0;

  state->track_count = 0;

  /* Try primary dir, then alt */

  dir_path = MUSIC_DIR;
  dir = opendir(dir_path);
  if (dir == NULL)
    {
      dir_path = MUSIC_DIR_ALT;
      dir = opendir(dir_path);
    }

  if (dir == NULL)
    {
      if (!g_music_missing_dir_logged)
        {
          fprintf(stderr,
                  "[music] no music directory found (checked %s and %s)\n",
                  MUSIC_DIR, MUSIC_DIR_ALT);
          g_music_missing_dir_logged = true;
        }

      state->sdcard_available = 0;
      if (state->source_id == MUSIC_SOURCE_SDCARD)
        {
          snprintf(state->source_status, sizeof(state->source_status),
                   "no_media");
        }

      return ERROR;
    }

  g_music_missing_dir_logged = false;

  while ((entry = readdir(dir)) != NULL && count < MUSIC_MAX_TRACKS)
    {
      FAR const char *name = entry->d_name;
      size_t len = strlen(name);

      /* Accept .wav and .pcm files */

      if (len < 5)
        {
          continue;
        }

      if (strcasecmp(name + len - 4, ".wav") != 0 &&
          strcasecmp(name + len - 4, ".pcm") != 0)
        {
          continue;
        }

      snprintf(state->tracks[count].path, MUSIC_MAX_PATH,
               "%s/%s", dir_path, name);
      snprintf(state->tracks[count].name, 64, "%s", name);
      state->tracks[count].duration_sec = 0;
      count++;
    }

  closedir(dir);

  /* Sort tracks by name (simple bubble sort) */

  for (int i = 0; i < count - 1; i++)
    {
      for (int j = i + 1; j < count; j++)
        {
          if (strcmp(state->tracks[i].name, state->tracks[j].name) > 0)
            {
              struct track_info_s tmp = state->tracks[i];
              state->tracks[i] = state->tracks[j];
              state->tracks[j] = tmp;
            }
        }
    }

  state->track_count = count;
  state->sdcard_available = count > 0 ? 1 : 0;
  if (state->source_id == MUSIC_SOURCE_SDCARD)
    {
      snprintf(state->source_status, sizeof(state->source_status),
               count > 0 ? "ready" : "no_media");
    }

  printf("[music] scanned %d tracks from %s\n", count, dir_path);
  return OK;
}

/****************************************************************************
 * WAV playback
 ****************************************************************************/

static int music_play_wav(FAR struct music_context_s *ctx,
                          FAR const char *path)
{
  struct wav_header_s hdr;
  int fd;
  int ret = OK;
  char buf[MUSIC_BUF_SIZE];
  ssize_t nread;
  ssize_t nwritten;
  uint32_t bytes_played = 0;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      fprintf(stderr, "[music] cannot open %s: %d\n", path, errno);
      return ERROR;
    }

  if (wav_parse_header(fd, &hdr) != OK)
    {
      /* Not a WAV file, try playing as raw PCM (16kHz/16bit/mono) */

      lseek(fd, 0, SEEK_SET);
      hdr.channels = 1;
      hdr.sample_rate = 16000;
      hdr.bits_per_sample = 16;
      hdr.data_size = 0;
      hdr.byte_rate = 16000 * 2;
    }

  /* Configure audio device */

  ret = music_audio_configure(ctx->audio_fd, hdr.channels,
                              hdr.sample_rate, hdr.bits_per_sample);
  if (ret != OK)
    {
      close(fd);
      return ERROR;
    }

  ret = music_audio_start(ctx->audio_fd);
  if (ret != OK)
    {
      close(fd);
      return ERROR;
    }

  /* Update state */

  pthread_mutex_lock(&ctx->lock);
  ctx->state.duration_sec = wav_get_duration(&hdr);
  ctx->state.position_sec = 0;
  ctx->state.playing = 1;
  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  printf("[music] playing: %s (%lu Hz, %d ch, %d bit)\n",
         path, hdr.sample_rate, hdr.channels, hdr.bits_per_sample);

  /* Play loop */

  while (ctx->running && ctx->command != MUSIC_CMD_STOP &&
         ctx->command != MUSIC_CMD_NEXT &&
         ctx->command != MUSIC_CMD_PREV)
    {
      /* Handle pause */

      if (ctx->command == MUSIC_CMD_PAUSE)
        {
          usleep(50000); /* 50ms idle while paused */
          continue;
        }

      nread = read(fd, buf, sizeof(buf));
      if (nread <= 0)
        {
          break; /* End of file */
        }

      nwritten = write(ctx->audio_fd, buf, nread);
      if (nwritten < 0)
        {
          fprintf(stderr, "[music] audio write error: %d\n", errno);
          ret = ERROR;
          break;
        }

      bytes_played += nread;

      /* Update position periodically */

      if (hdr.byte_rate > 0 && bytes_played % (hdr.byte_rate) < sizeof(buf))
        {
          pthread_mutex_lock(&ctx->lock);
          ctx->state.position_sec = (int)(bytes_played / hdr.byte_rate);
          pthread_mutex_unlock(&ctx->lock);
        }
    }

  music_audio_stop(ctx->audio_fd);
  close(fd);

  pthread_mutex_lock(&ctx->lock);
  ctx->state.playing = 0;
  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  return ret;
}

/****************************************************************************
 * WiFi HTTP streaming
 ****************************************************************************/

static int music_play_stream(FAR struct music_context_s *ctx,
                             FAR const char *url)
{
  int sockfd;
  struct sockaddr_in addr;
  char request[512];
  char host[128];
  char path[256];
  int port = 80;
  char buf[MUSIC_HTTP_BUF_SIZE];
  ssize_t nread;
  int header_done = 0;
  int header_len = 0;
  int i;

  /* Parse URL: http://host:port/path */

  snprintf(host, sizeof(host), "10.0.0.1");
  snprintf(path, sizeof(path), "/stream");

  if (strncmp(url, "http://", 7) == 0)
    {
      FAR const char *p = url + 7;
      FAR const char *slash = strchr(p, '/');
      FAR const char *colon = strchr(p, ':');

      if (colon && (!slash || colon < slash))
        {
          size_t hlen = colon - p;
          if (hlen >= sizeof(host))
            {
              hlen = sizeof(host) - 1;
            }

          memcpy(host, p, hlen);
          host[hlen] = '\0';
          port = atoi(colon + 1);
        }
      else if (slash)
        {
          size_t hlen = slash - p;
          if (hlen >= sizeof(host))
            {
              hlen = sizeof(host) - 1;
            }

          memcpy(host, p, hlen);
          host[hlen] = '\0';
        }

      if (slash)
        {
          snprintf(path, sizeof(path), "%s", slash);
        }
    }

  /* Connect */

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    {
      return ERROR;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);

  printf("[music] connecting to %s:%d%s\n", host, port, path);

  if (connect(sockfd, (FAR struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      pthread_mutex_lock(&ctx->lock);
      ctx->state.wifi_connected = 0;
      snprintf(ctx->state.source_status,
               sizeof(ctx->state.source_status), "not_connected");
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      close(sockfd);
      return ERROR;
    }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
           path, host, port);

  if (write(sockfd, request, strlen(request)) < 0)
    {
      close(sockfd);
      return ERROR;
    }

  /* Configure audio: assume 16kHz/16bit/mono for stream */

  music_audio_configure(ctx->audio_fd, 1, 16000, 16);
  music_audio_start(ctx->audio_fd);

  pthread_mutex_lock(&ctx->lock);
  ctx->state.wifi_connected = 1;
  snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
           "connected");
  ctx->state.playing = 1;
  ctx->state.duration_sec = 0;
  ctx->state.position_sec = 0;
  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  printf("[music] streaming from %s:%d%s\n", host, port, path);

  /* Read and play */

  while (ctx->running && ctx->command != MUSIC_CMD_STOP)
    {
      if (ctx->command == MUSIC_CMD_PAUSE)
        {
          usleep(50000);
          continue;
        }

      nread = read(sockfd, buf, sizeof(buf));
      if (nread <= 0)
        {
          break;
        }

      if (!header_done)
        {
          for (i = 0; i < (int)nread - 3; i++)
            {
              if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                  buf[i + 2] == '\r' && buf[i + 3] == '\n')
                {
                  header_done = 1;
                  header_len = i + 4;
                  break;
                }
            }

          if (header_done && header_len < (int)nread)
            {
              write(ctx->audio_fd, buf + header_len, nread - header_len);
            }
        }
      else
        {
          write(ctx->audio_fd, buf, nread);
        }
    }

  music_audio_stop(ctx->audio_fd);
  close(sockfd);

  pthread_mutex_lock(&ctx->lock);
  ctx->state.wifi_connected = 0;
  snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
           "not_connected");
  ctx->state.playing = 0;
  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  printf("[music] stream ended\n");
  return OK;
}

/****************************************************************************
 * Bluetooth stub
 ****************************************************************************/

static int music_bt_start(FAR struct music_context_s *ctx)
{
  printf("[music] Bluetooth A2DP not yet implemented\n");
  printf("[music] Requires NuttX Bluetooth stack (CONFIG_BLUETOOTH)\n");

  pthread_mutex_lock(&ctx->lock);
  snprintf(ctx->state.title, sizeof(ctx->state.title),
           "蓝牙未就绪");
  snprintf(ctx->state.artist, sizeof(ctx->state.artist),
           "需要蓝牙协议栈支持");
  snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
           "not_ready");
  ctx->state.bluetooth_connected = 0;
  ctx->state.playing = 0;
  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  return OK;
}

static int music_probe_wifi_url(FAR const char *url)
{
  int sockfd;
  struct sockaddr_in addr;
  char host[128];
  char path[256];
  int port = 80;
  int ret;

  snprintf(host, sizeof(host), "10.0.0.1");
  snprintf(path, sizeof(path), "/stream");

  if (url != NULL && strncmp(url, "http://", 7) == 0)
    {
      FAR const char *p = url + 7;
      FAR const char *slash = strchr(p, '/');
      FAR const char *colon = strchr(p, ':');

      if (colon && (!slash || colon < slash))
        {
          size_t hlen = (size_t)(colon - p);
          if (hlen >= sizeof(host))
            {
              hlen = sizeof(host) - 1;
            }

          memcpy(host, p, hlen);
          host[hlen] = '\0';
          port = atoi(colon + 1);
        }
      else if (slash)
        {
          size_t hlen = (size_t)(slash - p);
          if (hlen >= sizeof(host))
            {
              hlen = sizeof(host) - 1;
            }

          memcpy(host, p, hlen);
          host[hlen] = '\0';
        }

      if (slash != NULL)
        {
          snprintf(path, sizeof(path), "%s", slash);
        }
    }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    {
      return ERROR;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
      close(sockfd);
      return ERROR;
    }

  ret = connect(sockfd, (FAR struct sockaddr *)&addr, sizeof(addr));
  close(sockfd);
  (void)path;
  return ret == 0 ? OK : ERROR;
}

static bool music_ipv4_is_valid(struct in_addr addr)
{
  uint32_t value = ntohl(addr.s_addr);

  return value != 0 && value != 0xffffffffUL &&
         (value & 0xff000000UL) != 0x7f000000UL;
}

static int music_probe_wifi_link(void)
{
  uint8_t flags;
  struct in_addr addr;

  if (netlib_getifstatus(MUSIC_WIFI_IFNAME, &flags) != OK)
    {
      return ERROR;
    }

  if ((flags & IFF_UP) == 0 || (flags & IFF_RUNNING) == 0)
    {
      return ERROR;
    }

  if (netlib_get_ipv4addr(MUSIC_WIFI_IFNAME, &addr) != OK ||
      !music_ipv4_is_valid(addr))
    {
      return ERROR;
    }

  return OK;
}

static void music_probe_sources(FAR struct music_context_s *ctx)
{
  int source_id;
  char source[16];
  char source_status[32];
  int wifi_link;
  int wifi_stream;

  pthread_mutex_lock(&ctx->lock);
  source_id = ctx->state.source_id;
  snprintf(source, sizeof(source), "%s", ctx->state.source);
  snprintf(source_status, sizeof(source_status), "%s",
           ctx->state.source_status);
  (void)music_scan_sdcard(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  wifi_link = music_probe_wifi_link() == OK ? 1 : 0;
  wifi_stream = wifi_link && music_probe_wifi_url(ctx->wifi_url) == OK;

  pthread_mutex_lock(&ctx->lock);
  ctx->state.wifi_connected = wifi_link;
  ctx->state.bluetooth_connected = 0;
  ctx->state.source_id = source_id;
  snprintf(ctx->state.source, sizeof(ctx->state.source), "%s", source);

  if (strcmp(source, "wifi") == 0)
    {
      snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
               wifi_stream ? "connected" :
               (wifi_link ? "stream_unreachable" : "not_connected"));
    }
  else if (strcmp(source, "bluetooth") == 0)
    {
      snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
               "not_ready");
    }
  else
    {
      snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
               ctx->state.sdcard_available ? "ready" : "no_media");
    }

  if (source_status[0] != '\0' && strcmp(source, "sdcard") != 0 &&
      strcmp(source, "wifi") != 0 && strcmp(source, "bluetooth") != 0)
    {
      snprintf(ctx->state.source_status, sizeof(ctx->state.source_status),
               "%s", source_status);
    }

  music_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);
}

/****************************************************************************
 * State persistence
 ****************************************************************************/

static int music_state_save(FAR struct music_state_s *state)
{
  char json[2048];
  int len;

  len = snprintf(json, sizeof(json),
    "{\n"
    "  \"title\": \"%s\",\n"
    "  \"artist\": \"%s\",\n"
    "  \"album\": \"%s\",\n"
    "  \"source\": \"%s\",\n"
    "  \"source_status\": \"%s\",\n"
    "  \"playing\": %d,\n"
    "  \"duration_sec\": %d,\n"
    "  \"position_sec\": %d,\n"
    "  \"volume\": %d,\n"
    "  \"track_index\": %d,\n"
    "  \"track_count\": %d,\n"
    "  \"sdcard_available\": %d,\n"
    "  \"wifi_connected\": %d,\n"
    "  \"bluetooth_connected\": %d\n"
    "}\n",
    state->title,
    state->artist,
    state->album,
    state->source,
    state->source_status,
    state->playing,
    state->duration_sec,
    state->position_sec,
    state->volume,
    state->track_index,
    state->track_count,
    state->sdcard_available,
    state->wifi_connected,
    state->bluetooth_connected);

  (void)len;
  return music_write_json(MUSIC_STATE_PATH, json);
}

static int music_state_load(FAR struct music_state_s *state)
{
  char buf[1024];

  if (music_read_file(MUSIC_STATE_PATH, buf, sizeof(buf)) != OK)
    {
      return ERROR;
    }

  music_json_extract_string(buf, "title", state->title,
                            sizeof(state->title));
  music_json_extract_string(buf, "artist", state->artist,
                            sizeof(state->artist));
  music_json_extract_string(buf, "album", state->album,
                            sizeof(state->album));
  music_json_extract_string(buf, "source", state->source,
                            sizeof(state->source));
  music_json_extract_string(buf, "source_status", state->source_status,
                            sizeof(state->source_status));
  music_json_extract_int(buf, "playing", &state->playing);
  music_json_extract_int(buf, "duration_sec", &state->duration_sec);
  music_json_extract_int(buf, "position_sec", &state->position_sec);
  music_json_extract_int(buf, "volume", &state->volume);
  music_json_extract_int(buf, "track_index", &state->track_index);
  music_json_extract_int(buf, "track_count", &state->track_count);
  music_json_extract_int(buf, "sdcard_available",
                         &state->sdcard_available);
  music_json_extract_int(buf, "wifi_connected", &state->wifi_connected);
  music_json_extract_int(buf, "bluetooth_connected",
                         &state->bluetooth_connected);

  /* Map source name to ID */

  if (strcmp(state->source, "wifi") == 0)
    {
      state->source_id = MUSIC_SOURCE_WIFI;
    }
  else if (strcmp(state->source, "bluetooth") == 0)
    {
      state->source_id = MUSIC_SOURCE_BT;
    }
  else
    {
      state->source_id = MUSIC_SOURCE_SDCARD;
    }

  return OK;
}

static void music_state_init(FAR struct music_state_s *state)
{
  memset(state, 0, sizeof(*state));
  snprintf(state->source, sizeof(state->source), "sdcard");
  snprintf(state->source_status, sizeof(state->source_status), "unknown");
  snprintf(state->title, sizeof(state->title), "未在播放");
  state->source_id = MUSIC_SOURCE_SDCARD;
  state->volume = 60;
}

/****************************************************************************
 * Playback thread
 ****************************************************************************/

static void *music_playback_thread(FAR void *arg)
{
  FAR struct music_context_s *ctx = (FAR struct music_context_s *)arg;

  printf("[music] playback thread started\n");

  while (ctx->running)
    {
      /* Wait for play command */

      if (!ctx->state.playing && ctx->command != MUSIC_CMD_STOP)
        {
          usleep(100000); /* 100ms */
          continue;
        }

      if (ctx->state.source_id == MUSIC_SOURCE_SDCARD)
        {
          /* SD card: play current track */

          if (ctx->state.track_count == 0)
            {
              music_scan_sdcard(&ctx->state);
              if (ctx->state.track_count == 0)
                {
                  pthread_mutex_lock(&ctx->lock);
                  snprintf(ctx->state.title, sizeof(ctx->state.title),
                           "未找到音乐文件");
                  snprintf(ctx->state.artist, sizeof(ctx->state.artist),
                           "请将 WAV 文件放入 %s", MUSIC_DIR);
                  ctx->state.playing = 0;
                  music_state_save(&ctx->state);
                  pthread_mutex_unlock(&ctx->lock);
                  usleep(2000000);
                  continue;
                }
            }

          int idx = clamp_int(ctx->state.track_index, 0,
                              ctx->state.track_count - 1);

          /* Extract title from filename (remove .wav extension) */

          FAR const char *name = ctx->state.tracks[idx].name;
          char title[64];
          snprintf(title, sizeof(title), "%s", name);
          FAR char *dot = strrchr(title, '.');
          if (dot)
            {
              *dot = '\0';
            }

          pthread_mutex_lock(&ctx->lock);
          snprintf(ctx->state.title, sizeof(ctx->state.title), "%s", title);
          ctx->state.artist[0] = '\0';
          music_state_save(&ctx->state);
          pthread_mutex_unlock(&ctx->lock);

          if (music_open_audio(ctx) != OK)
            {
              usleep(1000000);
              continue;
            }

          music_play_wav(ctx, ctx->state.tracks[idx].path);

          /* Auto-advance to next track if not manually stopped */

          if (ctx->command != MUSIC_CMD_STOP &&
              ctx->command != MUSIC_CMD_PREV)
            {
              ctx->state.track_index =
                (idx + 1) % ctx->state.track_count;
              ctx->command = MUSIC_CMD_PLAY;
              ctx->state.playing = 1;
            }
          else
            {
              ctx->state.playing = 0;
              ctx->command = MUSIC_CMD_NONE;
            }
        }
      else if (ctx->state.source_id == MUSIC_SOURCE_WIFI)
        {
          if (music_open_audio(ctx) != OK)
            {
              usleep(1000000);
              continue;
            }

          FAR const char *url = ctx->wifi_url;
          if (url[0] == '\0')
            {
              url = "http://10.0.0.1:8080/stream";
            }

          music_play_stream(ctx, url);
          ctx->command = MUSIC_CMD_NONE;
        }
      else if (ctx->state.source_id == MUSIC_SOURCE_BT)
        {
          music_bt_start(ctx);
          ctx->command = MUSIC_CMD_NONE;
          ctx->state.playing = 0;
          usleep(1000000);
        }
      else
        {
          usleep(500000);
        }
    }

  music_close_audio(ctx);
  printf("[music] playback thread exited\n");
  return NULL;
}

/****************************************************************************
 * CLI command handler
 ****************************************************************************/

static int music_handle_command(FAR struct music_context_s *ctx,
                               FAR const char *cmd, FAR const char *arg)
{
  if (strcmp(cmd, "play") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      if (ctx->state.track_count == 0 &&
          ctx->state.source_id == MUSIC_SOURCE_SDCARD)
        {
          music_scan_sdcard(&ctx->state);
        }

      ctx->state.playing = 1;
      ctx->command = MUSIC_CMD_PLAY;
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] play\n");
    }
  else if (strcmp(cmd, "pause") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      ctx->state.playing = 0;
      ctx->command = MUSIC_CMD_PAUSE;
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] pause\n");
    }
  else if (strcmp(cmd, "stop") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      ctx->state.playing = 0;
      ctx->state.position_sec = 0;
      ctx->command = MUSIC_CMD_STOP;
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] stop\n");
    }
  else if (strcmp(cmd, "next") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      if (ctx->state.track_count > 0)
        {
          ctx->state.track_index =
            (ctx->state.track_index + 1) % ctx->state.track_count;
          ctx->state.position_sec = 0;
          ctx->command = MUSIC_CMD_NEXT;
          ctx->state.playing = 1;
        }

      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] next -> track %d\n", ctx->state.track_index);
    }
  else if (strcmp(cmd, "prev") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      if (ctx->state.track_count > 0)
        {
          ctx->state.track_index =
            (ctx->state.track_index - 1 + ctx->state.track_count) %
            ctx->state.track_count;
          ctx->state.position_sec = 0;
          ctx->command = MUSIC_CMD_PREV;
          ctx->state.playing = 1;
        }

      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] prev -> track %d\n", ctx->state.track_index);
    }
  else if (strcmp(cmd, "source") == 0)
    {
      if (arg == NULL)
        {
          printf("[music] current source: %s\n", ctx->state.source);
          return OK;
        }

      pthread_mutex_lock(&ctx->lock);
      ctx->command = MUSIC_CMD_STOP;
      ctx->state.playing = 0;
      ctx->state.position_sec = 0;
      ctx->state.duration_sec = 0;

      if (strcmp(arg, "sdcard") == 0)
        {
          ctx->state.source_id = MUSIC_SOURCE_SDCARD;
          snprintf(ctx->state.source, sizeof(ctx->state.source), "sdcard");
          snprintf(ctx->state.source_status,
                   sizeof(ctx->state.source_status), "unknown");
          music_scan_sdcard(&ctx->state);
        }
      else if (strcmp(arg, "wifi") == 0)
        {
          ctx->state.source_id = MUSIC_SOURCE_WIFI;
          snprintf(ctx->state.source, sizeof(ctx->state.source), "wifi");
          snprintf(ctx->state.source_status,
                   sizeof(ctx->state.source_status),
                   ctx->state.wifi_connected ? "connected" :
                   "not_connected");
          ctx->state.track_count = 0;
          snprintf(ctx->state.title, sizeof(ctx->state.title),
                   "WiFi 流媒体");
        }
      else if (strcmp(arg, "bluetooth") == 0)
        {
          ctx->state.source_id = MUSIC_SOURCE_BT;
          snprintf(ctx->state.source, sizeof(ctx->state.source),
                   "bluetooth");
          snprintf(ctx->state.source_status,
                   sizeof(ctx->state.source_status), "not_ready");
          ctx->state.bluetooth_connected = 0;
          ctx->state.track_count = 0;
          snprintf(ctx->state.title, sizeof(ctx->state.title),
                   "蓝牙音频");
        }
      else
        {
          fprintf(stderr, "[music] unknown source: %s\n", arg);
          pthread_mutex_unlock(&ctx->lock);
          return ERROR;
        }

      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] source -> %s\n", arg);
    }
  else if (strcmp(cmd, "volume") == 0)
    {
      if (arg == NULL)
        {
          printf("[music] volume: %d%%\n", ctx->state.volume);
          return OK;
        }

      int vol = clamp_int(atoi(arg), 0, 100);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.volume = vol;
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] volume -> %d%%\n", vol);
    }
  else if (strcmp(cmd, "seek") == 0)
    {
      if (arg == NULL)
        {
          return ERROR;
        }

      int pos = atoi(arg);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.position_sec = clamp_int(pos, 0,
                                          ctx->state.duration_sec);
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] seek -> %ds\n", pos);
    }
  else if (strcmp(cmd, "track") == 0)
    {
      if (arg == NULL)
        {
          printf("[music] track: %d/%d\n",
                 ctx->state.track_index + 1, ctx->state.track_count);
          return OK;
        }

      int idx = atoi(arg);
      pthread_mutex_lock(&ctx->lock);
      if (idx >= 0 && idx < ctx->state.track_count)
        {
          ctx->state.track_index = idx;
          ctx->state.position_sec = 0;
          ctx->command = MUSIC_CMD_NEXT;
          ctx->state.playing = 1;
        }

      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
      printf("[music] track -> %d\n", idx);
    }
  else if (strcmp(cmd, "scan") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      music_scan_sdcard(&ctx->state);
      music_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
    }
  else if (strcmp(cmd, "probe") == 0)
    {
      music_probe_sources(ctx);
      printf("[music] source probe updated\n");
    }
  else if (strcmp(cmd, "status") == 0)
    {
      char buf[1024];
      if (music_read_file(MUSIC_STATE_PATH, buf, sizeof(buf)) == OK)
        {
          printf("%s\n", buf);
        }
      else
        {
          printf("{\"error\":\"no state\"}\n");
        }
    }
  else if (strcmp(cmd, "list") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      printf("[music] %d tracks:\n", ctx->state.track_count);
      for (int i = 0; i < ctx->state.track_count; i++)
        {
          printf("  [%d] %s\n", i, ctx->state.tracks[i].name);
        }

      pthread_mutex_unlock(&ctx->lock);
    }
  else
    {
      fprintf(stderr, "[music] unknown command: %s\n", cmd);
      return ERROR;
    }

  return OK;
}

static void music_print_usage(void)
{
  printf("Usage: qiban_music_service [command] [args]\n");
  printf("\n");
  printf("Playback:\n");
  printf("  play                 Resume / start playback\n");
  printf("  pause                Pause playback\n");
  printf("  stop                 Stop playback\n");
  printf("  next                 Next track\n");
  printf("  prev                 Previous track\n");
  printf("  seek <seconds>       Seek to position\n");
  printf("  track <index>        Jump to track number\n");
  printf("\n");
  printf("Source:\n");
  printf("  source sdcard        Play from SD card (%s)\n", MUSIC_DIR);
  printf("  source wifi          WiFi HTTP stream\n");
  printf("  source bluetooth     Bluetooth A2DP (stub)\n");
  printf("\n");
  printf("Control:\n");
  printf("  volume <0-100>       Set volume\n");
  printf("  scan                 Rescan SD card for music files\n");
  printf("  probe                Probe SD / WiFi / Bluetooth availability\n");
  printf("  list                 List all tracks\n");
  printf("  status               Print current state JSON\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR struct music_context_s *ctx = &g_music_ctx;
  FAR const char *env;

  memset(ctx, 0, sizeof(*ctx));
  ctx->audio_fd = -1;
  pthread_mutex_init(&ctx->lock, NULL);

  music_state_init(&ctx->state);

  /* Load WiFi stream URL from environment */

  env = getenv("QIBAN_MUSIC_WIFI_URL");
  if (env)
    {
      snprintf(ctx->wifi_url, sizeof(ctx->wifi_url), "%s", env);
    }

  snprintf(ctx->audio_dev, sizeof(ctx->audio_dev), "%s",
           music_resolve_audio_device());

  /* Load existing state */

  music_state_load(&ctx->state);

  /* Handle CLI commands */

  if (argc >= 2)
    {
      FAR const char *cmd = argv[1];
      FAR const char *arg = (argc >= 3) ? argv[2] : NULL;

      if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0)
        {
          music_print_usage();
          return OK;
        }

      /* For one-shot commands, just handle and exit */

      if (strcmp(cmd, "status") == 0 || strcmp(cmd, "list") == 0)
        {
          music_scan_sdcard(&ctx->state);
          return music_handle_command(ctx, cmd, arg);
        }

      if (strcmp(cmd, "probe") == 0)
        {
          return music_handle_command(ctx, cmd, arg);
        }

      return music_command_save(cmd, arg);
    }

  /* Daemon mode: scan music and start playback thread */

  printf("[music] daemon starting, source=%s, audio=%s\n",
         ctx->state.source, ctx->audio_dev);

  music_probe_sources(ctx);
  music_state_save(&ctx->state);

  ctx->running = 1;
  pthread_create(&ctx->play_thread, NULL, music_playback_thread, ctx);

  /* Main loop: read commands from stdin */

  char line[256];
  time_t next_probe;
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) |
        O_NONBLOCK);
  printf("[music] ready. type 'help' for commands.\n");
  next_probe = time(NULL) + MUSIC_PROBE_PERIOD_SEC;

  while (ctx->running)
    {
      char file_cmd[64];
      char file_arg[192];
      time_t now;

      if (music_command_load(file_cmd, sizeof(file_cmd),
                             file_arg, sizeof(file_arg)) == OK)
        {
          music_handle_command(ctx, file_cmd,
                               file_arg[0] != '\0' ? file_arg : NULL);
        }

      now = time(NULL);
      if (now >= next_probe)
        {
          music_probe_sources(ctx);
          next_probe = now + MUSIC_PROBE_PERIOD_SEC;
        }

      if (fgets(line, sizeof(line), stdin) == NULL)
        {
          clearerr(stdin);
          usleep(200000);
          continue;
        }

      /* Strip newline */

      FAR char *nl = strchr(line, '\n');
      if (nl)
        {
          *nl = '\0';
        }

      /* Parse command and argument */

      FAR char *cmd = strtok(line, " ");
      FAR char *arg = strtok(NULL, " ");

      if (cmd)
        {
          music_handle_command(ctx, cmd, arg);
        }
    }

  ctx->running = 0;
  ctx->command = MUSIC_CMD_STOP;
  pthread_join(ctx->play_thread, NULL);
  music_close_audio(ctx);

  printf("[music] daemon exited\n");
  return OK;
}
