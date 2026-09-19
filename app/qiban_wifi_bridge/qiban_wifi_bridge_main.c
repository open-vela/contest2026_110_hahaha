/****************************************************************************
 * qiban_wifi_bridge_main.c
 *
 * HTTP bridge for wireless voice data transfer between board and host.
 * Replaces USB ADB for ASR/TTS workflows.
 *
 * Endpoints:
 *   GET  /api/asr/request     - get current ASR request JSON
 *   GET  /api/tts/request     - get current TTS request text
 *   GET  /api/file?path=...   - download a file from board
 *   GET  /api/status          - bridge health check
 *   POST /api/asr/result      - submit recognized text
 *   POST /api/tts/upload      - upload TTS PCM file
 *   POST /api/command         - run voice_service command
 *
 * Usage:
 *   qiban_wifi_bridge              # start on default port 8081
 *   qiban_wifi_bridge 9090         # start on custom port
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_BRIDGE_DEFAULT_PORT    8081
#define QIBAN_BRIDGE_BUF_SIZE        4096
#define QIBAN_BRIDGE_FILE_BUF_SIZE   8192
#define QIBAN_BRIDGE_MAX_PATH        256
#define QIBAN_BRIDGE_ASR_REQUEST     "/data/qiban_voice/asr/last_request.json"
#define QIBAN_BRIDGE_TTS_TEXT        "/data/qiban_voice/tts/last_request.txt"
#define QIBAN_BRIDGE_TTS_STATUS      "/data/qiban_voice_last_tts.json"

#define QIBAN_BRIDGE_RESP_OK_JSON \
  "HTTP/1.1 200 OK\r\n" \
  "Content-Type: application/json\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n\r\n"

#define QIBAN_BRIDGE_RESP_OK_TEXT \
  "HTTP/1.1 200 OK\r\n" \
  "Content-Type: text/plain\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n\r\n"

#define QIBAN_BRIDGE_RESP_OK_OCTET \
  "HTTP/1.1 200 OK\r\n" \
  "Content-Type: application/octet-stream\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n\r\n"

#define QIBAN_BRIDGE_RESP_CORS \
  "HTTP/1.1 200 OK\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" \
  "Access-Control-Allow-Headers: Content-Type\r\n" \
  "Connection: close\r\n\r\n"

#define QIBAN_BRIDGE_RESP_ERR \
  "HTTP/1.1 400 Bad Request\r\n" \
  "Content-Type: application/json\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n\r\n" \
  "{\"error\":\"bad request\"}\n"

#define QIBAN_BRIDGE_RESP_NOTFOUND \
  "HTTP/1.1 404 Not Found\r\n" \
  "Content-Type: application/json\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n\r\n" \
  "{\"error\":\"not found\"}\n"

/****************************************************************************
 * File I/O helpers
 ****************************************************************************/

static int qiban_bridge_read_file(FAR const char *path, FAR char *buf,
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
  return (int)n;
}

static int qiban_bridge_write_file(FAR const char *path,
                                   FAR const char *data, size_t len)
{
  char tmp[QIBAN_BRIDGE_MAX_PATH + 8];
  int fd;
  ssize_t n;

  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      return -errno;
    }

  n = write(fd, data, len);
  close(fd);

  if (n < 0 || (size_t)n != len)
    {
      unlink(tmp);
      return -EIO;
    }

  if (rename(tmp, path) != 0)
    {
      unlink(tmp);
      return -errno;
    }

  return OK;
}

/****************************************************************************
 * HTTP request parsing
 ****************************************************************************/

static bool http_is_method(FAR const char *req, FAR const char *method)
{
  return strncmp(req, method, strlen(method)) == 0;
}

static FAR char *http_find_body(FAR const char *req)
{
  FAR const char *p;

  p = strstr(req, "\r\n\r\n");
  if (p != NULL)
    {
      return (FAR char *)(p + 4);
    }

  p = strstr(req, "\n\n");
  if (p != NULL)
    {
      return (FAR char *)(p + 2);
    }

  return NULL;
}

/* Extract query parameter value. e.g. path from "/api/file?path=/data/x" */

static bool http_query_param(FAR const char *uri, FAR const char *key,
                              FAR char *val, size_t val_size)
{
  FAR const char *p;
  FAR const char *end;
  char pattern[64];
  size_t len;

  snprintf(pattern, sizeof(pattern), "%s=", key);
  p = strstr(uri, pattern);
  if (p == NULL)
    {
      return false;
    }

  p += strlen(pattern);
  end = strchr(p, ' ');
  if (end == NULL)
    {
      end = strchr(p, '&');
    }

  if (end == NULL)
    {
      end = p + strlen(p);
    }

  len = (size_t)(end - p);
  if (len >= val_size)
    {
      len = val_size - 1;
    }

  memcpy(val, p, len);
  val[len] = '\0';
  return true;
}

