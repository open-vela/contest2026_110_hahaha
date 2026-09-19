/****************************************************************************
 * qiban_video_service_main.c
 *
 * Local video playback service for the Qiban AI dashboard.
 * Uses R528 VE (Video Engine) hardware decoding via libcedarx XPlayer.
 *
 * Supported formats: MP4, MKV, AVI, FLV (H.264/H.265/MPEG4)
 *
 * Usage:
 *   qiban_video_service &                    # start daemon
 *   qiban_video_service list                 # list video files
 *   qiban_video_service play <index>         # play video by index
 *   qiban_video_service play <path>          # play video by path
 *   qiban_video_service pause                # pause playback
 *   qiban_video_service resume               # resume playback
 *   qiban_video_service stop                 # stop playback
 *   qiban_video_service seek <seconds>       # seek to position
 *   qiban_video_service status               # print state JSON
 *   qiban_video_service scan                 # rescan video directory
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

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

#define VIDEO_STATE_PATH       "/data/qiban_video_state.json"
#define VIDEO_DIR              "/data/videos"
#define VIDEO_DIR_ALT          "/mnt/sdcard/videos"
#define VIDEO_SDCARD_ROOT      "/mnt/sdcard"
#define VIDEO_DATA_ROOT        "/data"
#define VIDEO_TEMP_SUFFIX      ".tmp"
#define VIDEO_MAX_FILES        32
#define VIDEO_MAX_PATH         128
#define VIDEO_MAX_ENTRIES      64

#define VIDEO_STATE_STOPPED    0
#define VIDEO_STATE_PLAYING    1
#define VIDEO_STATE_PAUSED     2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct video_file_s
{
  char path[VIDEO_MAX_PATH];
  char name[64];
  int duration_sec;
  long file_size;
};

struct video_state_s
{
  int state;
  int current_index;
  int position_sec;
  int duration_sec;
  int file_count;
  char current_name[64];
  char error_msg[128];
  struct video_file_s files[VIDEO_MAX_FILES];
};

struct video_context_s
{
  struct video_state_s state;
  int running;
  int command;
  pthread_mutex_t lock;
  pthread_t play_thread;
  void *player;  /* XPlayer* when libcedarx available */
};

/****************************************************************************
 * Private data
 ****************************************************************************/

static struct video_context_s g_video_ctx;

/****************************************************************************
 * Helpers
 ****************************************************************************/

