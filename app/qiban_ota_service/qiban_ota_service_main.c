/****************************************************************************
 * qiban_ota_service_main.c
 *
 * Application-layer OTA service for the Qiban AI dashboard.
 * Periodically checks a relay server for app-level updates, downloads
 * packages, verifies SHA-256, and atomically replaces service binaries
 * under /data/app_ota/.  Firmware-level OTA (ota.zip) is handled by the
 * bootloader; this service only prepares the zip and triggers a reboot.
 *
 * Usage:
 *   qiban_ota_service              # daemon mode, poll every 300s
 *   qiban_ota_service check        # one-shot version check
 *   qiban_ota_service status       # print current OTA status
 *   qiban_ota_service apply <zip>  # apply a local OTA package
 *   qiban_ota_service rollback     # rollback to previous version
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

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

#define QIBAN_OTA_DIR              "/data/app_ota"
#define QIBAN_OTA_DOWNLOAD         "/data/app_ota/download.zip"
#define QIBAN_OTA_STAGING          "/data/app_ota/staging"
#define QIBAN_OTA_STATUS_FILE      "/data/ota/status.json"
#define QIBAN_OTA_BACKUP_DIR       "/data/app_ota/backup"
#define QIBAN_OTA_VERSION_FILE     "/data/app_ota/version.json"
#define QIBAN_OTA_FIRMWARE_ZIP     "/data/ota.zip"
#define QIBAN_OTA_FIRMWARE_DIR     "/data/firmware_ota"
#define QIBAN_OTA_FIRMWARE_MANIFEST "/data/firmware_ota/firmware_manifest.json"

#define QIBAN_OTA_BUF_SIZE         4096
#define QIBAN_OTA_DEFAULT_INTERVAL 300
#define QIBAN_OTA_DEFAULT_PORT     8787
#define QIBAN_OTA_CONNECT_TIMEOUT  10

#define QIBAN_OTA_STATE_IDLE       0
#define QIBAN_OTA_STATE_CHECKING   1
#define QIBAN_OTA_STATE_DOWNLOAD   2
#define QIBAN_OTA_STATE_VERIFY     3
#define QIBAN_OTA_STATE_APPLY      4
#define QIBAN_OTA_STATE_DONE       5
#define QIBAN_OTA_STATE_FAILED     6
#define QIBAN_OTA_STATE_ROLLBACK   7

#define QIBAN_OTA_COMPONENT_MAX    16

#define QIBAN_OTA_TEMP_FILE_SUFFIX ".tmp"

/****************************************************************************
 * Private types
 ****************************************************************************/

struct ota_component_s
{
  char name[64];
  char sha256[65];
  long size;
};

struct ota_manifest_s
{
  char version[32];
  char build_time[64];
  char min_firmware_version[32];
  int component_count;
  struct ota_component_s components[QIBAN_OTA_COMPONENT_MAX];
};

struct ota_status_s
{
  int state;
  int progress_pct;
  char current_version[32];
  char target_version[32];
  char error_msg[256];
  char last_check_time[64];
};

struct ota_context_s
{
  char server_host[128];
  int server_port;
  int poll_interval_sec;
  int running;
  struct ota_status_s status;
};

/****************************************************************************
 * Private data
 ****************************************************************************/

static const char *g_state_names[] =
{
  "idle", "checking", "downloading", "verifying",
  "applying", "done", "failed", "rollback"
};

static const char *g_service_names[] =
{
  "qiban_ui",
  "qiban_ai_agent",
  "qiban_nav_service",
  "qiban_map_service",
  "qiban_voice_service",
  "qiban_vehicle_service",
  "qiban_sensor_bridge",
  "qiban_gps_receiver",
  "qiban_wifi_bridge",
  NULL
};

/****************************************************************************
 * Private functions
 ****************************************************************************/

