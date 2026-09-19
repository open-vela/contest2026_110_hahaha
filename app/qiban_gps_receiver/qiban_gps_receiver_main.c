/****************************************************************************
 * qiban_gps_receiver_main.c
 *
 * Lightweight HTTP server that receives GPS coordinates from a phone browser
 * and writes them to /data/qiban_location_state.json.
 *
 * Usage:
 *   qiban_gps_receiver              # start on default port 8080
 *   qiban_gps_receiver 9090         # start on custom port
 *
 * Phone browser opens tools/phone_gps.html which calls:
 *   POST http://<board_ip>:8080/gps  { "lon": 116.481, "lat": 39.990 }
 ****************************************************************************/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef FAR
#  define FAR
#endif

#ifndef OK
#  define OK 0
#endif

#define QIBAN_GPS_DEFAULT_PORT       8080
#define QIBAN_GPS_LOCATION_PATH      "/data/qiban_location_state.json"
#define QIBAN_GPS_BUF_SIZE           1024
#define QIBAN_GPS_RESPONSE_OK        \
  "HTTP/1.1 200 OK\r\n"             \
  "Content-Type: application/json\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n"           \
  "\r\n"                            \
  "{\"status\":\"ok\"}\n"

#define QIBAN_GPS_RESPONSE_PREFLIGHT \
  "HTTP/1.1 200 OK\r\n"             \
  "Access-Control-Allow-Origin: *\r\n" \
  "Access-Control-Allow-Methods: POST, OPTIONS\r\n" \
  "Access-Control-Allow-Headers: Content-Type\r\n" \
  "Connection: close\r\n"           \
  "\r\n"

#define QIBAN_GPS_RESPONSE_ERR       \
  "HTTP/1.1 400 Bad Request\r\n"    \
  "Content-Type: application/json\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Connection: close\r\n"           \
  "\r\n"                            \
  "{\"status\":\"error\"}\n"