static int video_write_json(FAR const char *path, FAR const char *json)
{
  char tmp_path[128];
  FAR FILE *fp;

  snprintf(tmp_path, sizeof(tmp_path), "%s%s", path, VIDEO_TEMP_SUFFIX);
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

static int video_state_save(FAR struct video_state_s *s)
{
  char json[2048];
  const char *state_str;

  switch (s->state)
    {
      case VIDEO_STATE_PLAYING:
        state_str = "playing";
        break;
      case VIDEO_STATE_PAUSED:
        state_str = "paused";
        break;
      default:
        state_str = "stopped";
        break;
    }

  snprintf(json, sizeof(json),
    "{\n"
    "  \"state\": \"%s\",\n"
    "  \"current_index\": %d,\n"
    "  \"current_name\": \"%s\",\n"
    "  \"position_sec\": %d,\n"
    "  \"duration_sec\": %d,\n"
    "  \"file_count\": %d,\n"
    "  \"error_msg\": \"%s\"\n"
    "}\n",
    state_str,
    s->current_index,
    s->current_name,
    s->position_sec,
    s->duration_sec,
    s->file_count,
    s->error_msg);

  return video_write_json(VIDEO_STATE_PATH, json);
}

/****************************************************************************
 * Video file scanning
 ****************************************************************************/

static int video_scan_files(FAR struct video_state_s *s)
{
  FAR const char *dir_path;
  FAR DIR *dir;
  FAR struct dirent *entry;
  int count = 0;
  struct stat st;

  s->file_count = 0;

  dir_path = VIDEO_DIR;
  dir = opendir(dir_path);
  if (dir == NULL)
    {
      dir_path = VIDEO_DIR_ALT;
      dir = opendir(dir_path);
    }

  if (dir == NULL)
    {
      snprintf(s->error_msg, sizeof(s->error_msg),
               "no video directory found");
      return ERROR;
    }

  while ((entry = readdir(dir)) != NULL && count < VIDEO_MAX_FILES)
    {
      FAR const char *name = entry->d_name;
      size_t len = strlen(name);

      if (len < 5)
        {
          continue;
        }

      /* Accept common video formats */

      if (strcasecmp(name + len - 4, ".mp4") != 0 &&
          strcasecmp(name + len - 4, ".mkv") != 0 &&
          strcasecmp(name + len - 4, ".avi") != 0 &&
          strcasecmp(name + len - 4, ".flv") != 0 &&
          strcasecmp(name + len - 4, ".ts")  != 0 &&
          strcasecmp(name + len - 5, ".webm") != 0)
        {
          continue;
        }

      snprintf(s->files[count].path, VIDEO_MAX_PATH,
               "%s/%s", dir_path, name);
      snprintf(s->files[count].name, 64, "%s", name);
      s->files[count].duration_sec = 0;

      if (stat(s->files[count].path, &st) == 0)
        {
          s->files[count].file_size = st.st_size;
        }
      else
        {
          s->files[count].file_size = 0;
        }

      count++;
    }

  closedir(dir);

  /* Sort by name */

  for (int i = 0; i < count - 1; i++)
    {
      for (int j = i + 1; j < count; j++)
        {
          if (strcmp(s->files[i].name, s->files[j].name) > 0)
            {
              struct video_file_s tmp = s->files[i];
              s->files[i] = s->files[j];
              s->files[j] = tmp;
            }
        }
    }

  s->file_count = count;
  s->error_msg[0] = '\0';
  printf("[video] scanned %d video files from %s\n", count, dir_path);
  return OK;
}

/****************************************************************************
 * XPlayer integration (hardware decoding)
 *
 * When libcedarx is available, this uses the R528 VE hardware decoder.
 * The XPlayer API handles:
 *   - MP4/MKV/AVI/FLV demuxing (CdxParser)
 *   - H.264/H.265/MPEG4 video decoding (VE hardware)
 *   - AAC/MP3 audio decoding
 *   - Video output to DE display layer
 *   - Audio output via ALSA/sound framework
 ****************************************************************************/

#ifdef CONFIG_MULTIMEDIA_LIBCEDARX

#include "xplayer.h"
#include "layerControl.h"
#include "soundControl.h"

extern LayerCtrl *LayerCreate_DE(void);
extern SoundCtrl *TinaSoundDeviceInit(void);

static int video_xplayer_callback(void *puser, int msg,
                                  int ext1, void *para)
{
  FAR struct video_context_s *ctx = (FAR struct video_context_s *)puser;
  (void)ext1;
  (void)para;

  switch (msg)
    {
      case AWPLAYER_MEDIA_PREPARED:
        printf("[video] player prepared\n");
        break;

      case AWPLAYER_MEDIA_PLAYBACK_COMPLETE:
        printf("[video] playback complete\n");
        pthread_mutex_lock(&ctx->lock);
        ctx->state.state = VIDEO_STATE_STOPPED;
        ctx->state.position_sec = 0;
        video_state_save(&ctx->state);
        pthread_mutex_unlock(&ctx->lock);
        break;

      case AWPLAYER_MEDIA_ERROR:
        fprintf(stderr, "[video] player error: %d\n", ext1);
        pthread_mutex_lock(&ctx->lock);
        ctx->state.state = VIDEO_STATE_STOPPED;
        snprintf(ctx->state.error_msg, sizeof(ctx->state.error_msg),
                 "playback error %d", ext1);
        video_state_save(&ctx->state);
        pthread_mutex_unlock(&ctx->lock);
        break;

      case AWPLAYER_MEDIA_SET_VIDEO_SIZE:
        printf("[video] video size: %dx%d\n",
               ((int *)para)[0], ((int *)para)[1]);
        break;

      default:
        break;
    }

  return OK;
}

static int video_xplayer_start(FAR struct video_context_s *ctx,
                               FAR const char *path)
{
  XPlayer *player;
  LayerCtrl *layer;
  SoundCtrl *sound;
  int ret;

  printf("[video] starting XPlayer for: %s\n", path);

  player = XPlayerCreate();
  if (player == NULL)
    {
      fprintf(stderr, "[video] XPlayerCreate failed\n");
      return ERROR;
    }

  ret = XPlayerSetNotifyCallback(player, video_xplayer_callback, ctx);
  if (ret != OK)
    {
      fprintf(stderr, "[video] SetNotifyCallback failed: %d\n", ret);
      XPlayerDestroy(player);
      return ERROR;
    }

  ret = XPlayerInitCheck(player);
  if (ret != OK)
    {
      fprintf(stderr, "[video] InitCheck failed: %d\n", ret);
      XPlayerDestroy(player);
      return ERROR;
    }

  /* Set video display layer (DE hardware) */

  layer = LayerCreate_DE();
  if (layer != NULL)
    {
      XPlayerSetVideoSurfaceTexture(player, layer);
    }

  /* Set audio output */

  sound = TinaSoundDeviceInit();
  if (sound != NULL)
    {
      XPlayerSetAudioSink(player, sound);
    }

  /* Set data source */

  ret = XPlayerSetDataSourceUrl(player, path, NULL, NULL);
  if (ret != OK)
    {
      fprintf(stderr, "[video] SetDataSource failed: %d\n", ret);
      XPlayerDestroy(player);
      return ERROR;
    }

  /* Prepare and start */

  ret = XPlayerPrepare(player);
  if (ret != OK)
    {
      fprintf(stderr, "[video] Prepare failed: %d\n", ret);
      XPlayerDestroy(player);
      return ERROR;
    }

  ret = XPlayerStart(player);
  if (ret != OK)
    {
      fprintf(stderr, "[video] Start failed: %d\n", ret);
      XPlayerDestroy(player);
      return ERROR;
    }

  ctx->player = player;

  /* Get duration */

  int duration_ms = 0;
  XPlayerGetDuration(player, &duration_ms);

  pthread_mutex_lock(&ctx->lock);
  ctx->state.state = VIDEO_STATE_PLAYING;
  ctx->state.duration_sec = duration_ms / 1000;
  ctx->state.position_sec = 0;
  video_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);

  printf("[video] playing: %s (duration: %ds)\n",
         path, ctx->state.duration_sec);
  return OK;
}