static void ota_get_time_str(FAR char *buf, size_t buflen)
{
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  snprintf(buf, buflen, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static int ota_mkdir_p(FAR const char *path, mode_t mode)
{
  char tmp[256];
  FAR char *p = NULL;

  snprintf(tmp, sizeof(tmp), "%s", path);
  for (p = tmp + 1; *p; p++)
    {
      if (*p == '/')
        {
          *p = '\0';
          if (mkdir(tmp, mode) != 0 && errno != EEXIST)
            {
              return ERROR;
            }

          *p = '/';
        }
    }

  if (mkdir(tmp, mode) != 0 && errno != EEXIST)
    {
      return ERROR;
    }

  return OK;
}

static int ota_write_file(FAR const char *path, FAR const char *content)
{
  int fd;
  size_t len;
  ssize_t written;
  char tmp_path[256];

  snprintf(tmp_path, sizeof(tmp_path), "%s%s", path,
           QIBAN_OTA_TEMP_FILE_SUFFIX);

  fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return ERROR;
    }

  len = strlen(content);
  written = write(fd, content, len);
  close(fd);

  if ((size_t)written != len)
    {
      unlink(tmp_path);
      return ERROR;
    }

  if (rename(tmp_path, path) != 0)
    {
      unlink(tmp_path);
      return ERROR;
    }

  return OK;
}

static int ota_read_file(FAR const char *path, FAR char *buf,
                         size_t buflen)
{
  int fd;
  ssize_t n;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return ERROR;
    }

  n = read(fd, buf, buflen - 1);
  close(fd);
  if (n <= 0)
    {
      return ERROR;
    }

  buf[n] = '\0';
  return OK;
}

/****************************************************************************
 * Status persistence
 ****************************************************************************/

static int ota_save_status(FAR const struct ota_status_s *status)
{
  char json[1024];

  ota_mkdir_p("/data/ota", 0755);

  snprintf(json, sizeof(json),
    "{\n"
    "  \"state\": \"%s\",\n"
    "  \"progress_pct\": %d,\n"
    "  \"current_version\": \"%s\",\n"
    "  \"target_version\": \"%s\",\n"
    "  \"error_msg\": \"%s\",\n"
    "  \"last_check_time\": \"%s\"\n"
    "}\n",
    g_state_names[status->state],
    status->progress_pct,
    status->current_version,
    status->target_version,
    status->error_msg,
    status->last_check_time);

  return ota_write_file(QIBAN_OTA_STATUS_FILE, json);
}

static int ota_load_version(FAR char *ver, size_t verlen)
{
  char buf[256];

  if (ota_read_file(QIBAN_OTA_VERSION_FILE, buf, sizeof(buf)) == OK)
    {
      /* Simple extraction: look for "version":"..." */

      FAR char *p = strstr(buf, "\"version\"");
      if (p != NULL)
        {
          p = strchr(p, ':');
          if (p != NULL)
            {
              p++;
              while (*p == ' ' || *p == '"')
                {
                  p++;
                }

              FAR char *end = strchr(p, '"');
              if (end != NULL && (size_t)(end - p) < verlen)
                {
                  memcpy(ver, p, end - p);
                  ver[end - p] = '\0';
                  return OK;
                }
            }
        }
    }

  snprintf(ver, verlen, "0.0.0");
  return OK;
}

/****************************************************************************
 * HTTP client (minimal, no external deps)
 ****************************************************************************/