static int qiban_gps_write_location(double longitude, double latitude)
{
  FAR FILE *fp;
  char temp_path[256];
  int ret;

  if (longitude < -180.0 || longitude > 180.0 ||
      latitude < -90.0 || latitude > 90.0)
    {
      fprintf(stderr, "gps: coordinates out of range: %.6f, %.6f\n",
              longitude, latitude);
      return -ERANGE;
    }

  snprintf(temp_path, sizeof(temp_path), "%s.tmp", QIBAN_GPS_LOCATION_PATH);

  fp = fopen(temp_path, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  ret = fprintf(fp,
                "{\n"
                "  \"schema_version\": 1,\n"
                "  \"source\": \"phone:gps\",\n"
                "  \"generated_at_epoch_s\": %ld,\n"
                "  \"longitude\": %.6f,\n"
                "  \"latitude\": %.6f,\n"
                "  \"accuracy_m\": 10,\n"
                "  \"valid\": 1\n"
                "}\n",
                (long)time(NULL),
                longitude,
                latitude);

  if (ret < 0 || ferror(fp) != 0)
    {
      fclose(fp);
      unlink(temp_path);
      return -EIO;
    }

  fclose(fp);

  if (rename(temp_path, QIBAN_GPS_LOCATION_PATH) != 0)
    {
      unlink(temp_path);
      return -errno;
    }

  printf("gps: %.6f, %.6f\n", longitude, latitude);
  return OK;
}

static bool qiban_gps_parse_double(FAR const char *json,
                                    FAR const char *key,
                                    FAR double *out)
{
  FAR const char *pos;
  FAR char *endptr;
  char pattern[32];
  double val;

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
  val = strtod(pos, &endptr);
  if (pos == endptr || errno != 0)
    {
      return false;
    }

  *out = val;
  return true;
}

static int qiban_gps_handle_post(FAR const char *body)
{
  double longitude = 0.0;
  double latitude = 0.0;

  if (!qiban_gps_parse_double(body, "lon", &longitude) &&
      !qiban_gps_parse_double(body, "longitude", &longitude))
    {
      fprintf(stderr, "gps: missing lon/longitude in request\n");
      return -EINVAL;
    }

  if (!qiban_gps_parse_double(body, "lat", &latitude) &&
      !qiban_gps_parse_double(body, "latitude", &latitude))
    {
      fprintf(stderr, "gps: missing lat/latitude in request\n");
      return -EINVAL;
    }

  return qiban_gps_write_location(longitude, latitude);
}

static FAR char *qiban_gps_find_body(FAR const char *request)
{
  FAR const char *body;

  /* HTTP body starts after \r\n\r\n */

  body = strstr(request, "\r\n\r\n");
  if (body != NULL)
    {
      return (FAR char *)(body + 4);
  }

  /* Also try \n\n (some clients) */

  body = strstr(request, "\n\n");
  if (body != NULL)
    {
      return (FAR char *)(body + 2);
    }

  return NULL;
}

static bool qiban_gps_is_post(FAR const char *request)
{
  return strncmp(request, "POST ", 5) == 0;
}

static bool qiban_gps_is_options(FAR const char *request)
{
  return strncmp(request, "OPTIONS ", 8) == 0;
}

static void qiban_gps_handle_client(int client_fd)
{
  char buf[QIBAN_GPS_BUF_SIZE];
  ssize_t nread;
  FAR char *body;

  nread = recv(client_fd, buf, sizeof(buf) - 1, 0);
  if (nread <= 0)
    {
      close(client_fd);
      return;
    }

  buf[nread] = '\0';

  if (qiban_gps_is_options(buf))
    {
      /* CORS preflight */

      send(client_fd, QIBAN_GPS_RESPONSE_PREFLIGHT,
           strlen(QIBAN_GPS_RESPONSE_PREFLIGHT), 0);
    }
  else if (qiban_gps_is_post(buf))
    {
      body = qiban_gps_find_body(buf);
      if (body != NULL && qiban_gps_handle_post(body) == OK)
        {
          send(client_fd, QIBAN_GPS_RESPONSE_OK,
               strlen(QIBAN_GPS_RESPONSE_OK), 0);
        }
      else
        {
          send(client_fd, QIBAN_GPS_RESPONSE_ERR,
               strlen(QIBAN_GPS_RESPONSE_ERR), 0);
        }
    }
  else
    {
      send(client_fd, QIBAN_GPS_RESPONSE_ERR,
           strlen(QIBAN_GPS_RESPONSE_ERR), 0);
    }

  close(client_fd);
}

static int qiban_gps_run_server(int port)
{
  struct sockaddr_in addr;
  int server_fd;
  int client_fd;
  int opt = 1;
  int ret;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
    {
      fprintf(stderr, "gps: socket failed: %d\n", errno);
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
      fprintf(stderr, "gps: bind port %d failed: %d\n", port, errno);
      close(server_fd);
      return -errno;
    }

  ret = listen(server_fd, 4);
  if (ret < 0)
    {
      fprintf(stderr, "gps: listen failed: %d\n", errno);
      close(server_fd);
      return -errno;
    }

  printf("gps receiver: listening on port %d\n", port);
  printf("gps receiver: POST http://<board_ip>:%d/gps\n", port);
  printf("gps receiver: body: {\"lon\":116.481,\"lat\":39.990}\n");

  for (; ; )
    {
      client_fd = accept(server_fd, NULL, NULL);
      if (client_fd < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          fprintf(stderr, "gps: accept failed: %d\n", errno);
          continue;
        }

      qiban_gps_handle_client(client_fd);
    }

  close(server_fd);
  return OK;
}

static void qiban_gps_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s [port]     start GPS receiver (default port %d)\n",
         progname, QIBAN_GPS_DEFAULT_PORT);
}

int main(int argc, FAR char *argv[])
{
  int port = QIBAN_GPS_DEFAULT_PORT;

  if (argc > 1)
    {
      port = atoi(argv[1]);
      if (port <= 0 || port > 65535)
        {
          fprintf(stderr, "invalid port: %s\n", argv[1]);
          qiban_gps_usage(argv[0]);
          return EXIT_FAILURE;
        }
    }

  /* Ensure /data directory exists */

  mkdir("/data", 0777);

  return qiban_gps_run_server(port) < 0 ? EXIT_FAILURE : OK;
}