static void video_xplayer_stop(FAR struct video_context_s *ctx)
{
  if (ctx->player != NULL)
    {
      XPlayerStop((XPlayer *)ctx->player);
      XPlayerDestroy((XPlayer *)ctx->player);
      ctx->player = NULL;
    }

  pthread_mutex_lock(&ctx->lock);
  ctx->state.state = VIDEO_STATE_STOPPED;
  ctx->state.position_sec = 0;
  video_state_save(&ctx->state);
  pthread_mutex_unlock(&ctx->lock);
}

static void video_xplayer_pause(FAR struct video_context_s *ctx)
{
  if (ctx->player != NULL)
    {
      XPlayerPause((XPlayer *)ctx->player);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.state = VIDEO_STATE_PAUSED;
      video_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
    }
}

static void video_xplayer_resume(FAR struct video_context_s *ctx)
{
  if (ctx->player != NULL)
    {
      XPlayerStart((XPlayer *)ctx->player);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.state = VIDEO_STATE_PLAYING;
      video_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
    }
}

static void video_xplayer_seek(FAR struct video_context_s *ctx,
                               int seconds)
{
  if (ctx->player != NULL)
    {
      XPlayerSeekTo((XPlayer *)ctx->player, seconds * 1000, 0);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.position_sec = seconds;
      video_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
    }
}