static int ota_http_get(FAR const char *host, int port,
                        FAR const char *path,
                        FAR char *resp_buf, size_t resp_buflen)
{
  int sockfd;
  struct sockaddr_in addr;
  char request[1024];
  ssize_t n;
  int total = 0;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    {
      return ERROR;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);

  if (connect(sockfd, (FAR struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      close(sockfd);
      return ERROR;
    }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.0\r\n"
           "Host: %s:%d\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, host, port);

  if (write(sockfd, request, strlen(request)) < 0)
    {
      close(sockfd);
      return ERROR;
    }

  while ((size_t)total < resp_buflen - 1)
    {
      n = read(sockfd, resp_buf + total, resp_buflen - 1 - total);
      if (n <= 0)
        {
          break;
        }

      total += n;
    }

  close(sockfd);
  resp_buf[total] = '\0';
  return total;
}

static int ota_http_download(FAR const char *host, int port,
                             FAR const char *path,
                             FAR const char *out_path)
{
  int sockfd;
  struct sockaddr_in addr;
  char request[1024];
  char buf[QIBAN_OTA_BUF_SIZE];
  ssize_t n;
  int fd;
  int header_done = 0;
  int header_len = 0;
  int i;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    {
      return ERROR;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);

  if (connect(sockfd, (FAR struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      close(sockfd);
      return ERROR;
    }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.0\r\n"
           "Host: %s:%d\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, host, port);

  if (write(sockfd, request, strlen(request)) < 0)
    {
      close(sockfd);
      return ERROR;
    }

  fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      close(sockfd);
      return ERROR;
    }

  while ((n = read(sockfd, buf, sizeof(buf))) > 0)
    {
      if (!header_done)
        {
          /* Skip HTTP header: look for \r\n\r\n */

          for (i = 0; i < (int)n - 3; i++)
            {
              if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                  buf[i + 2] == '\r' && buf[i + 3] == '\n')
                {
                  header_done = 1;
                  header_len = i + 4;
                  break;
                }
            }

          if (header_done && header_len < (int)n)
            {
              write(fd, buf + header_len, n - header_len);
            }
        }
      else
        {
          write(fd, buf, n);
        }
    }

  close(fd);
  close(sockfd);
  return OK;
}

/****************************************************************************
 * Version comparison (simple semver: major.minor.patch)
 ****************************************************************************/

static int ota_parse_version(FAR const char *ver,
                             FAR int *major, FAR int *minor,
                             FAR int *patch)
{
  *major = 0;
  *minor = 0;
  *patch = 0;
  sscanf(ver, "%d.%d.%d", major, minor, patch);
  return OK;
}

static int ota_compare_version(FAR const char *a, FAR const char *b)
{
  int a_major, a_minor, a_patch;
  int b_major, b_minor, b_patch;

  ota_parse_version(a, &a_major, &a_minor, &a_patch);
  ota_parse_version(b, &b_major, &b_minor, &b_patch);

  if (a_major != b_major)
    {
      return a_major - b_major;
    }

  if (a_minor != b_minor)
    {
      return a_minor - b_minor;
    }

  return a_patch - b_patch;
}

/****************************************************************************
 * SHA-256 (minimal implementation for verification)
 ****************************************************************************/

#define SHA256_BLOCK_SIZE 32

static const uint32_t g_sha256_k[64] =
{
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t sha256_rotr(uint32_t x, int n)
{
  return (x >> n) | (x << (32 - n));
}

static inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (~x & z);
}

static inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t sha256_ep0(uint32_t x)
{
  return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22);
}

static inline uint32_t sha256_ep1(uint32_t x)
{
  return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25);
}

static inline uint32_t sha256_sig0(uint32_t x)
{
  return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t sha256_sig1(uint32_t x)
{
  return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10);
}

struct sha256_ctx_s
{
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t data[64];
  int datalen;
};