static bool json_get_string(FAR const char *json, FAR const char *key,
                             FAR char *buf, size_t buf_size)
{
  FAR const char *p;
  FAR const char *start;
  FAR const char *end;
  char pattern[64];
  size_t len;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    {
      return false;
    }

  p = strchr(p, ':');
  if (p == NULL)
    {
      return false;
    }

  start = strchr(p, '"');
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

/****************************************************************************
 * Endpoint handlers
 ****************************************************************************/

static void handle_status(int client_fd)
{
  char resp[512];
  int len;

  len = snprintf(resp, sizeof(resp),
                 "%s"
                 "{\"status\":\"ok\",\"service\":\"qiban_wifi_bridge\"}\n",
                 QIBAN_BRIDGE_RESP_OK_JSON);
  send(client_fd, resp, len, 0);
}

static void handle_get_asr_request(int client_fd)
{
  char buf[QIBAN_BRIDGE_BUF_SIZE];
  char resp[QIBAN_BRIDGE_BUF_SIZE + 128];
  int n;
  int len;

  n = qiban_bridge_read_file(QIBAN_BRIDGE_ASR_REQUEST, buf, sizeof(buf));
  if (n < 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
           strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
      return;
    }

  len = snprintf(resp, sizeof(resp), "%s%s",
                 QIBAN_BRIDGE_RESP_OK_JSON, buf);
  send(client_fd, resp, len, 0);
}

static void handle_get_tts_request(int client_fd)
{
  char buf[QIBAN_BRIDGE_BUF_SIZE];
  char resp[QIBAN_BRIDGE_BUF_SIZE + 128];
  int n;
  int len;

  n = qiban_bridge_read_file(QIBAN_BRIDGE_TTS_TEXT, buf, sizeof(buf));
  if (n < 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
           strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
      return;
    }

  len = snprintf(resp, sizeof(resp), "%s%s",
                 QIBAN_BRIDGE_RESP_OK_TEXT, buf);
  send(client_fd, resp, len, 0);
}

static void handle_get_file(int client_fd, FAR const char *uri)
{
  char path[QIBAN_BRIDGE_MAX_PATH];
  char header[256];
  char buf[QIBAN_BRIDGE_FILE_BUF_SIZE];
  int fd;
  ssize_t n;
  struct stat st;
  int hlen;

  if (!http_query_param(uri, "path", path, sizeof(path)))
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  if (stat(path, &st) < 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
           strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
      return;
    }

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
           strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
      return;
    }

  hlen = snprintf(header, sizeof(header),
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/octet-stream\r\n"
                  "Content-Length: %ld\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n",
                  (long)st.st_size);
  send(client_fd, header, hlen, 0);

  while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
      send(client_fd, buf, n, 0);
    }

  close(fd);
}

static void handle_post_asr_result(int client_fd, FAR const char *body)
{
  char text[512];
  pid_t pid;
  FAR char *argv[4];
  int ret;

  if (!json_get_string(body, "text", text, sizeof(text)))
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  printf("wifi bridge: ASR result = %s\n", text);

  argv[0] = "qiban_voice_service";
  argv[1] = "import-asr";
  argv[2] = text;
  argv[3] = NULL;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  send(client_fd,
       QIBAN_BRIDGE_RESP_OK_JSON "{\"status\":\"asr_imported\"}\n",
       strlen(QIBAN_BRIDGE_RESP_OK_JSON "{\"status\":\"asr_imported\"}\n"),
       0);
}