static void video_xplayer_update_position(FAR struct video_context_s *ctx)
{
  if (ctx->player != NULL && ctx->state.state == VIDEO_STATE_PLAYING)
    {
      int pos_ms = 0;
      XPlayerGetCurrentPosition((XPlayer *)ctx->player, &pos_ms);
      pthread_mutex_lock(&ctx->lock);
      ctx->state.position_sec = pos_ms / 1000;
      pthread_mutex_unlock(&ctx->lock);
    }
}

#else /* !CONFIG_MULTIMEDIA_LIBCEDARX */

/* Stub implementations when libcedarx is not available */

static int video_xplayer_start(FAR struct video_context_s *ctx,
                               FAR const char *path)
{
  (void)ctx;
  (void)path;
  printf("[video] libcedarx not available - hardware decoding disabled\n");
  printf("[video] enable CONFIG_MULTIMEDIA_LIBCEDARX in defconfig\n");
  return ERROR;
}

static void video_xplayer_stop(FAR struct video_context_s *ctx)
{
  (void)ctx;
}

static void video_xplayer_pause(FAR struct video_context_s *ctx)
{
  (void)ctx;
}

static void video_xplayer_resume(FAR struct video_context_s *ctx)
{
  (void)ctx;
}

static void video_xplayer_seek(FAR struct video_context_s *ctx, int sec)
{
  (void)ctx;
  (void)sec;
}

static void video_xplayer_update_position(FAR struct video_context_s *ctx)
{
  (void)ctx;
}

#endif /* CONFIG_MULTIMEDIA_LIBCEDARX */

/****************************************************************************
 * Playback thread
 ****************************************************************************/

static void *video_playback_thread(FAR void *arg)
{
  FAR struct video_context_s *ctx = (FAR struct video_context_s *)arg;

  printf("[video] playback thread started\n");

  while (ctx->running)
    {
      if (ctx->state.state == VIDEO_STATE_PLAYING)
        {
          video_xplayer_update_position(ctx);

          /* Save state periodically (every 5 seconds) */

          if (ctx->state.position_sec % 5 == 0)
            {
              pthread_mutex_lock(&ctx->lock);
              video_state_save(&ctx->state);
              pthread_mutex_unlock(&ctx->lock);
            }
        }

      usleep(1000000); /* 1 second */
    }

  video_xplayer_stop(ctx);
  printf("[video] playback thread exited\n");
  return NULL;
}

/****************************************************************************
 * CLI command handler
 ****************************************************************************/