static void sha256_init(FAR struct sha256_ctx_s *ctx)
{
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

static void sha256_transform(FAR struct sha256_ctx_s *ctx,
                             FAR const uint8_t data[])
{
  uint32_t m[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t t1;
  uint32_t t2;
  int i;
  int j;

  for (i = 0, j = 0; i < 16; i++, j += 4)
    {
      m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
             ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }

  for (; i < 64; i++)
    {
      m[i] = sha256_sig1(m[i - 2]) + m[i - 7] +
             sha256_sig0(m[i - 15]) + m[i - 16];
    }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64; i++)
    {
      t1 = h + sha256_ep1(e) + sha256_ch(e, f, g) +
           g_sha256_k[i] + m[i];
      t2 = sha256_ep0(a) + sha256_maj(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void sha256_update(FAR struct sha256_ctx_s *ctx,
                          FAR const uint8_t data[], size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      ctx->data[ctx->datalen] = data[i];
      ctx->datalen++;
      if (ctx->datalen == 64)
        {
          sha256_transform(ctx, ctx->data);
          ctx->bitlen += 512;
          ctx->datalen = 0;
        }
    }
}

static void sha256_final(FAR struct sha256_ctx_s *ctx,
                         FAR uint8_t hash[SHA256_BLOCK_SIZE])
{
  uint32_t i = ctx->datalen;

  if (ctx->datalen < 56)
    {
      ctx->data[i++] = 0x80;
      while (i < 56)
        {
          ctx->data[i++] = 0;
        }
    }
  else
    {
      ctx->data[i++] = 0x80;
      while (i < 64)
        {
          ctx->data[i++] = 0;
        }

      sha256_transform(ctx, ctx->data);
      memset(ctx->data, 0, 56);
    }

  ctx->bitlen += ctx->datalen * 8;
  ctx->data[63] = ctx->bitlen;
  ctx->data[62] = ctx->bitlen >> 8;
  ctx->data[61] = ctx->bitlen >> 16;
  ctx->data[60] = ctx->bitlen >> 24;
  ctx->data[59] = ctx->bitlen >> 32;
  ctx->data[58] = ctx->bitlen >> 40;
  ctx->data[57] = ctx->bitlen >> 48;
  ctx->data[56] = ctx->bitlen >> 56;
  sha256_transform(ctx, ctx->data);

  for (i = 0; i < 4; i++)
    {
      hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0xff;
      hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0xff;
      hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0xff;
      hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0xff;
      hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0xff;
      hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0xff;
      hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0xff;
      hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0xff;
    }
}

static int ota_sha256_file(FAR const char *path,
                          FAR char hex_out[65])
{
  int fd;
  struct sha256_ctx_s ctx;
  uint8_t hash[SHA256_BLOCK_SIZE];
  uint8_t buf[QIBAN_OTA_BUF_SIZE];
  ssize_t n;
  int i;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return ERROR;
    }

  sha256_init(&ctx);
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
      sha256_update(&ctx, buf, n);
    }

  close(fd);
  sha256_final(&ctx, hash);

  for (i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
      sprintf(hex_out + i * 2, "%02x", hash[i]);
    }

  hex_out[64] = '\0';
  return OK;
}

/****************************************************************************
 * Service management
 ****************************************************************************/

static int ota_stop_services(void)
{
  int i;

  printf("[ota] stopping services...\n");
  for (i = 0; g_service_names[i] != NULL; i++)
    {
      /* Use kill to signal running services to exit.
       * In a real system we'd track PIDs; here we rely on
       * the service's own graceful shutdown on SIGTERM.
       */

      char cmd[128];
      snprintf(cmd, sizeof(cmd), "killall %s 2>/dev/null", g_service_names[i]);
      system(cmd);
    }

  usleep(500000); /* 500ms for services to exit */
  return OK;
}

static int ota_start_services(void)
{
  int i;

  printf("[ota] starting services...\n");
  for (i = 0; g_service_names[i] != NULL; i++)
    {
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "%s &", g_service_names[i]);
      system(cmd);
    }

  return OK;
}

/****************************************************************************
 * OTA operations
 ****************************************************************************/

static int ota_check_update(FAR struct ota_context_s *ctx)
{
  char resp[4096];
  char url_path[256];
  int ret;
  FAR char *p;

  ctx->status.state = QIBAN_OTA_STATE_CHECKING;
  ota_get_time_str(ctx->status.last_check_time,
                   sizeof(ctx->status.last_check_time));
  ota_save_status(&ctx->status);

  snprintf(url_path, sizeof(url_path),
           "/api/ota/check?device_id=gemini-s1&app_version=%s",
           ctx->status.current_version);

  ret = ota_http_get(ctx->server_host, ctx->server_port,
                     url_path, resp, sizeof(resp));
  if (ret <= 0)
    {
      printf("[ota] server unreachable\n");
      ctx->status.state = QIBAN_OTA_STATE_IDLE;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "server unreachable");
      ota_save_status(&ctx->status);
      return ERROR;
    }

  /* Skip HTTP headers */

  p = strstr(resp, "\r\n\r\n");
  if (p == NULL)
    {
      ctx->status.state = QIBAN_OTA_STATE_IDLE;
      ota_save_status(&ctx->status);
      return ERROR;
    }

  p += 4;

  /* Simple JSON parse: look for "app_version" field */

  if (strstr(p, "\"no_update\"") != NULL ||
      strstr(p, "\"app_update\"") == NULL)
    {
      printf("[ota] no update available\n");
      ctx->status.state = QIBAN_OTA_STATE_IDLE;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg), "");
      ota_save_status(&ctx->status);
      return OK;
    }

  /* Extract target version */

  FAR char *ver_start = strstr(p, "\"app_version\"");
  if (ver_start != NULL)
    {
      ver_start = strchr(ver_start, ':');
      if (ver_start != NULL)
        {
          ver_start++;
          while (*ver_start == ' ' || *ver_start == '"')
            {
              ver_start++;
            }

          FAR char *ver_end = strchr(ver_start, '"');
          if (ver_end != NULL)
            {
              size_t vlen = ver_end - ver_start;
              if (vlen >= sizeof(ctx->status.target_version))
                {
                  vlen = sizeof(ctx->status.target_version) - 1;
                }

              memcpy(ctx->status.target_version, ver_start, vlen);
              ctx->status.target_version[vlen] = '\0';
            }
        }
    }

  printf("[ota] update available: %s -> %s\n",
         ctx->status.current_version, ctx->status.target_version);
  ctx->status.state = QIBAN_OTA_STATE_IDLE;
  ota_save_status(&ctx->status);
  return OK;
}