static void handle_post_tts_upload(int client_fd, FAR const char *body,
                                   size_t body_len)
{
  FAR const char *pcm_data;
  size_t pcm_len;
  char resp[256];
  int rlen;
  int ret;

  /* Body is raw PCM data */

  if (body == NULL || body_len == 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  pcm_data = body;
  pcm_len = body_len;

  /* Write to TTS placeholder path */

  mkdir("/data/qiban_voice/tts", 0777);
  ret = qiban_bridge_write_file(
      "/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm",
      pcm_data, pcm_len);

  if (ret < 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  printf("wifi bridge: TTS PCM uploaded (%zu bytes)\n", pcm_len);

  rlen = snprintf(resp, sizeof(resp),
                  "%s{\"status\":\"tts_uploaded\",\"size\":%zu}\n",
                  QIBAN_BRIDGE_RESP_OK_JSON, pcm_len);
  send(client_fd, resp, rlen, 0);
}

static bool qiban_is_allowed_command(FAR const char *cmd)
{
  /* Allowlist of safe commands */

  static FAR const char *allowed[] =
  {
    "qiban_vehicle_service",
    "qiban_nav_service",
    "qiban_sensor_bridge",
    "qiban_map_service",
    "qiban_voice_service",
    "qiban_gps_receiver",
    "qiban_music_service",
    "qiban_ota_service",
    "ls",
    "cat",
    "ps",
    "df",
    "free",
    NULL
  };

  int i;
  for (i = 0; allowed[i] != NULL; i++)
    {
      if (strcmp(cmd, allowed[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

static void handle_post_command(int client_fd, FAR const char *body)
{
  char cmd[256];
  pid_t pid;
  FAR char *argv[8];
  char *p;
  int argc = 0;
  int ret;

  if (!json_get_string(body, "cmd", cmd, sizeof(cmd)))
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  printf("wifi bridge: command = %s\n", cmd);

  /* Parse command string into argv */

  p = strtok(cmd, " ");
  while (p != NULL && argc < 7)
    {
      argv[argc++] = p;
      p = strtok(NULL, " ");
    }

  argv[argc] = NULL;

  if (argc == 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  /* Validate command against allowlist */

  if (!qiban_is_allowed_command(argv[0]))
    {
      fprintf(stderr, "wifi bridge: command not allowed: %s\n", argv[0]);
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      send(client_fd, QIBAN_BRIDGE_RESP_ERR,
           strlen(QIBAN_BRIDGE_RESP_ERR), 0);
      return;
    }

  /* Reap zombie child */

  waitpid(pid, NULL, WNOHANG);

  send(client_fd,
       QIBAN_BRIDGE_RESP_OK_JSON "{\"status\":\"command_sent\"}\n",
       strlen(QIBAN_BRIDGE_RESP_OK_JSON "{\"status\":\"command_sent\"}\n"),
       0);
}

/****************************************************************************
 * Client handler
 ****************************************************************************/

static void handle_client(int client_fd)
{
  char buf[QIBAN_BRIDGE_BUF_SIZE];
  ssize_t nread;
  FAR char *body;

  nread = recv(client_fd, buf, sizeof(buf) - 1, 0);
  if (nread <= 0)
    {
      close(client_fd);
      return;
    }

  buf[nread] = '\0';

  /* CORS preflight */

  if (http_is_method(buf, "OPTIONS"))
    {
      send(client_fd, QIBAN_BRIDGE_RESP_CORS,
           strlen(QIBAN_BRIDGE_RESP_CORS), 0);
      close(client_fd);
      return;
    }

  /* GET endpoints */

  if (http_is_method(buf, "GET"))
    {
      if (strstr(buf, "GET /api/status") != NULL)
        {
          handle_status(client_fd);
        }
      else if (strstr(buf, "GET /api/asr/request") != NULL)
        {
          handle_get_asr_request(client_fd);
        }
      else if (strstr(buf, "GET /api/tts/request") != NULL)
        {
          handle_get_tts_request(client_fd);
        }
      else if (strstr(buf, "GET /api/file") != NULL)
        {
          handle_get_file(client_fd, buf + 4);
        }
      else
        {
          send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
               strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
        }

      close(client_fd);
      return;
    }

  /* POST endpoints */

  if (http_is_method(buf, "POST"))
    {
      body = http_find_body(buf);

      if (strstr(buf, "POST /api/asr/result") != NULL)
        {
          handle_post_asr_result(client_fd, body);
        }
      else if (strstr(buf, "POST /api/tts/upload") != NULL)
        {
          /* For TTS upload, body is raw PCM after headers */

          handle_post_tts_upload(client_fd, body,
                                 body ? (size_t)(buf + nread - body) : 0);
        }
      else if (strstr(buf, "POST /api/command") != NULL)
        {
          handle_post_command(client_fd, body);
        }
      else
        {
          send(client_fd, QIBAN_BRIDGE_RESP_NOTFOUND,
               strlen(QIBAN_BRIDGE_RESP_NOTFOUND), 0);
        }

      close(client_fd);
      return;
    }

  send(client_fd, QIBAN_BRIDGE_RESP_ERR,
       strlen(QIBAN_BRIDGE_RESP_ERR), 0);
  close(client_fd);
}

/****************************************************************************
 * Main server loop
 ****************************************************************************/

static int run_server(int port)
{
  struct sockaddr_in addr;
  int server_fd;
  int client_fd;
  int opt = 1;
  int ret;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
    {
      fprintf(stderr, "wifi bridge: socket failed: %d\n", errno);
      return -errno;
    }

  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  ret = bind(server_fd, (FAR struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0)
    {
      fprintf(stderr, "wifi bridge: bind port %d failed: %d\n", port, errno);
      close(server_fd);
      return -errno;
    }

  ret = listen(server_fd, 4);
  if (ret < 0)
    {
      fprintf(stderr, "wifi bridge: listen failed: %d\n", errno);
      close(server_fd);
      return -errno;
    }

  printf("wifi bridge: listening on port %d\n", port);
  printf("wifi bridge: host script -> qiban_wifi_voice_loop.sh\n");

  for (; ; )
    {
      client_fd = accept(server_fd, NULL, NULL);
      if (client_fd < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          fprintf(stderr, "wifi bridge: accept failed: %d\n", errno);
          continue;
        }

      handle_client(client_fd);
    }

  close(server_fd);
  return OK;
}

int main(int argc, FAR char *argv[])
{
  int port = QIBAN_BRIDGE_DEFAULT_PORT;

  if (argc > 1)
    {
      port = atoi(argv[1]);
      if (port <= 0 || port > 65535)
        {
          fprintf(stderr, "invalid port: %s\n", argv[1]);
          return EXIT_FAILURE;
        }
    }

  mkdir("/data", 0777);
  mkdir("/data/qiban_voice", 0777);
  mkdir("/data/qiban_voice/asr", 0777);
  mkdir("/data/qiban_voice/tts", 0777);
  mkdir("/data/qiban_voice/recordings", 0777);

  return run_server(port) < 0 ? EXIT_FAILURE : OK;
}