static int video_handle_command(FAR struct video_context_s *ctx,
                               FAR const char *cmd, FAR const char *arg)
{
  if (strcmp(cmd, "list") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      printf("[video] %d files:\n", ctx->state.file_count);
      for (int i = 0; i < ctx->state.file_count; i++)
        {
          printf("  [%d] %s (%ld KB)\n", i,
                 ctx->state.files[i].name,
                 ctx->state.files[i].file_size / 1024);
        }
      pthread_mutex_unlock(&ctx->lock);
    }
  else if (strcmp(cmd, "play") == 0)
    {
      if (arg == NULL)
        {
          fprintf(stderr, "[video] usage: play <index|path>\n");
          return ERROR;
        }

      /* Stop current playback first */

      video_xplayer_stop(ctx);

      pthread_mutex_lock(&ctx->lock);

      FAR const char *path = NULL;
      int idx = atoi(arg);

      if (arg[0] >= '0' && arg[0] <= '9')
        {
          /* Play by index */

          if (idx >= 0 && idx < ctx->state.file_count)
            {
              ctx->state.current_index = idx;
              path = ctx->state.files[idx].path;
              snprintf(ctx->state.current_name,
                       sizeof(ctx->state.current_name),
                       "%s", ctx->state.files[idx].name);
            }
          else
            {
              fprintf(stderr, "[video] index out of range: %d\n", idx);
              pthread_mutex_unlock(&ctx->lock);
              return ERROR;
            }
        }
      else
        {
          /* Play by path */

          path = arg;
          FAR const char *slash = strrchr(arg, '/');
          snprintf(ctx->state.current_name,
                   sizeof(ctx->state.current_name),
                   "%s", slash ? slash + 1 : arg);
          ctx->state.current_index = -1;
        }

      pthread_mutex_unlock(&ctx->lock);

      return video_xplayer_start(ctx, path);
    }
  else if (strcmp(cmd, "pause") == 0)
    {
      video_xplayer_pause(ctx);
      printf("[video] paused\n");
    }
  else if (strcmp(cmd, "resume") == 0)
    {
      video_xplayer_resume(ctx);
      printf("[video] resumed\n");
    }
  else if (strcmp(cmd, "stop") == 0)
    {
      video_xplayer_stop(ctx);
      printf("[video] stopped\n");
    }
  else if (strcmp(cmd, "seek") == 0)
    {
      if (arg == NULL)
        {
          return ERROR;
        }

      video_xplayer_seek(ctx, atoi(arg));
      printf("[video] seek -> %ds\n", atoi(arg));
    }
  else if (strcmp(cmd, "scan") == 0)
    {
      pthread_mutex_lock(&ctx->lock);
      video_scan_files(&ctx->state);
      video_state_save(&ctx->state);
      pthread_mutex_unlock(&ctx->lock);
    }
  else if (strcmp(cmd, "status") == 0)
    {
      char buf[1024];
      FILE *fp = fopen(VIDEO_STATE_PATH, "r");
      if (fp != NULL)
        {
          size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
          fclose(fp);
          buf[n] = '\0';
          printf("%s\n", buf);
        }
      else
        {
          printf("{\"error\":\"no state\"}\n");
        }
    }
  else if (strcmp(cmd, "ls") == 0)
    {
      browser_ls(&g_browser, arg);
    }
  else if (strcmp(cmd, "cd") == 0)
    {
      browser_cd(&g_browser, arg);
    }
  else if (strcmp(cmd, "pwd") == 0)
    {
      printf("%s\n", g_browser.cwd);
    }
  else if (strcmp(cmd, "info") == 0)
    {
      if (arg == NULL)
        {
          fprintf(stderr, "[browser] usage: info <path>\n");
          return ERROR;
        }

      char full_path[VIDEO_MAX_PATH];
      if (arg[0] == '/')
        {
          snprintf(full_path, sizeof(full_path), "%s", arg);
        }
      else
        {
          snprintf(full_path, sizeof(full_path), "%s/%s",
                   g_browser.cwd, arg);
        }

      browser_info(full_path);
    }
  else if (strcmp(cmd, "tree") == 0)
    {
      FAR const char *path = arg ? arg : g_browser.cwd;
      int depth = 2;
      FAR char *space = arg ? strchr(arg, ' ') : NULL;
      if (space)
        {
          *space = '\0';
          depth = atoi(space + 1);
          path = arg;
        }

      browser_tree(path, 0, depth);
    }
  else if (strcmp(cmd, "sdcard") == 0)
    {
      browser_cd(&g_browser, VIDEO_SDCARD_ROOT);
    }
  else if (strcmp(cmd, "data") == 0)
    {
      browser_cd(&g_browser, VIDEO_DATA_ROOT);
    }
  else if (strcmp(cmd, "videos") == 0)
    {
      /* List video files in current directory or video dir */

      FAR const char *path = arg ? arg : NULL;
      if (path == NULL)
        {
          /* Try current dir first, then default video dir */

          struct stat st;
          char video_dir[VIDEO_MAX_PATH];
          snprintf(video_dir, sizeof(video_dir), "%s", g_browser.cwd);
          path = video_dir;
        }

      browser_ls(&g_browser, path);
    }
  else
    {
      fprintf(stderr, "[video] unknown command: %s\n", cmd);
      return ERROR;
    }

  return OK;
}

/****************************************************************************
 * SD Card File Browser
 ****************************************************************************/

struct file_entry_s
{
  char name[64];
  char path[VIDEO_MAX_PATH];
  int is_dir;
  long size;
};

struct file_browser_s
{
  char cwd[VIDEO_MAX_PATH];
  struct file_entry_s entries[VIDEO_MAX_ENTRIES];
  int entry_count;
};