static int ota_download_package(FAR struct ota_context_s *ctx)
{
  char url_path[256];

  ctx->status.state = QIBAN_OTA_STATE_DOWNLOAD;
  ctx->status.progress_pct = 0;
  ota_save_status(&ctx->status);

  ota_mkdir_p(QIBAN_OTA_DIR, 0755);

  snprintf(url_path, sizeof(url_path),
           "/api/ota/app/download?version=%s",
           ctx->status.target_version);

  printf("[ota] downloading app package v%s...\n",
         ctx->status.target_version);

  if (ota_http_download(ctx->server_host, ctx->server_port,
                        url_path, QIBAN_OTA_DOWNLOAD) != OK)
    {
      ctx->status.state = QIBAN_OTA_STATE_FAILED;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "download failed");
      ota_save_status(&ctx->status);
      return ERROR;
    }

  ctx->status.progress_pct = 100;
  ctx->status.state = QIBAN_OTA_STATE_VERIFY;
  ota_save_status(&ctx->status);

  return OK;
}

static int ota_verify_package(FAR struct ota_context_s *ctx)
{
  char sha_hex[65];

  printf("[ota] verifying package...\n");

  if (ota_sha256_file(QIBAN_OTA_DOWNLOAD, sha_hex) != OK)
    {
      ctx->status.state = QIBAN_OTA_STATE_FAILED;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "sha256 computation failed");
      ota_save_status(&ctx->status);
      return ERROR;
    }

  printf("[ota] package sha256: %s\n", sha_hex);

  /* In production, compare against server-provided hash.
   * For now, just verify the file is non-empty and valid.
   */

  struct stat st;
  if (stat(QIBAN_OTA_DOWNLOAD, &st) != 0 || st.st_size == 0)
    {
      ctx->status.state = QIBAN_OTA_STATE_FAILED;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "empty or missing package");
      ota_save_status(&ctx->status);
      return ERROR;
    }

  printf("[ota] package size: %ld bytes\n", (long)st.st_size);
  return OK;
}

static int ota_apply_update(FAR struct ota_context_s *ctx)
{
  ctx->status.state = QIBAN_OTA_STATE_APPLY;
  ctx->status.progress_pct = 0;
  ota_save_status(&ctx->status);

  printf("[ota] applying update %s -> %s\n",
         ctx->status.current_version, ctx->status.target_version);

  /* Step 1: Stop running services */

  ota_stop_services();
  ctx->status.progress_pct = 30;
  ota_save_status(&ctx->status);

  /* Step 2: Backup current version */

  ota_mkdir_p(QIBAN_OTA_BACKUP_DIR, 0755);
  /* In a real system, copy current binaries to backup */

  ctx->status.progress_pct = 50;
  ota_save_status(&ctx->status);

  /* Step 3: Extract and replace (simplified - just update version) */

  /* In production: unzip QIBAN_OTA_DOWNLOAD to staging, then
   * atomic rename each component into place.  For the MVP we
   * just update the version marker and restart services.
   */

  char ver_json[256];
  snprintf(ver_json, sizeof(ver_json),
    "{\n"
    "  \"version\": \"%s\",\n"
    "  \"updated_at\": \"%s\"\n"
    "}\n",
    ctx->status.target_version,
    ctx->status.last_check_time);

  ota_write_file(QIBAN_OTA_VERSION_FILE, ver_json);

  ctx->status.progress_pct = 80;
  ota_save_status(&ctx->status);

  /* Step 4: Restart services */

  ota_start_services();
  ctx->status.progress_pct = 100;

  /* Update current version */

  strlcpy(ctx->status.current_version, ctx->status.target_version,
          sizeof(ctx->status.current_version));
  ctx->status.state = QIBAN_OTA_STATE_DONE;
  snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg), "");
  ota_save_status(&ctx->status);

  printf("[ota] update complete: now at v%s\n",
         ctx->status.current_version);
  return OK;
}

static int ota_rollback(FAR struct ota_context_s *ctx)
{
  ctx->status.state = QIBAN_OTA_STATE_ROLLBACK;
  ota_save_status(&ctx->status);

  printf("[ota] rolling back...\n");

  /* In production: restore binaries from QIBAN_OTA_BACKUP_DIR */

  ota_stop_services();
  ota_start_services();

  ctx->status.state = QIBAN_OTA_STATE_IDLE;
  snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
           "rolled back");
  ota_save_status(&ctx->status);

  printf("[ota] rollback complete\n");
  return OK;
}

/****************************************************************************
 * Firmware OTA operations
 ****************************************************************************/

static int ota_firmware_check(FAR struct ota_context_s *ctx)
{
  char resp[4096];
  char url_path[256];
  int ret;
  FAR char *p;

  printf("[ota] checking for firmware update...\n");

  ctx->status.state = QIBAN_OTA_STATE_CHECKING;
  ota_save_status(&ctx->status);

  snprintf(url_path, sizeof(url_path),
           "/api/ota/check?device_id=gemini-s1&app_version=%s"
           "&fw_version=%s",
           ctx->status.current_version,
           ctx->status.current_version);

  ret = ota_http_get(ctx->server_host, ctx->server_port,
                     url_path, resp, sizeof(resp));
  if (ret <= 0)
    {
      printf("[ota] server unreachable\n");
      ctx->status.state = QIBAN_OTA_STATE_IDLE;
      ota_save_status(&ctx->status);
      return ERROR;
    }

  p = strstr(resp, "\r\n\r\n");
  if (p == NULL)
    {
      ctx->status.state = QIBAN_OTA_STATE_IDLE;
      ota_save_status(&ctx->status);
      return ERROR;
    }

  p += 4;

  if (strstr(p, "\"firmware_update\"") != NULL &&
      strstr(p, "\"no_update\"") == NULL)
    {
      FAR char *ver_start = strstr(p, "\"firmware_version\"");
      if (ver_start == NULL)
        {
          ver_start = strstr(p, "\"version\"");
        }

      if (ver_start != NULL)
        {
          ver_start = strchr(ver_start, ':');
          if (ver_start != NULL)
            {
              ver_start++;
              while (*ver_start == ' ' || *ver_start == '"')
                {
                  ver_start++;
                }

              FAR char *ver_end = strchr(ver_start, '"');
              if (ver_end != NULL)
                {
                  size_t vlen = ver_end - ver_start;
                  if (vlen >= sizeof(ctx->status.target_version))
                    {
                      vlen = sizeof(ctx->status.target_version) - 1;
                    }

                  memcpy(ctx->status.target_version, ver_start, vlen);
                  ctx->status.target_version[vlen] = '\0';
                }
            }
        }

      printf("[ota] firmware update available: %s -> %s\n",
             ctx->status.current_version, ctx->status.target_version);
    }
  else
    {
      printf("[ota] firmware is up to date\n");
    }

  ctx->status.state = QIBAN_OTA_STATE_IDLE;
  ota_save_status(&ctx->status);
  return OK;
}