static struct file_browser_s g_browser;

static int browser_is_video(FAR const char *name)
{
  size_t len = strlen(name);
  if (len < 5) return 0;
  return (strcasecmp(name + len - 4, ".mp4") == 0 ||
          strcasecmp(name + len - 4, ".mkv") == 0 ||
          strcasecmp(name + len - 4, ".avi") == 0 ||
          strcasecmp(name + len - 4, ".flv") == 0 ||
          strcasecmp(name + len - 3, ".ts")  == 0 ||
          strcasecmp(name + len - 5, ".webm") == 0);
}

static int browser_is_image(FAR const char *name)
{
  size_t len = strlen(name);
  if (len < 5) return 0;
  return (strcasecmp(name + len - 4, ".png") == 0 ||
          strcasecmp(name + len - 4, ".jpg") == 0 ||
          strcasecmp(name + len - 5, ".jpeg") == 0 ||
          strcasecmp(name + len - 4, ".bmp") == 0);
}

static int browser_is_audio(FAR const char *name)
{
  size_t len = strlen(name);
  if (len < 5) return 0;
  return (strcasecmp(name + len - 4, ".wav") == 0 ||
          strcasecmp(name + len - 4, ".pcm") == 0 ||
          strcasecmp(name + len - 4, ".mp3") == 0 ||
          strcasecmp(name + len - 4, ".aac") == 0);
}

static const char *browser_file_icon(FAR const char *name)
{
  if (browser_is_video(name)) return LV_SYMBOL_IMAGE;
  if (browser_is_image(name)) return LV_SYMBOL_IMAGE;
  if (browser_is_audio(name)) return LV_SYMBOL_AUDIO;
  size_t len = strlen(name);
  if (len > 4 && strcasecmp(name + len - 4, ".txt") == 0) return LV_SYMBOL_FILE;
  if (len > 4 && strcasecmp(name + len - 4, ".log") == 0) return LV_SYMBOL_FILE;
  if (len > 4 && strcasecmp(name + len - 4, ".json") == 0) return LV_SYMBOL_FILE;
  return LV_SYMBOL_DRIVE;
}

static void browser_init(FAR struct file_browser_s *b)
{
  snprintf(b->cwd, sizeof(b->cwd), "/");
  b->entry_count = 0;
}

static int browser_ls(FAR struct file_browser_s *b, FAR const char *path)
{
  FAR DIR *dir;
  FAR struct dirent *entry;
  struct stat st;
  int count = 0;

  if (path == NULL)
    {
      path = b->cwd;
    }

  /* Try to stat as file first */

  if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode))
    {
      /* It's a file, show info */

      printf("[browser] %s (%ld bytes)\n", path, st.st_size);
      if (browser_is_video(path))
        {
          printf("[browser] video file - use 'play %s' to play\n", path);
        }

      return OK;
    }

  dir = opendir(path);
  if (dir == NULL)
    {
      fprintf(stderr, "[browser] cannot open: %s\n", path);
      return ERROR;
    }

  snprintf(b->cwd, sizeof(b->cwd), "%s", path);
  b->entry_count = 0;

  printf("[browser] listing: %s\n", path);
  printf("  %-4s  %-8s  %s\n", "Type", "Size", "Name");
  printf("  ----  --------  ----\n");

  while ((entry = readdir(dir)) != NULL && count < VIDEO_MAX_ENTRIES)
    {
      FAR const char *name = entry->d_name;

      /* Skip . and .. */

      if (name[0] == '.' && (name[1] == '\0' ||
          (name[1] == '.' && name[2] == '\0')))
        {
          continue;
        }

      char full_path[VIDEO_MAX_PATH];
      snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

      snprintf(b->entries[count].name, 64, "%s", name);
      snprintf(b->entries[count].path, VIDEO_MAX_PATH, "%s", full_path);

      if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
          b->entries[count].is_dir = 1;
          b->entries[count].size = 0;
          printf("  [DIR]  %-8s  %s/\n", "", name);
        }
      else
        {
          b->entries[count].is_dir = 0;
          b->entries[count].size = st.st_size;
          const char *icon = browser_file_icon(name);
          const char *type = "";
          if (browser_is_video(name)) type = "VIDEO";
          else if (browser_is_image(name)) type = "IMAGE";
          else if (browser_is_audio(name)) type = "AUDIO";
          else type = "FILE";

          if (st.st_size > 1048576)
            {
              printf("  %-4s  %4ldMB  %s %s\n", type,
                     st.st_size / 1048576, icon, name);
            }
          else if (st.st_size > 1024)
            {
              printf("  %-4s  %4ldKB  %s %s\n", type,
                     st.st_size / 1024, icon, name);
            }
          else
            {
              printf("  %-4s  %4ldB   %s %s\n", type,
                     st.st_size, icon, name);
            }
        }

      count++;
    }

  closedir(dir);
  b->entry_count = count;
  printf("\n  %d items\n", count);
  return OK;
}