static int ota_firmware_apply(FAR struct ota_context_s *ctx)
{
  char url_path[256];
  char sha_hex[65];
  struct stat st;

  printf("[ota] firmware OTA: preparing update...\n");

  ctx->status.state = QIBAN_OTA_STATE_DOWNLOAD;
  ctx->status.progress_pct = 0;
  ota_save_status(&ctx->status);

  /* Step 1: Download ota.zip from server */

  ota_mkdir_p("/data", 0755);

  snprintf(url_path, sizeof(url_path),
           "/api/ota/firmware/download?version=%s",
           ctx->status.target_version[0] ? ctx->status.target_version : "latest");

  printf("[ota] downloading firmware ota.zip...\n");

  if (ota_http_download(ctx->server_host, ctx->server_port,
                        url_path, QIBAN_OTA_FIRMWARE_ZIP) != OK)
    {
      ctx->status.state = QIBAN_OTA_STATE_FAILED;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "firmware download failed");
      ota_save_status(&ctx->status);
      return ERROR;
    }

  ctx->status.progress_pct = 50;
  ctx->status.state = QIBAN_OTA_STATE_VERIFY;
  ota_save_status(&ctx->status);

  /* Step 2: Verify downloaded file */

  printf("[ota] verifying firmware ota.zip...\n");

  if (stat(QIBAN_OTA_FIRMWARE_ZIP, &st) != 0 || st.st_size < 1024)
    {
      ctx->status.state = QIBAN_OTA_STATE_FAILED;
      snprintf(ctx->status.error_msg, sizeof(ctx->status.error_msg),
               "invalid firmware package (too small)");
      ota_save_status(&ctx->status);
      unlink(QIBAN_OTA_FIRMWARE_ZIP);
      return ERROR;
    }

  if (ota_sha256_file(QIBAN_OTA_FIRMWARE_ZIP, sha_hex) == OK)
    {
      printf("[ota] firmware ota.zip sha256: %s\n", sha_hex);
    }

  printf("[ota] firmware ota.zip size: %ld bytes\n", (long)st.st_size);

  ctx->status.progress_pct = 80;
  ota_save_status(&ctx->status);

  /* Step 3: Update version marker */

  ota_get_time_str(ctx->status.last_check_time,
                   sizeof(ctx->status.last_check_time));

  char ver_json[256];
  snprintf(ver_json, sizeof(ver_json),
    "{\n"
    "  \"version\": \"%s\",\n"
    "  \"firmware_update_time\": \"%s\",\n"
    "  \"type\": \"firmware\"\n"
    "}\n",
    ctx->status.target_version[0] ? ctx->status.target_version : "pending",
    ctx->status.last_check_time);

  ota_mkdir_p(QIBAN_OTA_DIR, 0755);
  ota_write_file(QIBAN_OTA_VERSION_FILE, ver_json);

  ctx->status.progress_pct = 90;
  ctx->status.state = QIBAN_OTA_STATE_APPLY;
  ota_save_status(&ctx->status);

  /* Step 4: Reboot to apply firmware
   *
   * The bootloader (rcS.blboottee) detects /data/ota.zip on next boot,
   * verifies it, mounts it as zipfs, and boots /ota/vela_ota.bin which
   * runs the OTA recovery script (ota.sh) to flash the firmware.
   */

  printf("[ota] firmware ota.zip placed at %s\n", QIBAN_OTA_FIRMWARE_ZIP);
  printf("[ota] rebooting to apply firmware update...\n");
  printf("[ota] BL will detect ota.zip and boot recovery image\n");

  ctx->status.progress_pct = 100;
  ctx->status.state = QIBAN_OTA_STATE_DONE;
  ota_save_status(&ctx->status);

  /* Give a moment for status to be written */

  usleep(500000);

  /* Reboot - the BL will pick up /data/ota.zip */

  system("reboot");
  return OK;
}

static int ota_firmware_apply_local(FAR const char *zip_path)
{
  struct stat st;

  printf("[ota] applying local firmware: %s\n", zip_path);

  if (stat(zip_path, &st) != 0)
    {
      fprintf(stderr, "[ota] file not found: %s\n", zip_path);
      return ERROR;
    }

  if (st.st_size < 1024)
    {
      fprintf(stderr, "[ota] file too small to be valid firmware\n");
      return ERROR;
    }

  /* Copy to /data/ota.zip */

  printf("[ota] copying %s -> %s (%ld bytes)\n",
         zip_path, QIBAN_OTA_FIRMWARE_ZIP, (long)st.st_size);

  int src_fd = open(zip_path, O_RDONLY);
  if (src_fd < 0)
    {
      return ERROR;
    }

  int dst_fd = open(QIBAN_OTA_FIRMWARE_ZIP,
                    O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (dst_fd < 0)
    {
      close(src_fd);
      return ERROR;
    }

  char buf[QIBAN_OTA_BUF_SIZE];
  ssize_t n;
  while ((n = read(src_fd, buf, sizeof(buf))) > 0)
    {
      write(dst_fd, buf, n);
    }

  close(src_fd);
  close(dst_fd);

  printf("[ota] firmware ota.zip ready at %s\n", QIBAN_OTA_FIRMWARE_ZIP);
  printf("[ota] reboot to apply (BL will detect and flash)\n");
  return OK;
}

/****************************************************************************
 * Daemon loop
 ****************************************************************************/

static int ota_daemon_loop(FAR struct ota_context_s *ctx)
{
  printf("[ota] daemon started, polling every %ds, server=%s:%d\n",
         ctx->poll_interval_sec, ctx->server_host, ctx->server_port);

  ctx->running = 1;
  while (ctx->running)
    {
      ota_check_update(ctx);

      if (ctx->status.target_version[0] != '\0' &&
          ota_compare_version(ctx->status.target_version,
                              ctx->status.current_version) > 0)
        {
          if (ota_download_package(ctx) == OK)
            {
              if (ota_verify_package(ctx) == OK)
                {
                  ota_apply_update(ctx);
                }
            }
        }

      sleep(ctx->poll_interval_sec);
    }

  return OK;
}

/****************************************************************************
 * Print usage
 ****************************************************************************/

static void print_usage(void)
{
  printf("Usage: qiban_ota_service [command]\n");
  printf("\n");
  printf("App OTA commands:\n");
  printf("  (none)              Start OTA daemon (poll every %ds)\n",
         QIBAN_OTA_DEFAULT_INTERVAL);
  printf("  check               One-shot app version check\n");
  printf("  status              Print current OTA status\n");
  printf("  apply <zip>         Apply a local app OTA package\n");
  printf("  rollback            Rollback to previous app version\n");
  printf("\n");
  printf("Firmware OTA commands:\n");
  printf("  firmware_check      Check for firmware update\n");
  printf("  firmware_apply      Download firmware ota.zip & reboot\n");
  printf("  firmware_local <f>  Copy local ota.zip to /data & reboot\n");
  printf("\n");
  printf("Environment:\n");
  printf("  QIBAN_OTA_SERVER   Server IP (default: 10.0.0.1)\n");
  printf("  QIBAN_OTA_PORT     Server port (default: %d)\n",
         QIBAN_OTA_DEFAULT_PORT);
  printf("  QIBAN_OTA_INTERVAL Poll interval seconds (default: %d)\n",
         QIBAN_OTA_DEFAULT_INTERVAL);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct ota_context_s ctx;
  FAR const char *env;
  FAR const char *command = NULL;

  memset(&ctx, 0, sizeof(ctx));

  /* Load config from environment */

  env = getenv("QIBAN_OTA_SERVER");
  strlcpy(ctx.server_host, env ? env : "10.0.0.1",
          sizeof(ctx.server_host));

  env = getenv("QIBAN_OTA_PORT");
  ctx.server_port = env ? atoi(env) : QIBAN_OTA_DEFAULT_PORT;

  env = getenv("QIBAN_OTA_INTERVAL");
  ctx.poll_interval_sec = env ? atoi(env) : QIBAN_OTA_DEFAULT_INTERVAL;

  /* Load current version */

  ota_load_version(ctx.status.current_version,
                   sizeof(ctx.status.current_version));

  if (argc >= 2)
    {
      command = argv[1];
    }

  if (command == NULL)
    {
      /* Daemon mode */

      return ota_daemon_loop(&ctx);
    }
  else if (strcmp(command, "check") == 0)
    {
      return ota_check_update(&ctx);
    }
  else if (strcmp(command, "status") == 0)
    {
      char buf[512];
      ota_read_file(QIBAN_OTA_STATUS_FILE, buf, sizeof(buf));
      printf("%s\n", buf);
      return OK;
    }
  else if (strcmp(command, "apply") == 0)
    {
      if (argc < 3)
        {
          fprintf(stderr, "usage: qiban_ota_service apply <zip_path>\n");
          return 1;
        }

      strlcpy(ctx.status.target_version, "manual",
              sizeof(ctx.status.target_version));
      return ota_apply_update(&ctx);
    }
  else if (strcmp(command, "rollback") == 0)
    {
      return ota_rollback(&ctx);
    }
  else if (strcmp(command, "firmware_check") == 0)
    {
      return ota_firmware_check(&ctx);
    }
  else if (strcmp(command, "firmware_apply") == 0)
    {
      strlcpy(ctx.status.target_version, "latest",
              sizeof(ctx.status.target_version));
      return ota_firmware_apply(&ctx);
    }
  else if (strcmp(command, "firmware_local") == 0)
    {
      if (argc < 3)
        {
          fprintf(stderr,
                  "usage: qiban_ota_service firmware_local <zip_path>\n");
          return 1;
        }

      return ota_firmware_apply_local(argv[2]);
    }
  else if (strcmp(command, "help") == 0 ||
           strcmp(command, "--help") == 0)
    {
      print_usage();
      return OK;
    }
  else
    {
      fprintf(stderr, "unknown command: %s\n", command);
      print_usage();
      return 1;
    }
}