static int browser_cd(FAR struct file_browser_s *b, FAR const char *path)
{
  struct stat st;

  if (path == NULL || strcmp(path, "~") == 0)
    {
      snprintf(b->cwd, sizeof(b->cwd), "/");
      return OK;
    }

  /* Handle .. */

  if (strcmp(path, "..") == 0)
    {
      FAR char *slash = strrchr(b->cwd, '/');
      if (slash != NULL && slash != b->cwd)
        {
          *slash = '\0';
        }
      else
        {
          snprintf(b->cwd, sizeof(b->cwd), "/");
        }

      return OK;
    }

  /* Handle absolute path */

  char full_path[VIDEO_MAX_PATH];
  if (path[0] == '/')
    {
      snprintf(full_path, sizeof(full_path), "%s", path);
    }
  else
    {
      snprintf(full_path, sizeof(full_path), "%s/%s", b->cwd, path);
    }

  if (stat(full_path, &st) != 0)
    {
      fprintf(stderr, "[browser] not found: %s\n", full_path);
      return ERROR;
    }

  if (!S_ISDIR(st.st_mode))
    {
      /* It's a file - show info and suggest play */

      printf("[browser] %s (%ld bytes)\n", full_path, st.st_size);
      if (browser_is_video(full_path))
        {
          printf("[browser] play %s\n", full_path);
        }

      return OK;
    }

  snprintf(b->cwd, sizeof(b->cwd), "%s", full_path);
  return browser_ls(b, b->cwd);
}

static int browser_info(FAR const char *path)
{
  struct stat st;

  if (stat(path, &st) != 0)
    {
      fprintf(stderr, "[browser] not found: %s\n", path);
      return ERROR;
    }

  printf("[browser] %s\n", path);
  printf("  Size: %ld bytes", st.st_size);
  if (st.st_size > 1048576)
    {
      printf(" (%ld MB)", st.st_size / 1048576);
    }
  else if (st.st_size > 1024)
    {
      printf(" (%ld KB)", st.st_size / 1024);
    }

  printf("\n");

  if (S_ISDIR(st.st_mode))
    {
      printf("  Type: directory\n");
    }
  else if (browser_is_video(path))
    {
      printf("  Type: video\n");
      printf("  Command: play %s\n", path);
    }
  else if (browser_is_image(path))
    {
      printf("  Type: image\n");
    }
  else if (browser_is_audio(path))
    {
      printf("  Type: audio\n");
      printf("  Command: qiban_music_service play %s\n", path);
    }
  else
    {
      printf("  Type: file\n");
    }

  return OK;
}

static int browser_tree(FAR const char *path, int depth, int max_depth)
{
  FAR DIR *dir;
  FAR struct dirent *entry;
  struct stat st;

  if (depth > max_depth)
    {
      return OK;
    }

  dir = opendir(path);
  if (dir == NULL)
    {
      return ERROR;
    }

  while ((entry = readdir(dir)) != NULL)
    {
      FAR const char *name = entry->d_name;
      if (name[0] == '.' && (name[1] == '\0' ||
          (name[1] == '.' && name[2] == '\0')))
        {
          continue;
        }

      char full_path[VIDEO_MAX_PATH];
      snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

      for (int i = 0; i < depth; i++)
        {
          printf("  ");
        }

      if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
          printf("%s/\n", name);
          browser_tree(full_path, depth + 1, max_depth);
        }
      else
        {
          const char *icon = browser_file_icon(name);
          if (st.st_size > 1048576)
            {
              printf("%s %s (%ldMB)\n", icon, name,
                     st.st_size / 1048576);
            }
          else if (st.st_size > 1024)
            {
              printf("%s %s (%ldKB)\n", icon, name,
                     st.st_size / 1024);
            }
          else
            {
              printf("%s %s (%ldB)\n", icon, name, st.st_size);
            }
        }
    }

  closedir(dir);
  return OK;
}

static void video_print_usage(void)
{
  printf("Usage: qiban_video_service [command] [args]\n");
  printf("\n");
  printf("File Browser:\n");
  printf("  ls [path]          List directory contents\n");
  printf("  cd <path>          Change directory\n");
  printf("  pwd                Print current directory\n");
  printf("  info <path>        Show file details\n");
  printf("  tree [path] [depth] Show directory tree\n");
  printf("  find <name>        Search for files (TODO)\n");
  printf("\n");
  printf("Video Playback:\n");
  printf("  videos             List video files in current dir\n");
  printf("  play <index>       Play video by index\n");
  printf("  play <path>        Play video by file path\n");
  printf("  pause              Pause playback\n");
  printf("  resume             Resume playback\n");
  printf("  stop               Stop playback\n");
  printf("  seek <seconds>     Seek to position\n");
  printf("  status             Print current state JSON\n");
  printf("\n");
  printf("SD Card Roots:\n");
  printf("  sdcard             cd to /mnt/sdcard\n");
  printf("  data               cd to /data\n");
  printf("\n");
  printf("Supported formats: MP4, MKV, AVI, FLV, TS, WebM\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR struct video_context_s *ctx = &g_video_ctx;

  memset(ctx, 0, sizeof(*ctx));
  pthread_mutex_init(&ctx->lock, NULL);

  /* Scan video files */

  video_scan_files(&ctx->state);
  video_state_save(&ctx->state);

  /* Initialize file browser */

  browser_init(&g_browser);

  /* Handle CLI commands */

  if (argc >= 2)
    {
      FAR const char *cmd = argv[1];
      FAR const char *arg = (argc >= 3) ? argv[2] : NULL;

      if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0)
        {
          video_print_usage();
          return OK;
        }

      if (strcmp(cmd, "list") == 0 || strcmp(cmd, "status") == 0 ||
          strcmp(cmd, "scan") == 0)
        {
          return video_handle_command(ctx, cmd, arg);
        }

      /* For playback commands, start the playback thread */

      ctx->running = 1;
      pthread_create(&ctx->play_thread, NULL, video_playback_thread, ctx);

      video_handle_command(ctx, cmd, arg);

      /* Keep running */

      while (ctx->running)
        {
          usleep(500000);
        }

      pthread_join(ctx->play_thread, NULL);
      return OK;
    }

  /* Daemon mode */

  printf("[video] daemon starting, %d files found\n",
         ctx->state.file_count);

  ctx->running = 1;
  pthread_create(&ctx->play_thread, NULL, video_playback_thread, ctx);

  /* Read commands from stdin */

  char line[256];
  printf("[video] ready. type 'help' for commands.\n");

  while (ctx->running)
    {
      if (fgets(line, sizeof(line), stdin) == NULL)
        {
          break;
        }

      FAR char *nl = strchr(line, '\n');
      if (nl)
        {
          *nl = '\0';
        }

      FAR char *command = strtok(line, " ");
      FAR char *argument = strtok(NULL, " ");

      if (command)
        {
          video_handle_command(ctx, command, argument);
        }
    }

  ctx->running = 0;
  video_xplayer_stop(ctx);
  pthread_join(ctx->play_thread, NULL);

  printf("[video] daemon exited\n");
  return OK;
}
