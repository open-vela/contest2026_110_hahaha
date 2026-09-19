/****************************************************************************
 * qiban_ui_main.c
 *
 * Native OpenVela LVGL dashboard for the "Qiban AI" contest project.
 ****************************************************************************/

#include <nuttx/config.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <string.h>
#include <sys/boardctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/misc/cache/lv_image_cache.h>

#include "lv_offline_map.h"
#include "lv_offline_nav.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QIBAN_STATE_PATH           "/data/qiban_vehicle_state.json"
#define QIBAN_MAP_STATE_PATH       "/data/qiban_map/state.json"
#define QIBAN_NAV_STATE_PATH       "/data/qiban_nav_state.json"
#define QIBAN_NAV_INPUT_DIR        "/data/qiban_inputs"
#define QIBAN_NAV_REMAINING_PATH   QIBAN_NAV_INPUT_DIR "/nav_remaining_m"
#define QIBAN_LOCATION_STATE_PATH  "/data/qiban_location_state.json"
#define QIBAN_WEATHER_STATE_PATH   "/data/qiban_weather.json"
#define QIBAN_MUSIC_STATE_PATH     "/data/qiban_music_state.json"
#define QIBAN_VIDEO_STATE_PATH     "/data/qiban_video_state.json"
#define QIBAN_VOICE_LAST_INTENT_PATH "/data/qiban_voice_last_intent.json"
#define QIBAN_VOICE_LAST_AUDIO_PATH  "/data/qiban_voice_last_audio.json"
#define QIBAN_INPUT_PATH           "/dev/input0"
#define QIBAN_REFRESH_INTERVAL_MS  1000
#define QIBAN_PINCH_TIMER_INTERVAL_MS 30
#define QIBAN_PINCH_ZOOM_STEP_PX   40

/* Must stay in sync with GT911IOC_GETPINCH / struct gt911_pinch_data_s in
 * vendor/allwinnertech/boards/r528/drivers/gt911_iic_touch.h. Duplicated
 * here (instead of including the board driver header) to avoid coupling
 * this app to one specific board's touch driver.
 * GT911IOC_GETPINCH = _TSIOC(0x0011) = _TSIOCBASE(0x0900) | 0x0011.
 */
#define QIBAN_TOUCH_PINCH_IOC      0x0911

struct qiban_touch_pinch_s
{
  uint8_t npoints;
  int16_t x0;
  int16_t y0;
  int16_t x1;
  int16_t y1;
};
#define QIBAN_JSON_BUFFER_SIZE     1024
#define QIBAN_MAP_MARKER_SIZE      14
#define QIBAN_MAP_MARKER_PAD       18
#define QIBAN_OFFLINE_MAP_DIR      "/sdcard/map/"
#define QIBAN_OFFLINE_MAP_TILE_FILE "tile.png"
#define QIBAN_OFFLINE_NAV_GRAPH_PATH "/sdcard/nav/road_graph.bin"
#define QIBAN_OFFLINE_MAP_MIN_ZOOM 8
#define QIBAN_OFFLINE_MAP_MAX_ZOOM 18
#define QIBAN_OFFLINE_MAP_DEFAULT_ZOOM 17
#define QIBAN_OFFLINE_MAP_DEFAULT_LONGITUDE 112.9388140
#define QIBAN_OFFLINE_MAP_DEFAULT_LATITUDE 28.2282090
#define QIBAN_OFFLINE_MAP_TRACK_POINTS LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS
#define QIBAN_OFFLINE_NAV_ROUTE_RENDER_MAX LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS
#define QIBAN_OFFLINE_NAV_TURN_EVENT_MAX LV_OFFLINE_NAV_DEFAULT_MAX_TURN_EVENTS
#define QIBAN_MAP_PICK_MARKER_SIZE  14
#define QIBAN_MAP_PICK_CONFIRM_WIDTH 92
#define QIBAN_MAP_PICK_CONFIRM_HEIGHT 30

#if LVGL_VERSION_MAJOR >= 9
#  define QIBAN_UI_ACTIVE_INDEV() lv_indev_active()
#else
#  define QIBAN_UI_ACTIVE_INDEV() lv_indev_get_act()
#endif

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

extern const lv_font_t qiban_font_cjk_16;

#define QIBAN_CJK_FONT (&qiban_font_cjk_16)

#ifdef CONFIG_LV_FONT_MONTSERRAT_48
#  define QIBAN_SPEED_FONT (&lv_font_montserrat_48)
#elif defined(CONFIG_LV_FONT_MONTSERRAT_30)
#  define QIBAN_SPEED_FONT (&lv_font_montserrat_30)
#else
#  define QIBAN_SPEED_FONT QIBAN_CJK_FONT
#endif

#define QIBAN_TITLE_FONT QIBAN_CJK_FONT
#define QIBAN_METRIC_FONT QIBAN_CJK_FONT

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qiban_vehicle_state_s
{
  char source[48];
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

struct qiban_map_state_s
{
  char source[48];
  char title[32];
  char status[48];
  char mode[16];
  char image_path[96];
  double center_longitude;
  double center_latitude;
  int zoom;
  int has_route;
  double route_start_longitude;
  double route_start_latitude;
  double route_end_longitude;
  double route_end_latitude;
  int64_t updated_at;
  int ready;
};

struct qiban_nav_state_s
{
  char destination[48];
  char status[48];
  char next_turn[96];
  int total_distance_m;
  int remaining_distance_m;
  int eta_minutes;
  double start_longitude;
  double start_latitude;
  double end_longitude;
  double end_latitude;
  int active;
};

struct qiban_location_state_s
{
  char source[48];
  double longitude;
  double latitude;
  int accuracy_m;
  int valid;
  int64_t updated_at;
};

struct qiban_weather_state_s
{
  char condition[32];
  int temperature_c;
  int humidity;
  int wind_speed_kmh;
  char city[32];
  char source[48];
  int64_t updated_at;
  int valid;
};

struct qiban_music_state_s
{
  char title[64];
  char artist[64];
  char album[64];
  char source[16];     /* "sdcard", "wifi", "bluetooth" */
  char source_status[32];
  int playing;         /* 0=paused, 1=playing */
  int duration_sec;
  int position_sec;
  int volume;          /* 0-100 */
  int track_index;
  int track_count;
  int sdcard_available;
  int wifi_connected;
  int bluetooth_connected;
  int valid;
};

struct qiban_voice_state_s
{
  char status[64];
  char recognized_text[128];
  char intent_text[128];
  char action[32];
  char target[64];
  int valid;
};

struct qiban_video_state_s
{
  char state[16];        /* "stopped", "playing", "paused" */
  char current_name[64];
  int current_index;
  int position_sec;
  int duration_sec;
  int file_count;
  char error_msg[128];
  int valid;
  /* Browser entries */
  char cwd[128];
  struct
  {
    char name[64];
    char path[128];
    int is_dir;
    long size;
  } entries[16];
  int entry_count;
  int selected;
};

struct qiban_ui_s
{
  lv_obj_t *tileview;
  lv_obj_t *dashboard_tile;
  lv_obj_t *map_tile;
  lv_obj_t *voice_tile;
  lv_obj_t *weather_tile;
  lv_obj_t *music_tile;
  lv_obj_t *video_tile;
  lv_obj_t *prev_button;
  lv_obj_t *next_button;
  lv_obj_t *prev_button_label;
  lv_obj_t *next_button_label;
  lv_obj_t *header_label;
  lv_obj_t *page_label;
  lv_obj_t *source_label;
  lv_obj_t *speed_label;
  lv_obj_t *battery_label;
  lv_obj_t *range_label;
  lv_obj_t *ride_label;
  lv_obj_t *distance_label;
  lv_obj_t *nav_label;
  lv_obj_t *alert_panel;
  lv_obj_t *alert_label;
  lv_obj_t *map_frame;
  lv_obj_t *map_widget;
  lv_obj_t *map_route_line;
  lv_obj_t *map_page_status_label;
  lv_obj_t *map_pick_start_marker;
  lv_obj_t *map_pick_marker;
  lv_obj_t *map_pick_confirm_button;
  lv_obj_t *map_page_image;
  lv_obj_t *location_marker;
  lv_obj_t *location_marker_core;
  lv_obj_t *location_label;
  lv_obj_t *voice_status_label;
  lv_obj_t *voice_icon_panel;
  lv_obj_t *voice_quick_panel;
  lv_obj_t *voice_textarea;
  lv_obj_t *voice_ime;
  lv_obj_t *voice_textarea_panel;
  lv_obj_t *voice_cand_panel;
  lv_obj_t *voice_keyboard;
  lv_obj_t *weather_date_label;
  lv_obj_t *weather_time_label;
  lv_obj_t *weather_icon_label;
  lv_obj_t *weather_temp_label;
  lv_obj_t *weather_cond_label;
  lv_obj_t *weather_detail_label;
  lv_obj_t *weather_source_label;
  lv_obj_t *music_title_label;
  lv_obj_t *music_artist_label;
  lv_obj_t *music_source_label;
  lv_obj_t *music_play_btn;
  lv_obj_t *music_play_btn_label;
  lv_obj_t *music_progress_bar;
  lv_obj_t *music_time_label;
  lv_obj_t *music_vol_label;
  lv_obj_t *music_track_label;
  lv_obj_t *music_src_sdcard_btn;
  lv_obj_t *music_src_wifi_btn;
  lv_obj_t *music_src_bt_btn;
  lv_obj_t *music_conn_label;
  lv_obj_t *video_state_label;
  lv_obj_t *video_name_label;
  lv_obj_t *video_progress_bar;
  lv_obj_t *video_time_label;
  lv_obj_t *video_file_list;
  lv_obj_t *video_path_label;
  lv_obj_t *video_browser_list;
  lv_obj_t *video_btn_up;
  lv_obj_t *video_btn_down;
  lv_obj_t *video_btn_play;
  lv_obj_t *video_btn_back;
  int video_browser_top;      /* first visible entry index */
  int video_browser_sel;      /* selected entry index */
  int video_browser_count;    /* total entries */
  char video_browser_cwd[128]; /* current directory */
  int voice_input_visible;
  int voice_status_manual;
  struct qiban_video_state_s video_state;
  struct qiban_vehicle_state_s state;
  struct qiban_map_state_s map_state;
  struct qiban_nav_state_s nav_state;
  struct qiban_location_state_s location_state;
  struct qiban_weather_state_s weather_state;
  struct qiban_music_state_s music_state;
  struct qiban_voice_state_s voice_state;
  lv_point_precise_t map_route_points[2];
  char active_map_path[96];
  int64_t active_map_updated_at;
  int map_view_center_lon_e7;
  int map_view_center_lat_e7;
  int map_view_zoom;
  int map_route_start_lon_e7;
  int map_route_start_lat_e7;
  int map_route_end_lon_e7;
  int map_route_end_lat_e7;
  uint32_t map_route_total_distance_m;
  int map_route_active;
  int map_local_route_active;
  int map_pick_start_lon_e7;
  int map_pick_start_lat_e7;
  int map_pick_start_valid;
  int map_pick_end_lon_e7;
  int map_pick_end_lat_e7;
  int map_pick_end_valid;
  int map_view_override_active;
  int active_page;
  int nav_active_latched;
  uint32_t tick;
  int touch_pinch_fd;
  bool touch_pinch_active;
  int32_t touch_pinch_last_distance;
  int32_t touch_pinch_last_mid_x;
  int32_t touch_pinch_last_mid_y;
  int32_t touch_pinch_zoom_accum;
};

static struct qiban_ui_s *g_qiban_ui;
static lv_offline_nav_point_t g_qiban_nav_route_points[QIBAN_OFFLINE_NAV_ROUTE_RENDER_MAX];
static lv_offline_nav_turn_event_t g_qiban_nav_turn_events[QIBAN_OFFLINE_NAV_TURN_EVENT_MAX];
static lv_offline_nav_route_t g_qiban_nav_route =
{
  .points = g_qiban_nav_route_points,
  .point_capacity = QIBAN_OFFLINE_NAV_ROUTE_RENDER_MAX,
  .point_count = 0U,
  .turn_events = g_qiban_nav_turn_events,
  .turn_event_capacity = QIBAN_OFFLINE_NAV_TURN_EVENT_MAX,
  .turn_event_count = 0U,
  .total_distance_m = 0U,
  .center_point = {0, 0, 0U},
  .has_center_point = false
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void qiban_ui_set_page(struct qiban_ui_s *ui, int page,
                              lv_anim_enable_t anim_en);
static void qiban_ui_set_voice_input_visible(struct qiban_ui_s *ui,
                                             bool visible);
static void qiban_ui_set_map_pending(struct qiban_ui_s *ui,
                                     FAR const char *text);
static void qiban_ui_request_map_refresh(FAR const char *status_text);
static int qiban_ui_start_navigation(struct qiban_ui_s *ui,
                                     FAR const char *text);
static void qiban_ui_refresh_map(struct qiban_ui_s *ui);
static void qiban_ui_refresh_location_marker(struct qiban_ui_s *ui);
static void qiban_ui_sync_offline_map_view(struct qiban_ui_s *ui);
static void qiban_ui_sync_offline_route(struct qiban_ui_s *ui);
static void qiban_ui_refresh_map_overlay(struct qiban_ui_s *ui);
static void qiban_ui_refresh_offline_guidance(struct qiban_ui_s *ui);
static void qiban_ui_map_update_pick_overlay(struct qiban_ui_s *ui);
static void qiban_ui_publish_local_nav_state(struct qiban_ui_s *ui);
static void qiban_ui_capture_map_view_state(struct qiban_ui_s *ui,
                                            bool persist);
static void qiban_ui_refresh_weather(struct qiban_ui_s *ui);
static void qiban_ui_refresh_voice(struct qiban_ui_s *ui);
static void qiban_ui_refresh_music(struct qiban_ui_s *ui);
static void qiban_ui_refresh_video(struct qiban_ui_s *ui);
static void qiban_ui_map_event_cb(lv_event_t *event);
static void qiban_ui_map_pick_confirm_event_cb(lv_event_t *event);

static void qiban_state_set_defaults(struct qiban_vehicle_state_s *state)
{
  snprintf(state->source, sizeof(state->source), "%s", "mock:qiban_ui");
  state->speed_kmh = 18;
  state->battery_percent = 78;
  state->remaining_range_km = 34;
  state->ride_duration_min = 8;
  state->total_distance_km_x10 = 126;
  state->nav_remaining_m = 4200;
  state->alert_overspeed = 0;
  state->alert_low_battery = 0;
  state->alert_fatigue = 0;
}

static void qiban_map_set_defaults(struct qiban_map_state_s *state)
{
  snprintf(state->source, sizeof(state->source), "%s", "none");
  snprintf(state->title, sizeof(state->title), "%s", "Map");
  snprintf(state->status, sizeof(state->status), "%s", "waiting for map");
  snprintf(state->mode, sizeof(state->mode), "%s", "point");
  state->image_path[0] = '\0';
  state->center_longitude = 0.0;
  state->center_latitude = 0.0;
  state->zoom = 0;
  state->has_route = 0;
  state->route_start_longitude = 0.0;
  state->route_start_latitude = 0.0;
  state->route_end_longitude = 0.0;
  state->route_end_latitude = 0.0;
  state->updated_at = 0;
  state->ready = 0;
}

static int qiban_ui_coord_to_e7(double value)
{
  double scaled;

  if (!isfinite(value))
    {
      return 0;
    }

  scaled = value * 10000000.0;
  return scaled >= 0.0 ? (int)(scaled + 0.5) : (int)(scaled - 0.5);
}

static bool qiban_ui_coord_is_valid(double value, double minimum,
                                    double maximum)
{
  return isfinite(value) && value >= minimum && value <= maximum;
}

static double qiban_ui_e7_to_coord(int value_e7)
{
  return (double)value_e7 / 10000000.0;
}

static int qiban_ui_effective_map_zoom(const struct qiban_ui_s *ui)
{
  int zoom;

  if (ui != NULL &&
      ui->map_state.zoom >= QIBAN_OFFLINE_MAP_MIN_ZOOM &&
      ui->map_state.zoom <= QIBAN_OFFLINE_MAP_MAX_ZOOM)
    {
      return ui->map_state.zoom;
    }

  zoom = QIBAN_OFFLINE_MAP_DEFAULT_ZOOM;
  if (zoom < QIBAN_OFFLINE_MAP_MIN_ZOOM)
    {
      zoom = QIBAN_OFFLINE_MAP_MIN_ZOOM;
    }
  else if (zoom > QIBAN_OFFLINE_MAP_MAX_ZOOM)
    {
      zoom = QIBAN_OFFLINE_MAP_MAX_ZOOM;
    }

  return zoom;
}

static void qiban_ui_get_effective_map_center(const struct qiban_ui_s *ui,
                                              int *lon_e7, int *lat_e7)
{
  double longitude;
  double latitude;

  longitude = QIBAN_OFFLINE_MAP_DEFAULT_LONGITUDE;
  latitude = QIBAN_OFFLINE_MAP_DEFAULT_LATITUDE;

  if (ui != NULL)
    {
      if (ui->map_state.center_longitude != 0.0 ||
          ui->map_state.center_latitude != 0.0)
        {
          longitude = ui->map_state.center_longitude;
          latitude = ui->map_state.center_latitude;
        }
      else if (ui->location_state.valid)
        {
          longitude = ui->location_state.longitude;
          latitude = ui->location_state.latitude;
        }
      else if (ui->nav_state.active &&
               (ui->nav_state.start_longitude != 0.0 ||
                ui->nav_state.start_latitude != 0.0))
        {
          longitude = ui->nav_state.start_longitude;
          latitude = ui->nav_state.start_latitude;
        }
    }

  if (lon_e7 != NULL)
    {
      *lon_e7 = qiban_ui_coord_to_e7(longitude);
    }

  if (lat_e7 != NULL)
    {
      *lat_e7 = qiban_ui_coord_to_e7(latitude);
    }
}

static void qiban_ui_capture_map_view_state(struct qiban_ui_s *ui,
                                            bool persist)
{
  lv_coord_t center_x;
  lv_coord_t center_y;
  int lon_e7;
  int lat_e7;
  int zoom;

  (void)persist;

  if (ui == NULL || ui->map_widget == NULL)
    {
      return;
    }

  center_x = lv_obj_get_width(ui->map_widget) / 2;
  center_y = lv_obj_get_height(ui->map_widget) / 2;
  if (center_x <= 0 || center_y <= 0)
    {
      return;
    }

  zoom = lv_offline_map_get_zoom(ui->map_widget);
  if (zoom < QIBAN_OFFLINE_MAP_MIN_ZOOM || zoom > QIBAN_OFFLINE_MAP_MAX_ZOOM)
    {
      zoom = qiban_ui_effective_map_zoom(ui);
    }

  if (!lv_offline_map_view_point_to_lonlat_e7(ui->map_widget, center_x,
                                              center_y, &lon_e7, &lat_e7))
    {
      return;
    }

  ui->map_view_center_lon_e7 = lon_e7;
  ui->map_view_center_lat_e7 = lat_e7;
  ui->map_view_zoom = zoom;
  ui->map_state.center_longitude = qiban_ui_e7_to_coord(lon_e7);
  ui->map_state.center_latitude = qiban_ui_e7_to_coord(lat_e7);
  ui->map_state.zoom = zoom;
  ui->map_view_override_active = 1;
}

static void qiban_ui_format_distance(char *buffer, size_t buffer_size,
                                     uint32_t distance_m)
{
  if (buffer == NULL || buffer_size == 0U)
    {
      return;
    }

  if (distance_m >= 1000U)
    {
      snprintf(buffer, buffer_size, "%lu.%lukm",
               (unsigned long)(distance_m / 1000U),
               (unsigned long)((distance_m % 1000U) / 100U));
    }
  else
    {
      snprintf(buffer, buffer_size, "%lum",
               (unsigned long)distance_m);
    }
}

static FAR const char *qiban_ui_nav_turn_text(lv_offline_nav_turn_type_t type)
{
  switch (type)
    {
      case LV_OFFLINE_NAV_TURN_LEFT:
        return "Left";

      case LV_OFFLINE_NAV_TURN_RIGHT:
        return "Right";

      case LV_OFFLINE_NAV_TURN_UTURN:
        return "U-turn";

      case LV_OFFLINE_NAV_TURN_NONE:
      default:
        return "Straight";
    }
}

static void qiban_ui_fill_offline_nav_config(lv_offline_nav_config_t *config)
{
  if (config == NULL)
    {
      return;
    }

  lv_offline_nav_get_default_config(config);
  config->graph_path = QIBAN_OFFLINE_NAV_GRAPH_PATH;
  config->max_render_points = QIBAN_OFFLINE_NAV_ROUTE_RENDER_MAX;
  config->max_turn_events = QIBAN_OFFLINE_NAV_TURN_EVENT_MAX;
}

static void qiban_ui_clear_picked_destination(struct qiban_ui_s *ui)
{
  if (ui == NULL)
    {
      return;
    }

  ui->map_pick_start_valid = 0;
  ui->map_pick_end_valid = 0;
  qiban_ui_map_update_pick_overlay(ui);
}

static void qiban_ui_clear_route_cache(struct qiban_ui_s *ui)
{
  if (ui == NULL)
    {
      return;
    }

  ui->map_route_start_lon_e7 = 0;
  ui->map_route_start_lat_e7 = 0;
  ui->map_route_end_lon_e7 = 0;
  ui->map_route_end_lat_e7 = 0;
  ui->map_route_total_distance_m = 0;
  ui->map_route_active = 0;
}

static void qiban_ui_clear_local_route(struct qiban_ui_s *ui)
{
  if (ui == NULL)
    {
      return;
    }

  if (ui->map_widget != NULL)
    {
      lv_offline_map_track_stop_follow(ui->map_widget);
      lv_offline_map_track_clear(ui->map_widget);
    }

  lv_offline_nav_clear_route(&g_qiban_nav_route);
  ui->map_local_route_active = 0;
  qiban_ui_clear_route_cache(ui);
}

static bool qiban_ui_get_route_start_e7(struct qiban_ui_s *ui,
                                        int *start_lon_e7,
                                        int *start_lat_e7)
{
  if (ui == NULL || start_lon_e7 == NULL || start_lat_e7 == NULL)
    {
      return false;
    }

  if (ui->map_pick_start_valid)
    {
      *start_lon_e7 = ui->map_pick_start_lon_e7;
      *start_lat_e7 = ui->map_pick_start_lat_e7;
      return true;
    }

  if (ui->location_state.valid)
    {
      *start_lon_e7 = qiban_ui_coord_to_e7(ui->location_state.longitude);
      *start_lat_e7 = qiban_ui_coord_to_e7(ui->location_state.latitude);
      return true;
    }

  if (ui->map_widget != NULL &&
      lv_offline_map_track_get_follow_location_e7(ui->map_widget,
                                                  start_lon_e7,
                                                  start_lat_e7))
    {
      return true;
    }

  if (ui->nav_state.active &&
      (ui->nav_state.start_longitude != 0.0 ||
       ui->nav_state.start_latitude != 0.0))
    {
      *start_lon_e7 = qiban_ui_coord_to_e7(ui->nav_state.start_longitude);
      *start_lat_e7 = qiban_ui_coord_to_e7(ui->nav_state.start_latitude);
      return true;
    }

  qiban_ui_get_effective_map_center(ui, start_lon_e7, start_lat_e7);
  return true;
}

static bool qiban_ui_render_nav_route(struct qiban_ui_s *ui,
                                      const lv_offline_nav_route_t *route)
{
  uint32_t point_index;
  int route_zoom;

  if (ui == NULL || ui->map_widget == NULL || route == NULL ||
      route->points == NULL || route->point_count < 2U)
    {
      printf("[qiban-debug] render_nav_route: guard failed "
            "ui=%p map_widget=%p route=%p points=%p "
            "point_count=%u\n",
            (void *)ui, ui != NULL ? (void *)ui->map_widget : NULL,
            (void *)route, route != NULL ? (void *)route->points : NULL,
            route != NULL ? (unsigned int)route->point_count : 0U);
      return false;
    }

  route_zoom = lv_offline_map_get_zoom(ui->map_widget);
  if (route_zoom <= 0)
    {
      route_zoom = qiban_ui_effective_map_zoom(ui);
    }

  if (route->has_center_point &&
      lv_offline_map_center_lonlat_e7(ui->map_widget,
                                      route->center_point.lon_e7,
                                      route->center_point.lat_e7,
                                      route_zoom))
    {
      ui->map_view_center_lon_e7 = route->center_point.lon_e7;
      ui->map_view_center_lat_e7 = route->center_point.lat_e7;
      ui->map_view_zoom = route_zoom;
    }

  lv_offline_map_track_stop_follow(ui->map_widget);
  lv_offline_map_track_clear(ui->map_widget);
  lv_offline_map_track_begin_batch(ui->map_widget);
  for (point_index = 0U; point_index < route->point_count; point_index++)
    {
      if (!lv_offline_map_track_add_lonlat_e7_with_distance(
              ui->map_widget,
              route->points[point_index].lon_e7,
              route->points[point_index].lat_e7,
              route_zoom,
              route->points[point_index].distance_m))
        {
          lv_offline_map_track_end_batch(ui->map_widget);
          lv_offline_map_track_clear(ui->map_widget);
          qiban_ui_clear_route_cache(ui);
          return false;
        }
    }

  lv_offline_map_track_end_batch(ui->map_widget);
  ui->map_route_start_lon_e7 = route->points[0].lon_e7;
  ui->map_route_start_lat_e7 = route->points[0].lat_e7;
  ui->map_route_end_lon_e7 = route->points[route->point_count - 1U].lon_e7;
  ui->map_route_end_lat_e7 = route->points[route->point_count - 1U].lat_e7;
  ui->map_route_total_distance_m = route->total_distance_m;
  ui->map_route_active = 1;
  return true;
}

static bool qiban_ui_plan_local_route(struct qiban_ui_s *ui,
                                      int end_lon_e7,
                                      int end_lat_e7)
{
  lv_offline_nav_config_t config;
  int start_lon_e7;
  int start_lat_e7;
  bool manual_start_selected;

  if (ui == NULL || ui->map_widget == NULL)
    {
      return false;
    }

  manual_start_selected = ui->map_pick_start_valid ? true : false;

  if (!qiban_ui_get_route_start_e7(ui, &start_lon_e7, &start_lat_e7))
    {
      return false;
    }

  qiban_ui_fill_offline_nav_config(&config);
  lv_offline_nav_clear_route(&g_qiban_nav_route);

  {
    struct stat graph_stat;
    bool graph_exists = (stat(config.graph_path, &graph_stat) == 0);

    printf("[qiban-debug] nav: graph_path=%s exists=%d start=(%d,%d) "
          "end=(%d,%d)\n", config.graph_path, (int)graph_exists,
          start_lon_e7, start_lat_e7, end_lon_e7, end_lat_e7);
  }

  if (!lv_offline_nav_plan_route(&config, start_lon_e7, start_lat_e7,
                                 end_lon_e7, end_lat_e7,
                                 &g_qiban_nav_route))
    {
      printf("[qiban-debug] nav: lv_offline_nav_plan_route failed\n");
      lv_offline_nav_clear_route(&g_qiban_nav_route);
      ui->map_local_route_active = 0;
      qiban_ui_clear_route_cache(ui);
      return false;
    }

  if (!qiban_ui_render_nav_route(ui, &g_qiban_nav_route))
    {
      printf("[qiban-debug] nav: qiban_ui_render_nav_route failed "
            "(point_count=%u)\n",
            (unsigned int)g_qiban_nav_route.point_count);
      lv_offline_nav_clear_route(&g_qiban_nav_route);
      ui->map_local_route_active = 0;
      qiban_ui_clear_route_cache(ui);
      return false;
    }

  ui->map_local_route_active = 1;
  qiban_ui_publish_local_nav_state(ui);
  qiban_ui_clear_picked_destination(ui);
  if (manual_start_selected)
    {
      lv_offline_map_track_start_follow(ui->map_widget);
    }
  else if (ui->location_state.valid)
    {
      qiban_ui_refresh_location_marker(ui);
    }
  else
    {
      lv_offline_map_track_start_follow(ui->map_widget);
    }

  qiban_ui_refresh_map(ui);
  return true;
}

static bool qiban_ui_map_is_route_select_target(struct qiban_ui_s *ui,
                                                lv_obj_t *target)
{
  lv_obj_t *container;

  if (ui == NULL || ui->map_widget == NULL)
    {
      return false;
    }

  container = lv_offline_map_get_container(ui->map_widget);
  return target == ui->map_widget || target == container;
}

static bool qiban_ui_handle_map_long_press(struct qiban_ui_s *ui,
                                           lv_event_t *event)
{
  lv_obj_t *target;
  lv_indev_t *indev;
  lv_area_t map_area;
  lv_point_t press_point;
  lv_coord_t view_x;
  lv_coord_t view_y;
  int lon_e7;
  int lat_e7;

  if (ui == NULL || event == NULL)
    {
      return false;
    }

  target = lv_event_get_target(event);
  if (!qiban_ui_map_is_route_select_target(ui, target))
    {
      return false;
    }

  indev = QIBAN_UI_ACTIVE_INDEV();
  if (indev == NULL)
    {
      qiban_ui_set_map_pending(ui, "Touch point unavailable");
      return true;
    }

  lv_indev_get_point(indev, &press_point);
  lv_obj_get_coords(ui->map_widget, &map_area);
  view_x = press_point.x - map_area.x1;
  view_y = press_point.y - map_area.y1;
  if (!lv_offline_map_view_point_to_lonlat_e7(ui->map_widget, view_x, view_y,
                                              &lon_e7, &lat_e7))
    {
      qiban_ui_set_map_pending(ui, "Press point is outside the map");
      return true;
    }

  if (ui->map_pick_start_valid && ui->map_pick_end_valid)
    {
      qiban_ui_clear_local_route(ui);
      ui->map_pick_start_valid = 0;
      ui->map_pick_end_valid = 0;
    }

  if (!ui->map_pick_start_valid)
    {
      ui->map_pick_start_lon_e7 = lon_e7;
      ui->map_pick_start_lat_e7 = lat_e7;
      ui->map_pick_start_valid = 1;
      qiban_ui_map_update_pick_overlay(ui);
      qiban_ui_set_map_pending(ui, "Start selected, pick destination");
      return true;
    }

  ui->map_pick_end_lon_e7 = lon_e7;
  ui->map_pick_end_lat_e7 = lat_e7;
  ui->map_pick_end_valid = 1;
  qiban_ui_map_update_pick_overlay(ui);
  qiban_ui_set_map_pending(ui, "Route points selected, tap confirm");
  return true;
}

static void qiban_nav_set_defaults(struct qiban_nav_state_s *state)
{
  state->destination[0] = '\0';
  snprintf(state->status, sizeof(state->status), "%s", "Idle");
  state->next_turn[0] = '\0';
  state->total_distance_m = 0;
  state->remaining_distance_m = 0;
  state->eta_minutes = 0;
  state->start_longitude = 0.0;
  state->start_latitude = 0.0;
  state->end_longitude = 0.0;
  state->end_latitude = 0.0;
  state->active = 0;
}

static void qiban_location_set_defaults(struct qiban_location_state_s *state)
{
  snprintf(state->source, sizeof(state->source), "%s", "none");
  state->longitude = 0.0;
  state->latitude = 0.0;
  state->accuracy_m = 0;
  state->valid = 0;
  state->updated_at = 0;
}

static void qiban_weather_set_defaults(struct qiban_weather_state_s *state)
{
  snprintf(state->condition, sizeof(state->condition), "clear");
  state->temperature_c = 28;
  state->humidity = 45;
  state->wind_speed_kmh = 12;
  snprintf(state->city, sizeof(state->city), "北京");
  snprintf(state->source, sizeof(state->source), "local");
  state->updated_at = 0;
  state->valid = 1;
}

static void qiban_music_set_defaults(struct qiban_music_state_s *state)
{
  snprintf(state->title, sizeof(state->title), "未在播放");
  state->artist[0] = '\0';
  state->album[0] = '\0';
  snprintf(state->source, sizeof(state->source), "sdcard");
  snprintf(state->source_status, sizeof(state->source_status), "unknown");
  state->playing = 0;
  state->duration_sec = 0;
  state->position_sec = 0;
  state->volume = 60;
  state->track_index = 0;
  state->track_count = 0;
  state->sdcard_available = 0;
  state->wifi_connected = 0;
  state->bluetooth_connected = 0;
  state->valid = 0;
}

static void qiban_voice_set_defaults(struct qiban_voice_state_s *state)
{
  state->status[0] = '\0';
  state->recognized_text[0] = '\0';
  state->intent_text[0] = '\0';
  state->action[0] = '\0';
  state->target[0] = '\0';
  state->valid = 0;
}

static void qiban_video_set_defaults(struct qiban_video_state_s *state)
{
  snprintf(state->state, sizeof(state->state), "stopped");
  state->current_name[0] = '\0';
  state->current_index = 0;
  state->position_sec = 0;
  state->duration_sec = 0;
  state->file_count = 0;
  state->error_msg[0] = '\0';
  state->valid = 0;
}

static void qiban_state_step_mock(struct qiban_vehicle_state_s *state,
                                  uint32_t tick)
{
  static const int speed_pattern[] = {18, 21, 26, 29, 24, 19, 22, 27};
  int speed_index;

  speed_index = tick % (sizeof(speed_pattern) / sizeof(speed_pattern[0]));
  state->speed_kmh = speed_pattern[speed_index];
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

  if (tick > 0 && tick % 4 == 0 && state->battery_percent > 8)
    {
      state->battery_percent -= 1;
    }

  state->remaining_range_km = state->battery_percent / 2;
  state->alert_overspeed = state->speed_kmh > 25;
  state->alert_low_battery = state->battery_percent <= 20;
  state->alert_fatigue = state->ride_duration_min >= 45;
}

static const char *qiban_json_find_key(const char *json, const char *key)
{
  char pattern[64];

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  return strstr(json, pattern);
}

static bool qiban_json_extract_int(const char *json, const char *key,
                                   int *out_value)
{
  const char *cursor;
  char *endptr;
  long value;

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
  while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
      cursor++;
    }

  value = strtol(cursor, &endptr, 10);
  if (cursor == endptr)
    {
      return false;
    }

  *out_value = (int)value;
  return true;
}

static bool qiban_json_extract_int64(const char *json, const char *key,
                                     int64_t *out_value)
{
  const char *cursor;
  char *endptr;
  long long value;

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
  while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
      cursor++;
    }

  value = strtoll(cursor, &endptr, 10);
  if (cursor == endptr)
    {
      return false;
    }

  *out_value = (int64_t)value;
  return true;
}

static bool qiban_json_extract_double(const char *json, const char *key,
                                      double *out_value)
{
  const char *cursor;
  char *endptr;
  double value;

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
  while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
      cursor++;
    }

  errno = 0;
  value = strtod(cursor, &endptr);
  if (cursor == endptr || errno != 0 || !isfinite(value))
    {
      return false;
    }

  while (*endptr != '\0' && isspace((unsigned char)*endptr))
    {
      endptr++;
    }

  if (*endptr != '\0' && *endptr != ',' && *endptr != '}' && *endptr != ']')
    {
      return false;
    }

  *out_value = value;
  return true;
}

static bool qiban_json_extract_bool(const char *json, const char *key,
                                    int *out_value)
{
  const char *cursor;

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
  while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
      cursor++;
    }

  if (strncmp(cursor, "true", 4) == 0)
    {
      *out_value = 1;
      return true;
    }

  if (strncmp(cursor, "false", 5) == 0)
    {
      *out_value = 0;
      return true;
    }

  if (*cursor == '1')
    {
      *out_value = 1;
      return true;
    }

  if (*cursor == '0')
    {
      *out_value = 0;
      return true;
    }

  return false;
}

static int qiban_json_hex_digit(char ch)
{
  if (ch >= '0' && ch <= '9')
    {
      return ch - '0';
    }

  if (ch >= 'a' && ch <= 'f')
    {
      return ch - 'a' + 10;
    }

  if (ch >= 'A' && ch <= 'F')
    {
      return ch - 'A' + 10;
    }

  return -1;
}

static bool qiban_json_parse_u16(const char *text, uint32_t *out_value)
{
  uint32_t value = 0;

  for (int i = 0; i < 4; i++)
    {
      int digit = qiban_json_hex_digit(text[i]);
      if (digit < 0)
        {
          return false;
        }

      value = (value << 4) | (uint32_t)digit;
    }

  *out_value = value;
  return true;
}

static size_t qiban_json_utf8_length(uint32_t codepoint)
{
  if (codepoint <= 0x7f)
    {
      return 1;
    }

  if (codepoint <= 0x7ff)
    {
      return 2;
    }

  if (codepoint <= 0xffff)
    {
      return 3;
    }

  return 4;
}

static bool qiban_json_append_utf8(uint32_t codepoint,
                                   char **dst, size_t *remaining)
{
  size_t length;

  if (codepoint > 0x10ffff)
    {
      codepoint = 0xfffd;
    }

  length = qiban_json_utf8_length(codepoint);
  if (*remaining <= length)
    {
      return false;
    }

  if (length == 1)
    {
      *(*dst)++ = (char)codepoint;
    }
  else if (length == 2)
    {
      *(*dst)++ = (char)(0xc0 | (codepoint >> 6));
      *(*dst)++ = (char)(0x80 | (codepoint & 0x3f));
    }
  else if (length == 3)
    {
      *(*dst)++ = (char)(0xe0 | (codepoint >> 12));
      *(*dst)++ = (char)(0x80 | ((codepoint >> 6) & 0x3f));
      *(*dst)++ = (char)(0x80 | (codepoint & 0x3f));
    }
  else
    {
      *(*dst)++ = (char)(0xf0 | (codepoint >> 18));
      *(*dst)++ = (char)(0x80 | ((codepoint >> 12) & 0x3f));
      *(*dst)++ = (char)(0x80 | ((codepoint >> 6) & 0x3f));
      *(*dst)++ = (char)(0x80 | (codepoint & 0x3f));
    }

  *remaining -= length;
  return true;
}

static bool qiban_json_extract_string(const char *json, const char *key,
                                      char *buffer, size_t buffer_size)
{
  const char *cursor;
  const char *start;
  const char *src;
  char *dst;
  size_t remaining;

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
  src = start;
  dst = buffer;
  remaining = buffer_size;

  if (buffer_size == 0)
    {
      return false;
    }

  while (*src != '\0' && *src != '"' && remaining > 1)
    {
      if (*src == '\\')
        {
          src++;
          if (*src == '\0')
            {
              break;
            }

          switch (*src)
            {
              case '"':
              case '\\':
              case '/':
                *dst++ = *src++;
                remaining--;
                break;

              case 'b':
                *dst++ = '\b';
                src++;
                remaining--;
                break;

              case 'f':
                *dst++ = '\f';
                src++;
                remaining--;
                break;

              case 'n':
                *dst++ = '\n';
                src++;
                remaining--;
                break;

              case 'r':
                *dst++ = '\r';
                src++;
                remaining--;
                break;

              case 't':
                *dst++ = '\t';
                src++;
                remaining--;
                break;

              case 'u':
                {
                  uint32_t codepoint;

                  if (!qiban_json_parse_u16(src + 1, &codepoint))
                    {
                      *dst++ = *src++;
                      remaining--;
                      break;
                    }

                  src += 5;
                  if (codepoint >= 0xd800 && codepoint <= 0xdbff &&
                      src[0] == '\\' && src[1] == 'u')
                    {
                      uint32_t low;

                      if (qiban_json_parse_u16(src + 2, &low) &&
                          low >= 0xdc00 && low <= 0xdfff)
                        {
                          codepoint =
                            0x10000 + ((codepoint - 0xd800) << 10) +
                            (low - 0xdc00);
                          src += 6;
                        }
                    }

                  if (!qiban_json_append_utf8(codepoint, &dst, &remaining))
                    {
                      remaining = 1;
                    }
                }
                break;

              default:
                *dst++ = *src++;
                remaining--;
                break;
            }
        }
      else
        {
          *dst++ = *src++;
          remaining--;
        }
    }

  *dst = '\0';
  return *src == '"';
}

static bool qiban_file_exists(const char *path)
{
  FILE *fp;

  if (path == NULL || *path == '\0')
    {
      return false;
    }

  fp = fopen(path, "rb");
  if (fp == NULL)
    {
      return false;
    }

  fclose(fp);
  return true;
}

static bool qiban_write_text_file(const char *path, const char *content)
{
  FILE *fp;

  if (path == NULL || content == NULL)
    {
      return false;
    }

  fp = fopen(path, "w");
  if (fp == NULL)
    {
      return false;
    }

  if (fputs(content, fp) == EOF || ferror(fp) != 0)
    {
      fclose(fp);
      return false;
    }

  return fclose(fp) == 0;
}

static void qiban_ui_publish_local_nav_state(struct qiban_ui_s *ui)
{
  char buffer[768];
  char remaining[32];
  uint32_t total_distance_m;
  int eta_minutes;

  if (ui == NULL || ui->map_widget == NULL || !ui->map_local_route_active)
    {
      return;
    }

  total_distance_m = ui->map_route_total_distance_m;
  if (total_distance_m == 0U)
    {
      total_distance_m =
        lv_offline_map_track_get_total_distance_m(ui->map_widget);
    }

  eta_minutes = (int)(total_distance_m / 350U);
  if (eta_minutes < 1)
    {
      eta_minutes = 1;
    }

  snprintf(buffer, sizeof(buffer),
           "{\n"
           "  \"schema_version\": 1,\n"
           "  \"source\": \"board:qiban_ui:offline-route\",\n"
           "  \"generated_at_epoch_s\": %ld,\n"
           "  \"active\": true,\n"
           "  \"destination\": \"地图选点\",\n"
           "  \"status\": \"瓦片路线已规划\",\n"
           "  \"next_turn\": \"沿瓦片地图规划路线行驶\",\n"
           "  \"total_distance_m\": %lu,\n"
           "  \"remaining_distance_m\": %lu,\n"
           "  \"eta_minutes\": %d,\n"
           "  \"start_longitude\": %.6f,\n"
           "  \"start_latitude\": %.6f,\n"
           "  \"end_longitude\": %.6f,\n"
           "  \"end_latitude\": %.6f\n"
           "}\n",
           (long)time(NULL),
           (unsigned long)total_distance_m,
           (unsigned long)total_distance_m,
           eta_minutes,
           qiban_ui_e7_to_coord(ui->map_route_start_lon_e7),
           qiban_ui_e7_to_coord(ui->map_route_start_lat_e7),
           qiban_ui_e7_to_coord(ui->map_route_end_lon_e7),
           qiban_ui_e7_to_coord(ui->map_route_end_lat_e7));
  (void)qiban_write_text_file(QIBAN_NAV_STATE_PATH, buffer);

  (void)mkdir(QIBAN_NAV_INPUT_DIR, 0777);
  snprintf(remaining, sizeof(remaining), "%lu\n",
           (unsigned long)total_distance_m);
  (void)qiban_write_text_file(QIBAN_NAV_REMAINING_PATH, remaining);
}

static bool qiban_state_load_from_file(struct qiban_vehicle_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);

  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "source",
                                state->source, sizeof(state->source)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "speed_kmh", &state->speed_kmh))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "battery_percent",
                             &state->battery_percent))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "remaining_range_km",
                             &state->remaining_range_km))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "ride_duration_min",
                             &state->ride_duration_min))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "total_distance_km_x10",
                             &state->total_distance_km_x10))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "nav_remaining_m",
                             &state->nav_remaining_m))
    {
      parsed_fields++;
    }

  /* Canonical payload stores alert flags under alerts.{overspeed, ...}. */
  qiban_json_extract_bool(buffer, "overspeed", &state->alert_overspeed);
  qiban_json_extract_bool(buffer, "low_battery", &state->alert_low_battery);
  qiban_json_extract_bool(buffer, "fatigue", &state->alert_fatigue);

  return parsed_fields > 0;
}

static bool qiban_map_load_from_file(struct qiban_map_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_MAP_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "source",
                                state->source, sizeof(state->source)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "title",
                                state->title, sizeof(state->title)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "status",
                                state->status, sizeof(state->status)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "image_path",
                                state->image_path, sizeof(state->image_path)))
    {
      parsed_fields++;
    }

  qiban_json_extract_string(buffer, "mode",
                            state->mode, sizeof(state->mode));
  qiban_json_extract_double(buffer, "center_longitude",
                            &state->center_longitude);
  qiban_json_extract_double(buffer, "center_latitude",
                            &state->center_latitude);
  qiban_json_extract_int(buffer, "zoom", &state->zoom);
  qiban_json_extract_bool(buffer, "has_route", &state->has_route);
  qiban_json_extract_double(buffer, "route_start_longitude",
                            &state->route_start_longitude);
  qiban_json_extract_double(buffer, "route_start_latitude",
                            &state->route_start_latitude);
  qiban_json_extract_double(buffer, "route_end_longitude",
                            &state->route_end_longitude);
  qiban_json_extract_double(buffer, "route_end_latitude",
                            &state->route_end_latitude);
  qiban_json_extract_int64(buffer, "updated_at", &state->updated_at);

  if (!qiban_ui_coord_is_valid(state->center_longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->center_latitude, -90.0, 90.0))
    {
      state->center_longitude = 0.0;
      state->center_latitude = 0.0;
    }

  if (!qiban_ui_coord_is_valid(state->route_start_longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->route_start_latitude, -90.0, 90.0))
    {
      state->route_start_longitude = 0.0;
      state->route_start_latitude = 0.0;
    }

  if (!qiban_ui_coord_is_valid(state->route_end_longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->route_end_latitude, -90.0, 90.0))
    {
      state->route_end_longitude = 0.0;
      state->route_end_latitude = 0.0;
    }

  state->ready = qiban_file_exists(state->image_path) ? 1 : 0;
  return parsed_fields > 0;
}

static bool qiban_nav_load_from_file(struct qiban_nav_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_NAV_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "destination",
                                state->destination,
                                sizeof(state->destination)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "status",
                                state->status,
                                sizeof(state->status)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "next_turn",
                                state->next_turn,
                                sizeof(state->next_turn)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "remaining_distance_m",
                             &state->remaining_distance_m))
    {
      parsed_fields++;
    }

  qiban_json_extract_int(buffer, "total_distance_m",
                         &state->total_distance_m);
  qiban_json_extract_int(buffer, "eta_minutes", &state->eta_minutes);
  qiban_json_extract_double(buffer, "start_longitude",
                            &state->start_longitude);
  qiban_json_extract_double(buffer, "start_latitude",
                            &state->start_latitude);
  qiban_json_extract_double(buffer, "end_longitude",
                            &state->end_longitude);
  qiban_json_extract_double(buffer, "end_latitude",
                            &state->end_latitude);
  qiban_json_extract_bool(buffer, "active", &state->active);

  if (!qiban_ui_coord_is_valid(state->start_longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->start_latitude, -90.0, 90.0))
    {
      state->start_longitude = 0.0;
      state->start_latitude = 0.0;
    }

  if (!qiban_ui_coord_is_valid(state->end_longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->end_latitude, -90.0, 90.0))
    {
      state->end_longitude = 0.0;
      state->end_latitude = 0.0;
    }

  return parsed_fields > 0;
}

static bool qiban_location_load_from_file(struct qiban_location_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_LOCATION_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "source",
                                state->source, sizeof(state->source)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_double(buffer, "longitude", &state->longitude))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_double(buffer, "latitude", &state->latitude))
    {
      parsed_fields++;
    }

  qiban_json_extract_int(buffer, "accuracy_m", &state->accuracy_m);
  qiban_json_extract_int64(buffer, "updated_at", &state->updated_at);
  if (!qiban_ui_coord_is_valid(state->longitude, -180.0, 180.0) ||
      !qiban_ui_coord_is_valid(state->latitude, -90.0, 90.0))
    {
      state->longitude = 0.0;
      state->latitude = 0.0;
      parsed_fields = 0;
    }

  state->valid = parsed_fields >= 3;
  return state->valid;
}

static bool qiban_weather_load_from_file(struct qiban_weather_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_WEATHER_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "condition",
                                state->condition,
                                sizeof(state->condition)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "city",
                                state->city, sizeof(state->city)))
    {
      parsed_fields++;
    }

  qiban_json_extract_string(buffer, "source",
                            state->source, sizeof(state->source));
  qiban_json_extract_int64(buffer, "updated_at", &state->updated_at);
  qiban_json_extract_int64(buffer, "updated_at_epoch_s",
                           &state->updated_at);

  if (qiban_json_extract_int(buffer, "temperature_c",
                             &state->temperature_c))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "humidity", &state->humidity))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "wind_speed_kmh",
                             &state->wind_speed_kmh))
    {
      parsed_fields++;
    }

  if (state->humidity < 0)
    {
      state->humidity = 0;
    }
  else if (state->humidity > 100)
    {
      state->humidity = 100;
    }

  if (state->wind_speed_kmh < 0)
    {
      state->wind_speed_kmh = 0;
    }

  state->valid = parsed_fields > 0 && state->condition[0] != '\0';
  return state->valid;
}

static bool qiban_music_load_from_file(struct qiban_music_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_MUSIC_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  if (qiban_json_extract_string(buffer, "title",
                                state->title, sizeof(state->title)))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_string(buffer, "artist",
                                state->artist, sizeof(state->artist)))
    {
      parsed_fields++;
    }

  qiban_json_extract_string(buffer, "album",
                            state->album, sizeof(state->album));

  if (qiban_json_extract_string(buffer, "source",
                                state->source, sizeof(state->source)))
    {
      parsed_fields++;
    }

  qiban_json_extract_string(buffer, "source_status",
                            state->source_status,
                            sizeof(state->source_status));

  if (qiban_json_extract_int(buffer, "playing", &state->playing))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "duration_sec",
                             &state->duration_sec))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "position_sec",
                             &state->position_sec))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "volume", &state->volume))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "track_index", &state->track_index))
    {
      parsed_fields++;
    }

  if (qiban_json_extract_int(buffer, "track_count", &state->track_count))
    {
      parsed_fields++;
    }

  qiban_json_extract_bool(buffer, "sdcard_available",
                          &state->sdcard_available);
  qiban_json_extract_bool(buffer, "wifi_connected",
                          &state->wifi_connected);
  qiban_json_extract_bool(buffer, "bluetooth_connected",
                          &state->bluetooth_connected);

  if (state->duration_sec < 0)
    {
      state->duration_sec = 0;
    }

  if (state->position_sec < 0)
    {
      state->position_sec = 0;
    }
  else if (state->duration_sec > 0 &&
           state->position_sec > state->duration_sec)
    {
      state->position_sec = state->duration_sec;
    }

  if (state->volume < 0)
    {
      state->volume = 0;
    }
  else if (state->volume > 100)
    {
      state->volume = 100;
    }

  if (state->track_count < 0)
    {
      state->track_count = 0;
    }

  if (state->track_index < 0)
    {
      state->track_index = 0;
    }
  else if (state->track_count > 0 &&
           state->track_index >= state->track_count)
    {
      state->track_index = state->track_count - 1;
    }

  if (strcmp(state->source, "wifi") != 0 &&
      strcmp(state->source, "bluetooth") != 0)
    {
      snprintf(state->source, sizeof(state->source), "sdcard");
    }

  if (state->source_status[0] == '\0')
    {
      if (strcmp(state->source, "sdcard") == 0)
        {
          snprintf(state->source_status, sizeof(state->source_status),
                   state->track_count > 0 ? "ready" : "no_media");
        }
      else if (strcmp(state->source, "wifi") == 0)
        {
          snprintf(state->source_status, sizeof(state->source_status),
                   state->wifi_connected ? "connected" : "not_connected");
        }
      else
        {
          snprintf(state->source_status, sizeof(state->source_status),
                   state->bluetooth_connected ? "connected" : "not_ready");
        }
    }

  state->playing = state->playing ? 1 : 0;
  state->valid = parsed_fields > 0;
  return state->valid;
}

static bool qiban_voice_load_from_file(struct qiban_voice_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;
  int parsed_fields = 0;

  fp = fopen(QIBAN_VOICE_LAST_AUDIO_PATH, "r");
  if (fp != NULL)
    {
      nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
      fclose(fp);
      if (nread > 0)
        {
          buffer[nread] = '\0';

          if (qiban_json_extract_string(buffer, "status",
                                        state->status,
                                        sizeof(state->status)))
            {
              parsed_fields++;
            }

          if (qiban_json_extract_string(buffer, "recognized_text",
                                        state->recognized_text,
                                        sizeof(state->recognized_text)))
            {
              parsed_fields++;
            }
        }
    }

  fp = fopen(QIBAN_VOICE_LAST_INTENT_PATH, "r");
  if (fp != NULL)
    {
      nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
      fclose(fp);
      if (nread > 0)
        {
          buffer[nread] = '\0';

          if (qiban_json_extract_string(buffer, "intent_text",
                                        state->intent_text,
                                        sizeof(state->intent_text)))
            {
              parsed_fields++;
            }

          if (qiban_json_extract_string(buffer, "action",
                                        state->action,
                                        sizeof(state->action)))
            {
              parsed_fields++;
            }

          if (qiban_json_extract_string(buffer, "target",
                                        state->target,
                                        sizeof(state->target)))
            {
              parsed_fields++;
            }

          if (qiban_json_extract_string(buffer, "status",
                                        state->status,
                                        sizeof(state->status)))
            {
              parsed_fields++;
            }
        }
    }

  state->valid = parsed_fields > 0;
  return state->valid;
}

static bool qiban_video_load_from_file(struct qiban_video_state_s *state)
{
  char buffer[QIBAN_JSON_BUFFER_SIZE];
  FILE *fp;
  size_t nread;

  fp = fopen(QIBAN_VIDEO_STATE_PATH, "r");
  if (fp == NULL)
    {
      return false;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  if (nread == 0)
    {
      return false;
    }

  buffer[nread] = '\0';

  qiban_json_extract_string(buffer, "state",
                            state->state, sizeof(state->state));
  qiban_json_extract_string(buffer, "current_name",
                            state->current_name, sizeof(state->current_name));
  qiban_json_extract_string(buffer, "error_msg",
                            state->error_msg, sizeof(state->error_msg));
  qiban_json_extract_int(buffer, "current_index", &state->current_index);
  qiban_json_extract_int(buffer, "position_sec", &state->position_sec);
  qiban_json_extract_int(buffer, "duration_sec", &state->duration_sec);
  qiban_json_extract_int(buffer, "file_count", &state->file_count);

  state->valid = (state->state[0] != '\0');
  return state->valid;
}

static void video_browser_scan(FAR struct qiban_video_state_s *state,
                               FAR const char *path)
{
  FAR DIR *dir;
  FAR struct dirent *entry;
  struct stat st;
  int count = 0;

  if (path == NULL)
    {
      path = "/sdcard";
    }

  snprintf(state->cwd, sizeof(state->cwd), "%s", path);
  state->entry_count = 0;
  state->selected = 0;

  dir = opendir(path);
  if (dir == NULL)
    {
      path = "/data";
      dir = opendir(path);
      if (dir == NULL)
        {
          return;
        }

      snprintf(state->cwd, sizeof(state->cwd), "%s", path);
    }

  while ((entry = readdir(dir)) != NULL && count < 16)
    {
      FAR const char *name = entry->d_name;

      if (name[0] == '.' && (name[1] == '\0' ||
          (name[1] == '.' && name[2] == '\0')))
        {
          continue;
        }

      char full_path[128];
      snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

      snprintf(state->entries[count].name, 64, "%s", name);
      snprintf(state->entries[count].path, 128, "%s", full_path);

      if (stat(full_path, &st) == 0)
        {
          state->entries[count].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
          state->entries[count].size = state->entries[count].is_dir ?
                                       0 : st.st_size;
        }
      else
        {
          state->entries[count].is_dir = 0;
          state->entries[count].size = 0;
        }

      count++;
    }

  closedir(dir);
  state->entry_count = count;
}

static void qiban_apply_card_style(lv_obj_t *obj, lv_color_t bg_color)
{
  lv_obj_set_style_bg_color(obj, bg_color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x263244), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_pad_all(obj, 10, 0);
}

static void qiban_apply_button_style(lv_obj_t *obj, lv_color_t bg_color)
{
  lv_obj_set_style_bg_color(obj, bg_color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
}

static bool qiban_ui_starts_with(FAR const char *text,
                                 FAR const char *prefix)
{
  size_t prefix_length;

  if (text == NULL || prefix == NULL)
    {
      return false;
    }

  prefix_length = strlen(prefix);
  return strncmp(text, prefix, prefix_length) == 0;
}

static FAR const char *qiban_ui_skip_spaces(FAR const char *text)
{
  while (text != NULL && *text != '\0' &&
         isspace((unsigned char)*text))
    {
      text++;
    }

  return text;
}

static int qiban_ui_extract_destination(FAR const char *text,
                                        FAR char *buffer,
                                        size_t buffer_size)
{
  size_t length;
  FAR const char *start;

  if (text == NULL || buffer == NULL || buffer_size == 0)
    {
      return -EINVAL;
    }

  start = qiban_ui_skip_spaces(text);
  if (start == NULL || *start == '\0')
    {
      return -EINVAL;
    }

  if (qiban_ui_starts_with(start, "帮我导航到"))
    {
      start += strlen("帮我导航到");
    }
  else if (qiban_ui_starts_with(start, "开始导航到"))
    {
      start += strlen("开始导航到");
    }
  else if (qiban_ui_starts_with(start, "导航到"))
    {
      start += strlen("导航到");
    }
  else if (qiban_ui_starts_with(start, "带我去"))
    {
      start += strlen("带我去");
    }
  else if (qiban_ui_starts_with(start, "去"))
    {
      start += strlen("去");
    }

  start = qiban_ui_skip_spaces(start);
  if (start == NULL || *start == '\0')
    {
      return -EINVAL;
    }

  length = strlen(start);
  while (length > 0 &&
         isspace((unsigned char)start[length - 1]))
    {
      length--;
    }

  if (length == 0 || length >= buffer_size)
    {
      return -ENAMETOOLONG;
    }

  memcpy(buffer, start, length);
  buffer[length] = '\0';
  return OK;
}

static int qiban_ui_spawn_map_service(FAR char * const argv[])
{
  pid_t pid;
  int ret;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      fprintf(stderr, "failed to spawn qiban_map_service: %d\n", ret);
      return -ret;
    }

  return OK;
}

static int qiban_ui_spawn_nav_service(FAR char * const argv[])
{
  pid_t pid;
  int ret;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      fprintf(stderr, "failed to spawn %s: %d\n", argv[0], ret);
      return -ret;
    }

  return OK;
}

static int qiban_ui_spawn_voice_service(FAR char * const argv[])
{
  pid_t pid;
  int ret;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      fprintf(stderr, "failed to spawn %s: %d\n", argv[0], ret);
      return -ret;
    }

  return OK;
}

static int qiban_ui_spawn_music_service(FAR char * const argv[])
{
  pid_t pid;
  int ret;

  ret = posix_spawnp(&pid, argv[0], NULL, NULL, argv, NULL);
  if (ret != 0)
    {
      fprintf(stderr, "failed to spawn %s: %d\n", argv[0], ret);
      return -ret;
    }

  return OK;
}

static void qiban_ui_set_map_pending(struct qiban_ui_s *ui,
                                     FAR const char *text)
{
  if (ui == NULL || text == NULL)
    {
      return;
    }

  lv_obj_clear_flag(ui->map_page_status_label, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(ui->map_page_status_label, text);
}

static void qiban_ui_set_voice_input_visible(struct qiban_ui_s *ui,
                                             bool visible)
{
  if (ui == NULL)
    {
      return;
    }

  if (ui->voice_textarea_panel != NULL)
    {
      if (visible)
        {
          lv_obj_clear_flag(ui->voice_textarea_panel, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(ui->voice_textarea_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (ui->voice_keyboard != NULL)
    {
      if (visible)
        {
          lv_obj_clear_flag(ui->voice_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(ui->voice_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (ui->voice_cand_panel != NULL)
    {
      if (visible)
        {
          lv_obj_clear_flag(ui->voice_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(ui->voice_cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (ui->voice_quick_panel != NULL)
    {
      if (visible)
        {
          lv_obj_add_flag(ui->voice_quick_panel, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_clear_flag(ui->voice_quick_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

  ui->voice_input_visible = visible ? 1 : 0;
}

static int qiban_ui_start_navigation(struct qiban_ui_s *ui,
                                     FAR const char *text)
{
  FAR char *argv[4];
  char destination[QIBAN_JSON_BUFFER_SIZE / 4];

  if (ui == NULL)
    {
      return -EINVAL;
    }

  if (qiban_ui_extract_destination(text, destination,
                                   sizeof(destination)) < 0)
    {
      return -EINVAL;
    }

  argv[0] = "qiban_nav_service";
  argv[1] = "start";
  argv[2] = destination;
  argv[3] = NULL;
  if (qiban_ui_spawn_nav_service(argv) != OK)
    {
      return -EIO;
    }

  qiban_ui_set_voice_input_visible(ui, false);
  qiban_ui_request_map_refresh("正在刷新导航地图...");
  qiban_ui_set_page(ui, 1, LV_ANIM_ON);
  return OK;
}

static void qiban_ui_request_map_refresh(FAR const char *status_text)
{
  FAR char *argv[] =
  {
    "qiban_map_service",
    "refresh",
    NULL
  };

  if (g_qiban_ui == NULL)
    {
      return;
    }

  if (qiban_ui_spawn_map_service(argv) == OK)
    {
      qiban_ui_set_map_pending(g_qiban_ui,
                               status_text != NULL ? status_text
                                                   : "Refreshing map...");
    }
}

static void qiban_ui_map_event_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui;
  lv_event_code_t code;

  ui = g_qiban_ui;
  if (ui == NULL)
    {
      return;
    }

  code = lv_event_get_code(event);
  if (code == LV_EVENT_SCROLL &&
      qiban_ui_map_is_route_select_target(ui, lv_event_get_target(event)))
    {
      qiban_ui_map_update_pick_overlay(ui);
    }
  else if (code == LV_EVENT_SCROLL_END && ui->map_widget != NULL)
    {
      lv_obj_t *target = lv_event_get_target(event);
      lv_obj_t *container = lv_offline_map_get_container(ui->map_widget);

      if (target == ui->map_widget || target == container)
        {
          qiban_ui_capture_map_view_state(ui, true);
          qiban_ui_refresh_map_overlay(ui);
        }
    }
  else if (code == LV_EVENT_VALUE_CHANGED && ui->map_widget != NULL &&
           lv_event_get_target(event) == ui->map_widget)
    {
      qiban_ui_capture_map_view_state(ui, true);
      qiban_ui_refresh_map_overlay(ui);
    }
  else if (code == LV_EVENT_LONG_PRESSED &&
           qiban_ui_handle_map_long_press(ui, event))
    {
      lv_event_stop_bubbling(event);
    }
}

static void qiban_ui_map_pick_confirm_event_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  ui = g_qiban_ui;
  if (ui == NULL || !ui->map_pick_start_valid || !ui->map_pick_end_valid)
    {
      return;
    }

  if (qiban_ui_plan_local_route(ui, ui->map_pick_end_lon_e7,
                                ui->map_pick_end_lat_e7))
    {
      qiban_ui_set_map_pending(ui, "Offline route ready");
    }
  else
    {
      qiban_ui_set_map_pending(ui, "Offline route failed, pick again");
    }
}

static void qiban_ui_voice_preset_event_cb(lv_event_t *event)
{
  FAR const char *text;
  struct qiban_ui_s *ui;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  ui = g_qiban_ui;
  if (ui == NULL || ui->voice_textarea == NULL)
    {
      return;
    }

  text = (FAR const char *)lv_event_get_user_data(event);
  if (text == NULL)
    {
      return;
    }

  lv_textarea_set_text(ui->voice_textarea, text);
  lv_textarea_set_cursor_pos(ui->voice_textarea, LV_TEXTAREA_CURSOR_LAST);
  if (ui->voice_status_label != NULL)
    {
      ui->voice_status_manual = 1;
      lv_label_set_text(ui->voice_status_label, "正在启动导航...");
    }

  (void)qiban_ui_start_navigation(ui, text);
}

static void qiban_ui_voice_keyboard_event_cb(lv_event_t *event)
{
  lv_event_code_t code;
  struct qiban_ui_s *ui;

  code = lv_event_get_code(event);
  ui = g_qiban_ui;
  if (ui == NULL || ui->voice_textarea == NULL)
    {
      return;
    }

  if (code == LV_EVENT_CANCEL)
    {
      qiban_ui_set_voice_input_visible(ui, false);
      if (ui->voice_status_label != NULL)
        {
          ui->voice_status_manual = 1;
          lv_label_set_text(ui->voice_status_label, "已关闭键盘输入");
        }

      return;
    }

  if (code != LV_EVENT_READY)
    {
      return;
    }

  if (ui->voice_status_label != NULL)
    {
      ui->voice_status_manual = 1;
      lv_label_set_text(ui->voice_status_label, "正在解析输入...");
    }

  if (qiban_ui_start_navigation(ui, lv_textarea_get_text(ui->voice_textarea)) <
      0 && ui->voice_status_label != NULL)
    {
      ui->voice_status_manual = 1;
      lv_label_set_text(ui->voice_status_label, "请输入目的地");
    }
}

static void qiban_ui_voice_icon_event_cb(lv_event_t *event)
{
  FAR const char *action;
  struct qiban_ui_s *ui;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  ui = g_qiban_ui;
  if (ui == NULL)
    {
      return;
    }

  action = (FAR const char *)lv_event_get_user_data(event);
  if (action == NULL)
    {
      return;
    }

  if (strcmp(action, "keyboard") == 0)
    {
      qiban_ui_set_voice_input_visible(ui, !ui->voice_input_visible);
      if (ui->voice_status_label != NULL)
        {
          ui->voice_status_manual = 1;
          lv_label_set_text(ui->voice_status_label,
                            ui->voice_input_visible ?
                            "输入目的地后按确认" : "已关闭键盘输入");
        }

      if (ui->voice_input_visible && ui->voice_textarea != NULL)
        {
          lv_textarea_set_cursor_pos(ui->voice_textarea,
                                     LV_TEXTAREA_CURSOR_LAST);
        }
    }
  else if (strcmp(action, "record") == 0)
    {
      FAR char *argv[] =
      {
        "qiban_voice_service",
        "record-play",
        "4",
        NULL
      };

      if (qiban_ui_spawn_voice_service(argv) == OK)
        {
          if (ui->voice_status_label != NULL)
            {
              ui->voice_status_manual = 1;
              lv_label_set_text(ui->voice_status_label,
                                "正在录音，结束后自动回放一次");
            }
        }
      else if (ui->voice_status_label != NULL)
        {
          ui->voice_status_manual = 1;
          lv_label_set_text(ui->voice_status_label,
                            "语音服务启动失败");
        }

      qiban_ui_set_voice_input_visible(ui, false);
    }
}

static void qiban_ui_music_command_event_cb(lv_event_t *event)
{
  FAR const char *cmd;
  FAR char *argv[3];
  struct qiban_ui_s *ui;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  ui = g_qiban_ui;
  cmd = (FAR const char *)lv_event_get_user_data(event);
  if (ui == NULL || cmd == NULL)
    {
      return;
    }

  argv[0] = "qiban_music_service";
  argv[1] = (FAR char *)cmd;
  argv[2] = NULL;

  if (strcmp(cmd, "toggle") == 0)
    {
      argv[1] = ui->music_state.playing ? "pause" : "play";
    }

  if (qiban_ui_spawn_music_service(argv) == OK)
    {
      if (strcmp(argv[1], "play") == 0)
        {
          ui->music_state.playing = 1;
        }
      else if (strcmp(argv[1], "pause") == 0)
        {
          ui->music_state.playing = 0;
        }
      else if (strcmp(argv[1], "next") == 0 &&
               ui->music_state.track_count > 0)
        {
          ui->music_state.track_index =
            (ui->music_state.track_index + 1) % ui->music_state.track_count;
          ui->music_state.position_sec = 0;
        }
      else if (strcmp(argv[1], "prev") == 0 &&
               ui->music_state.track_count > 0)
        {
          ui->music_state.track_index =
            (ui->music_state.track_index + ui->music_state.track_count - 1) %
            ui->music_state.track_count;
          ui->music_state.position_sec = 0;
        }

      qiban_ui_refresh_music(ui);
    }
}

static void qiban_ui_music_source_event_cb(lv_event_t *event)
{
  FAR const char *source;
  FAR char *argv[4];

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  source = (FAR const char *)lv_event_get_user_data(event);
  if (source == NULL)
    {
      return;
    }

  argv[0] = "qiban_music_service";
  argv[1] = "source";
  argv[2] = (FAR char *)source;
  argv[3] = NULL;

  if (qiban_ui_spawn_music_service(argv) == OK)
    {
      snprintf(g_qiban_ui->music_state.source,
               sizeof(g_qiban_ui->music_state.source), "%s", source);
      g_qiban_ui->music_state.playing = 0;
      qiban_ui_refresh_music(g_qiban_ui);
    }
}

static void qiban_ui_update_page_indicator(struct qiban_ui_s *ui)
{
  if (ui == NULL)
    {
      return;
    }

  if (ui->active_page == 0)
    {
      lv_obj_add_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_remove_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "骑伴 AI");
      lv_label_set_text(ui->page_label, "01 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
  else if (ui->active_page == 1)
    {
      lv_obj_remove_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_remove_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "导航地图");
      lv_label_set_text(ui->page_label, "02 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
  else if (ui->active_page == 2)
    {
      lv_obj_remove_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_remove_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "语音导航");
      lv_label_set_text(ui->page_label, "03 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
  else if (ui->active_page == 3)
    {
      lv_obj_remove_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_remove_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "日期天气");
      lv_label_set_text(ui->page_label, "04 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
  else if (ui->active_page == 4)
    {
      lv_obj_remove_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_remove_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "音乐播放");
      lv_label_set_text(ui->page_label, "05 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
  else
    {
      lv_obj_remove_state(ui->prev_button, LV_STATE_DISABLED);
      lv_obj_add_state(ui->next_button, LV_STATE_DISABLED);
      lv_obj_clear_flag(ui->header_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui->page_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->header_label, "本地视频");
      lv_label_set_text(ui->page_label, "06 / 06");
      lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
      lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
    }
}

static void qiban_ui_set_page(struct qiban_ui_s *ui, int page,
                              lv_anim_enable_t anim_en)
{
  if (ui == NULL || ui->tileview == NULL)
    {
      return;
    }

  if (page < 0)
    {
      page = 0;
    }

  if (page > 5)
    {
      page = 5;
    }

  ui->active_page = page;
  lv_tileview_set_tile_by_index(ui->tileview, (uint32_t)page, 0, anim_en);
  qiban_ui_update_page_indicator(ui);
}

static void qiban_ui_prev_page_event_cb(lv_event_t *event)
{
  (void)event;

  if (g_qiban_ui == NULL)
    {
      return;
    }

  if (g_qiban_ui->active_page > 0)
    {
      qiban_ui_set_page(g_qiban_ui, g_qiban_ui->active_page - 1, LV_ANIM_ON);
    }
}

static void qiban_ui_next_page_event_cb(lv_event_t *event)
{
  (void)event;

  if (g_qiban_ui == NULL)
    {
      return;
    }

  if (g_qiban_ui->active_page < 5)
    {
      qiban_ui_set_page(g_qiban_ui, g_qiban_ui->active_page + 1, LV_ANIM_ON);
    }
}

static void qiban_ui_tileview_event_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui;
  lv_obj_t *active_tile;

  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
    {
      return;
    }

  ui = g_qiban_ui;
  if (ui == NULL || ui->tileview == NULL)
    {
      return;
    }

  active_tile = lv_tileview_get_tile_active(ui->tileview);
  if (active_tile == ui->map_tile)
    {
      ui->active_page = 1;
    }
  else if (active_tile == ui->voice_tile)
    {
      ui->active_page = 2;
    }
  else if (active_tile == ui->weather_tile)
    {
      ui->active_page = 3;
    }
  else if (active_tile == ui->music_tile)
    {
      ui->active_page = 4;
    }
  else if (active_tile == ui->video_tile)
    {
      ui->active_page = 5;
    }
  else
    {
      ui->active_page = 0;
    }
  qiban_ui_update_page_indicator(ui);

  if (ui->active_page == 1)
    {
      qiban_ui_refresh_map(ui);
      qiban_ui_refresh_location_marker(ui);
    }
}

static void qiban_ui_update_alerts(struct qiban_ui_s *ui)
{
  char alert_text[128];
  lv_color_t color;

  if (ui->state.alert_overspeed)
    {
      snprintf(alert_text, sizeof(alert_text), "超速提醒  请减速");
      color = lv_color_hex(0x991b1b);
    }
  else if (ui->state.alert_low_battery)
    {
      snprintf(alert_text, sizeof(alert_text), "电量偏低  请充电");
      color = lv_color_hex(0xb45309);
    }
  else if (ui->state.alert_fatigue)
    {
      snprintf(alert_text, sizeof(alert_text), "骑行较久  建议休息");
      color = lv_color_hex(0x0f766e);
    }
  else if (ui->map_local_route_active || ui->nav_state.active)
    {
      snprintf(alert_text, sizeof(alert_text), "Navigating  %s",
               ui->map_local_route_active ? "Local route" :
               (ui->nav_state.destination[0] != '\0' ?
                ui->nav_state.destination : "Route active"));
      color = lv_color_hex(0x1d4ed8);
    }
  else
    {
      snprintf(alert_text, sizeof(alert_text), "状态稳定");
      color = lv_color_hex(0x166534);
    }

  lv_obj_set_style_bg_color(ui->alert_panel, color, 0);
  lv_label_set_text(ui->alert_label, alert_text);
}

static void qiban_ui_build_map_status(struct qiban_ui_s *ui,
                                      char *buffer, size_t buffer_size)
{
  char turn_distance[24];
  char remaining_distance[24];
  uint32_t traveled_distance_m;
  uint32_t remaining_distance_m;
  lv_offline_nav_guidance_t guidance;

  if (ui->map_local_route_active && ui->map_widget != NULL)
    {
      traveled_distance_m =
        lv_offline_map_track_get_traveled_distance_m(ui->map_widget);
      remaining_distance_m =
        lv_offline_map_track_get_remaining_distance_m(ui->map_widget);
      if (lv_offline_nav_get_guidance(&g_qiban_nav_route, traveled_distance_m,
                                      &guidance))
        {
          qiban_ui_format_distance(remaining_distance, sizeof(remaining_distance),
                                   guidance.remaining_distance_m);
          if (guidance.turn_type != LV_OFFLINE_NAV_TURN_NONE &&
              guidance.distance_to_turn_m > 0U)
            {
              qiban_ui_format_distance(turn_distance, sizeof(turn_distance),
                                       guidance.distance_to_turn_m);
              snprintf(buffer, buffer_size, "In %s %s  %s left",
                       turn_distance,
                       qiban_ui_nav_turn_text(guidance.turn_type),
                       remaining_distance);
            }
          else if (guidance.remaining_distance_m == 0U &&
                   guidance.total_distance_m > 0U)
            {
              snprintf(buffer, buffer_size, "%s", "Arrived");
            }
          else
            {
              snprintf(buffer, buffer_size, "Offline route  %s left",
                       remaining_distance);
            }

          return;
        }

      if (remaining_distance_m > 0U)
        {
          qiban_ui_format_distance(remaining_distance,
                                   sizeof(remaining_distance),
                                   remaining_distance_m);
          snprintf(buffer, buffer_size, "Offline route  %s left",
                   remaining_distance);
        }
      else
        {
          snprintf(buffer, buffer_size, "%s", "Offline route ready");
        }

      return;
    }

  if (ui->nav_state.active)
    {
      snprintf(buffer, buffer_size, "%s  %s  %s  %d min",
               ui->nav_state.destination[0] != '\0' ?
               ui->nav_state.destination : "Navigating",
               ui->nav_state.status[0] != '\0' ?
               ui->nav_state.status : "Route active",
               ui->nav_state.next_turn[0] != '\0' ?
               ui->nav_state.next_turn : "Waiting for next turn",
               ui->nav_state.eta_minutes);
      return;
    }

  if (ui->map_pick_start_valid && ui->map_pick_end_valid)
    {
      snprintf(buffer, buffer_size, "%s", "Route points selected, tap confirm");
      return;
    }

  if (ui->map_pick_start_valid)
    {
      snprintf(buffer, buffer_size, "%s", "Start selected, pick destination");
      return;
    }

  if (ui->map_state.ready)
    {
      snprintf(buffer, buffer_size, "%s",
               ui->map_state.title[0] != '\0' ? ui->map_state.title : "Map");
    }
  else
    {
      snprintf(buffer, buffer_size, "%s",
               ui->map_state.status[0] != '\0' ?
               ui->map_state.status : "Waiting for map");
    }
}

static void qiban_ui_refresh_map(struct qiban_ui_s *ui)
{
  char map_status[160];

  if (ui == NULL)
    {
      return;
    }

  qiban_ui_build_map_status(ui, map_status, sizeof(map_status));

  if (ui->map_widget != NULL)
    {
      qiban_ui_sync_offline_map_view(ui);
      qiban_ui_refresh_map_overlay(ui);
      lv_obj_clear_flag(ui->map_page_status_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->map_page_status_label, map_status);
    }
  else
    {
      lv_obj_clear_flag(ui->map_page_status_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui->map_page_status_label, "Offline map init failed");
    }
}

static void qiban_ui_sync_offline_map_view(struct qiban_ui_s *ui)
{
  int center_lon_e7;
  int center_lat_e7;
  int zoom;

  if (ui == NULL || ui->map_widget == NULL)
    {
      return;
    }

  if (ui->map_local_route_active)
    {
      return;
    }

  qiban_ui_get_effective_map_center(ui, &center_lon_e7, &center_lat_e7);
  zoom = qiban_ui_effective_map_zoom(ui);
  if (center_lon_e7 == ui->map_view_center_lon_e7 &&
      center_lat_e7 == ui->map_view_center_lat_e7 &&
      zoom == ui->map_view_zoom)
    {
      return;
    }

  if (lv_offline_map_center_lonlat_e7(ui->map_widget, center_lon_e7,
                                      center_lat_e7, zoom))
    {
      ui->map_view_center_lon_e7 = center_lon_e7;
      ui->map_view_center_lat_e7 = center_lat_e7;
      ui->map_view_zoom = zoom;
    }
}

static void qiban_ui_refresh_map_overlay(struct qiban_ui_s *ui)
{
  if (ui == NULL || ui->map_widget == NULL)
    {
      return;
    }

  if (ui->map_route_line != NULL)
    {
      lv_obj_add_flag(ui->map_route_line, LV_OBJ_FLAG_HIDDEN);
    }

  qiban_ui_sync_offline_route(ui);
  qiban_ui_refresh_offline_guidance(ui);
  qiban_ui_map_update_pick_overlay(ui);
}

static void qiban_ui_sync_offline_route(struct qiban_ui_s *ui)
{
  if (ui == NULL || ui->map_widget == NULL)
    {
      return;
    }

  if (ui->map_local_route_active)
    {
      return;
    }

  /* Only keep fully local routes rendered on the offline map widget.
   * External nav/map service state is still shown in labels, but the
   * two-point track overlay is disabled for crash isolation.
   */

  if (ui->map_route_active)
    {
      lv_offline_map_track_clear(ui->map_widget);
      qiban_ui_clear_route_cache(ui);
    }
}

static void qiban_ui_refresh_location_marker(struct qiban_ui_s *ui)
{
  int lon_e7;
  int lat_e7;
  int zoom;
  lv_offline_map_location_result_t result;

  if (ui == NULL || ui->map_widget == NULL)
    {
      return;
    }

  if (ui->location_marker != NULL)
    {
      lv_obj_add_flag(ui->location_marker, LV_OBJ_FLAG_HIDDEN);
    }

  if (ui->location_label != NULL)
    {
      lv_obj_add_flag(ui->location_label, LV_OBJ_FLAG_HIDDEN);
    }

  if (!ui->location_state.valid)
    {
      return;
    }

  lon_e7 = qiban_ui_coord_to_e7(ui->location_state.longitude);
  lat_e7 = qiban_ui_coord_to_e7(ui->location_state.latitude);
  zoom = qiban_ui_effective_map_zoom(ui);
  memset(&result, 0, sizeof(result));
  if (ui->map_route_active &&
      lv_offline_map_track_get_point_count(ui->map_widget) >= 2 &&
      lv_offline_map_track_update_location_e7(ui->map_widget, lon_e7, lat_e7,
                                              zoom, &result))
    {
      return;
    }

  lv_offline_map_set_vehicle_location_e7(ui->map_widget, lon_e7, lat_e7,
                                         zoom, false);
}

static void qiban_ui_map_update_pick_overlay(struct qiban_ui_s *ui)
{
  lv_area_t frame_area;
  lv_area_t map_area;
  lv_coord_t view_x;
  lv_coord_t view_y;
  int map_width;
  int map_height;

  if (ui == NULL || ui->map_frame == NULL)
    {
      return;
    }

  if (ui->map_pick_start_marker != NULL)
    {
      lv_obj_add_flag(ui->map_pick_start_marker, LV_OBJ_FLAG_HIDDEN);
    }

  if (ui->map_pick_marker != NULL)
    {
      lv_obj_add_flag(ui->map_pick_marker, LV_OBJ_FLAG_HIDDEN);
    }

  if (ui->map_pick_confirm_button != NULL)
    {
      if (ui->map_pick_start_valid && ui->map_pick_end_valid)
        {
          lv_obj_clear_flag(ui->map_pick_confirm_button, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_foreground(ui->map_pick_confirm_button);
        }
      else
        {
          lv_obj_add_flag(ui->map_pick_confirm_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (ui->map_widget == NULL)
    {
      return;
    }

  lv_obj_get_coords(ui->map_frame, &frame_area);
  lv_obj_get_coords(ui->map_widget, &map_area);
  map_width = lv_obj_get_width(ui->map_widget);
  map_height = lv_obj_get_height(ui->map_widget);

  if (ui->map_pick_start_valid && ui->map_pick_start_marker != NULL &&
      lv_offline_map_lonlat_e7_to_view_point(ui->map_widget,
                                             ui->map_pick_start_lon_e7,
                                             ui->map_pick_start_lat_e7,
                                             &view_x, &view_y) &&
      view_x >= 0 && view_y >= 0 &&
      view_x < map_width && view_y < map_height)
    {
      lv_obj_set_pos(ui->map_pick_start_marker,
                     map_area.x1 - frame_area.x1 + view_x -
                     QIBAN_MAP_PICK_MARKER_SIZE / 2,
                     map_area.y1 - frame_area.y1 + view_y -
                     QIBAN_MAP_PICK_MARKER_SIZE / 2);
      lv_obj_clear_flag(ui->map_pick_start_marker, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(ui->map_pick_start_marker);
    }

  if (ui->map_pick_end_valid && ui->map_pick_marker != NULL &&
      lv_offline_map_lonlat_e7_to_view_point(ui->map_widget,
                                             ui->map_pick_end_lon_e7,
                                             ui->map_pick_end_lat_e7,
                                             &view_x, &view_y) &&
      view_x >= 0 && view_y >= 0 &&
      view_x < map_width && view_y < map_height)
    {
      lv_obj_set_pos(ui->map_pick_marker,
                     map_area.x1 - frame_area.x1 + view_x -
                     QIBAN_MAP_PICK_MARKER_SIZE / 2,
                     map_area.y1 - frame_area.y1 + view_y -
                     QIBAN_MAP_PICK_MARKER_SIZE / 2);
      lv_obj_clear_flag(ui->map_pick_marker, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(ui->map_pick_marker);
    }
}

static void qiban_ui_refresh_offline_guidance(struct qiban_ui_s *ui)
{
  char turn_text[24];
  char remaining_text[24];
  uint32_t traveled_distance_m;
  lv_offline_nav_guidance_t guidance;

  if (ui == NULL || ui->map_widget == NULL || !ui->map_local_route_active)
    {
      return;
    }

  traveled_distance_m =
    lv_offline_map_track_get_traveled_distance_m(ui->map_widget);
  if (!lv_offline_nav_get_guidance(&g_qiban_nav_route, traveled_distance_m,
                                   &guidance))
    {
      return;
    }

  qiban_ui_format_distance(remaining_text, sizeof(remaining_text),
                           guidance.remaining_distance_m);
  if (guidance.turn_type != LV_OFFLINE_NAV_TURN_NONE &&
      guidance.distance_to_turn_m > 0U)
    {
      qiban_ui_format_distance(turn_text, sizeof(turn_text),
                               guidance.distance_to_turn_m);
      lv_label_set_text_fmt(ui->map_page_status_label, "In %s %s  %s left",
                            turn_text,
                            qiban_ui_nav_turn_text(guidance.turn_type),
                            remaining_text);
    }
  else if (guidance.remaining_distance_m == 0U &&
           guidance.total_distance_m > 0U)
    {
      lv_label_set_text(ui->map_page_status_label, "Arrived");
    }
  else
    {
      lv_label_set_text_fmt(ui->map_page_status_label, "Offline route  %s left",
                            remaining_text);
    }
}

static void qiban_ui_refresh_labels(struct qiban_ui_s *ui)
{
  int nav_remaining_m;

  nav_remaining_m = ui->map_local_route_active && ui->map_widget != NULL ?
                    (int)lv_offline_map_track_get_remaining_distance_m(
                      ui->map_widget) :
                    (ui->nav_state.active ?
                     ui->nav_state.remaining_distance_m :
                     ui->state.nav_remaining_m);

  lv_label_set_text_fmt(ui->source_label, "%s", ui->state.source);
  lv_label_set_text_fmt(ui->speed_label, "%d km/h", ui->state.speed_kmh);
  lv_label_set_text_fmt(ui->battery_label, "电量    %d%%",
                        ui->state.battery_percent);
  lv_label_set_text_fmt(ui->range_label, "续航    %d km",
                        ui->state.remaining_range_km);
  lv_label_set_text_fmt(ui->ride_label, "骑行    %d min",
                        ui->state.ride_duration_min);
  lv_label_set_text_fmt(ui->distance_label, "里程    %d.%d km",
                        ui->state.total_distance_km_x10 / 10,
                        ui->state.total_distance_km_x10 % 10);
  lv_label_set_text_fmt(ui->nav_label, "导航    %d m",
                        nav_remaining_m);
  qiban_ui_update_alerts(ui);
  qiban_ui_refresh_map(ui);
  qiban_ui_refresh_location_marker(ui);
  qiban_ui_refresh_voice(ui);
  qiban_ui_refresh_weather(ui);
  qiban_ui_refresh_music(ui);
  qiban_ui_refresh_video(ui);
}

static void qiban_ui_refresh_weather(struct qiban_ui_s *ui)
{
  struct timespec ts;
  struct tm tm_info;
  struct tm update_tm;
  char date_buf[32];
  char time_buf[16];
  FAR const char *condition_text;

  /* Update date and time from system clock */

  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm_info);

  snprintf(date_buf, sizeof(date_buf),
           "%04d\xe5\xb9\xb4%02d\xe6\x9c\x88%02d\xe6\x97\xa5",
           tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday);
  /* "YYYY年MM月DD日" */

  snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
           tm_info.tm_hour, tm_info.tm_min);

  lv_label_set_text(ui->weather_date_label, date_buf);
  lv_label_set_text(ui->weather_time_label, time_buf);

  /* Update weather info */

  if (ui->weather_state.valid)
    {
      char temp_buf[16];

      snprintf(temp_buf, sizeof(temp_buf), "%d°C",
               ui->weather_state.temperature_c);
      lv_label_set_text(ui->weather_temp_label, temp_buf);

      condition_text = ui->weather_state.condition;
      if (strcmp(condition_text, "clear") == 0 ||
          strcmp(condition_text, "sunny") == 0)
        {
          condition_text = "晴朗";
        }
      else if (strcmp(condition_text, "cloudy") == 0 ||
               strcmp(condition_text, "cloud") == 0)
        {
          condition_text = "多云";
        }
      else if (strcmp(condition_text, "rain") == 0 ||
               strcmp(condition_text, "rainy") == 0)
        {
          condition_text = "有雨";
        }
      else if (strcmp(condition_text, "snow") == 0 ||
               strcmp(condition_text, "snowy") == 0)
        {
          condition_text = "降雪";
        }

      if (ui->weather_state.city[0] != '\0')
        {
          lv_label_set_text_fmt(ui->weather_cond_label, "%s  %s",
                                ui->weather_state.city, condition_text);
        }
      else
        {
          lv_label_set_text(ui->weather_cond_label, condition_text);
        }

      /* Show detail: humidity + wind */

      char detail_buf[64];
      snprintf(detail_buf, sizeof(detail_buf),
               "\xe6\xb9\xbf\xe5\xba\xa6 %d%%  \xe9\xa3\x8e\xe9\x80\x9f"
               " %dkm/h",
               ui->weather_state.humidity,
               ui->weather_state.wind_speed_kmh);
      /* "湿度 X%  风速 Ykm/h" */
      lv_label_set_text(ui->weather_detail_label, detail_buf);

      if (ui->weather_source_label != NULL)
        {
          if (ui->weather_state.updated_at > 0)
            {
              time_t updated_at;

              updated_at = (time_t)ui->weather_state.updated_at;
              localtime_r(&updated_at, &update_tm);
              lv_label_set_text_fmt(ui->weather_source_label,
                                    "来源 %s  更新 %02d:%02d",
                                    ui->weather_state.source,
                                    update_tm.tm_hour,
                                    update_tm.tm_min);
            }
          else
            {
              lv_label_set_text_fmt(ui->weather_source_label,
                                    "来源 %s  等待实时更新",
                                    ui->weather_state.source);
            }
        }

      /* Update icon based on condition */

      if (strstr(ui->weather_state.condition, "rain") != NULL)
        {
          lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_DOWN);
          lv_obj_set_style_text_color(ui->weather_icon_label,
                                      lv_color_hex(0x60a5fa), 0);
        }
      else if (strstr(ui->weather_state.condition, "snow") != NULL)
        {
          lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_SHUFFLE);
          lv_obj_set_style_text_color(ui->weather_icon_label,
                                      lv_color_hex(0xe2e8f0), 0);
        }
      else if (strstr(ui->weather_state.condition, "cloud") != NULL)
        {
          lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_IMAGE);
          lv_obj_set_style_text_color(ui->weather_icon_label,
                                      lv_color_hex(0x94a3b8), 0);
        }
      else
        {
          lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_REFRESH);
          lv_obj_set_style_text_color(ui->weather_icon_label,
                                      lv_color_hex(0xfbbf24), 0);
        }
    }
  else
    {
      lv_label_set_text(ui->weather_temp_label, "--°C");
      lv_label_set_text(ui->weather_cond_label, "\xe6\x9a\x82\xe6\x97\xa0"
                        "\xe5\xa4\xa9\xe6\xb0\x94\xe6\x95\xb0\xe6\x8d\xae");
      lv_label_set_text(ui->weather_detail_label, "");
      if (ui->weather_source_label != NULL)
        {
          lv_label_set_text(ui->weather_source_label,
                            "未读取到 /data/qiban_weather.json");
        }
      lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_REFRESH);
      lv_obj_set_style_text_color(ui->weather_icon_label,
                                  lv_color_hex(0xfbbf24), 0);
    }
}

static FAR const char *qiban_ui_music_status_text(FAR const char *status)
{
  if (status == NULL || status[0] == '\0' ||
      strcmp(status, "unknown") == 0)
    {
      return "状态未知";
    }

  if (strcmp(status, "ready") == 0)
    {
      return "已就绪";
    }

  if (strcmp(status, "no_media") == 0)
    {
      return "未发现音乐";
    }

  if (strcmp(status, "connected") == 0)
    {
      return "已连接";
    }

  if (strcmp(status, "not_connected") == 0)
    {
      return "未连接";
    }

  if (strcmp(status, "stream_unreachable") == 0)
    {
      return "音乐流不可达";
    }

  if (strcmp(status, "not_ready") == 0)
    {
      return "未就绪";
    }

  return status;
}

static FAR const char *qiban_ui_voice_status_text(FAR const char *status)
{
  if (status == NULL || status[0] == '\0')
    {
      return "语音服务待命中";
    }

  if (strcmp(status, "record_request_prepared") == 0)
    {
      return "录音命令已准备，请等待识别";
    }

  if (strcmp(status, "recording") == 0)
    {
      return "正在录音";
    }

  if (strcmp(status, "playback_started") == 0)
    {
      return "录音完成，正在回放";
    }

  if (strcmp(status, "record_playback_done") == 0)
    {
      return "录音回放完成";
    }

  if (strcmp(status, "record_failed") == 0)
    {
      return "录音失败";
    }

  if (strcmp(status, "playback_failed") == 0)
    {
      return "录音回放失败";
    }

  if (strcmp(status, "intent_forwarded") == 0 ||
      strcmp(status, "navigation_forwarded_announce_queued") == 0)
    {
      return "语音指令已执行";
    }

  if (strcmp(status, "intent_not_supported") == 0)
    {
      return "未识别到支持的语音指令";
    }

  if (strcmp(status, "intent_forward_failed") == 0 ||
      strcmp(status, "navigation_failed") == 0)
    {
      return "语音指令转发失败";
    }

  if (strcmp(status, "pending") == 0)
    {
      return "云端语音任务处理中";
    }

  if (strcmp(status, "done") == 0)
    {
      return "云端语音任务已完成";
    }

  if (strcmp(status, "failed") == 0)
    {
      return "云端语音任务失败";
    }

  return status;
}

static void qiban_ui_refresh_voice(struct qiban_ui_s *ui)
{
  char text[192];
  FAR const char *main_text;

  if (ui == NULL || ui->voice_status_label == NULL ||
      ui->voice_input_visible || ui->voice_status_manual)
    {
      return;
    }

  if (!ui->voice_state.valid)
    {
      lv_label_set_text(ui->voice_status_label,
                        "点击键盘输入目的地，或点击麦克风准备录音");
      return;
    }

  main_text = ui->voice_state.recognized_text[0] != '\0'
            ? ui->voice_state.recognized_text
            : ui->voice_state.intent_text;

  if (main_text[0] != '\0' && ui->voice_state.target[0] != '\0')
    {
      snprintf(text, sizeof(text), "%s：%s -> %s",
               qiban_ui_voice_status_text(ui->voice_state.status),
               main_text,
               ui->voice_state.target);
    }
  else if (main_text[0] != '\0')
    {
      snprintf(text, sizeof(text), "%s：%s",
               qiban_ui_voice_status_text(ui->voice_state.status),
               main_text);
    }
  else
    {
      snprintf(text, sizeof(text), "%s",
               qiban_ui_voice_status_text(ui->voice_state.status));
    }

  lv_label_set_text(ui->voice_status_label, text);
}

static void qiban_ui_refresh_music(struct qiban_ui_s *ui)
{
  struct qiban_music_state_s *m = &ui->music_state;

  if (m->title[0] != '\0')
    {
      lv_label_set_text(ui->music_title_label, m->title);
    }
  else
    {
      lv_label_set_text(ui->music_title_label, "未在播放");
    }

  if (m->artist[0] != '\0')
    {
      lv_label_set_text(ui->music_artist_label, m->artist);
    }
  else
    {
      lv_label_set_text(ui->music_artist_label, "未知艺术家");
    }

  /* Source indicator */

  if (strcmp(m->source, "bluetooth") == 0)
    {
      lv_label_set_text(ui->music_source_label,
                        LV_SYMBOL_BLUETOOTH " 蓝牙");
    }
  else if (strcmp(m->source, "wifi") == 0)
    {
      lv_label_set_text(ui->music_source_label,
                        LV_SYMBOL_WIFI " WiFi");
    }
  else
    {
      lv_label_set_text(ui->music_source_label,
                        LV_SYMBOL_SD_CARD " SD卡");
    }

  /* Play/pause button */

  if (m->playing)
    {
      lv_label_set_text(ui->music_play_btn_label, LV_SYMBOL_PAUSE);
    }
  else
    {
      lv_label_set_text(ui->music_play_btn_label, LV_SYMBOL_PLAY);
    }

  /* Progress bar */

  if (m->duration_sec > 0)
    {
      int pct = (m->position_sec * 100) / m->duration_sec;
      if (pct > 100)
        {
          pct = 100;
        }

      lv_bar_set_value(ui->music_progress_bar, pct, LV_ANIM_ON);
    }
  else
    {
      lv_bar_set_value(ui->music_progress_bar, 0, LV_ANIM_OFF);
    }

  /* Time display */

  {
    int pos_min = m->position_sec / 60;
    int pos_sec = m->position_sec % 60;
    int dur_min = m->duration_sec / 60;
    int dur_sec = m->duration_sec % 60;
    char time_buf[32];

    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d",
             pos_min, pos_sec, dur_min, dur_sec);
    lv_label_set_text(ui->music_time_label, time_buf);
  }

  /* Volume */

  {
    char vol_buf[24];
    snprintf(vol_buf, sizeof(vol_buf), "%s %d%%",
             LV_SYMBOL_VOLUME_MAX, m->volume);
    lv_label_set_text(ui->music_vol_label, vol_buf);
  }

  /* Track info */

  if (m->track_count > 0)
    {
      char track_buf[24];
      snprintf(track_buf, sizeof(track_buf), "%d / %d",
               m->track_index + 1, m->track_count);
      lv_label_set_text(ui->music_track_label, track_buf);
    }
  else
    {
      lv_label_set_text(ui->music_track_label, "- / -");
    }

  /* Highlight active source button */

  lv_obj_set_style_bg_color(ui->music_src_sdcard_btn,
    lv_color_hex(strcmp(m->source, "sdcard") == 0 ? 0x1e40af : 0x1e293b), 0);
  lv_obj_set_style_bg_color(ui->music_src_wifi_btn,
    lv_color_hex(strcmp(m->source, "wifi") == 0 ? 0x1e40af : 0x1e293b), 0);
  lv_obj_set_style_bg_color(ui->music_src_bt_btn,
    lv_color_hex(strcmp(m->source, "bluetooth") == 0 ? 0x1e40af : 0x1e293b), 0);

  if (ui->music_conn_label != NULL)
    {
      lv_label_set_text_fmt(ui->music_conn_label,
                            "SD卡 %s(%d首)  WiFi %s  蓝牙 %s",
                            m->sdcard_available ? "可用" : "未插入",
                            m->track_count,
                            m->wifi_connected ? "已连接" : "未连接",
                            m->bluetooth_connected ? "已连接" :
                            "未就绪");
    }

  if (m->source_status[0] != '\0')
    {
      lv_label_set_text_fmt(ui->music_artist_label, "%s  %s",
                            m->artist[0] != '\0' ? m->artist : "未知艺术家",
                            qiban_ui_music_status_text(m->source_status));
    }
}

/****************************************************************************
 * Video browser event callbacks
 ****************************************************************************/

static void video_btn_up_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui = (struct qiban_ui_s *)lv_event_get_user_data(event);
  if (ui == NULL) return;
  struct qiban_video_state_s *v = &ui->video_state;
  if (v->selected > 0)
    {
      v->selected--;
      video_browser_scan(v, v->cwd);
    }
}

static void video_btn_down_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui = (struct qiban_ui_s *)lv_event_get_user_data(event);
  if (ui == NULL) return;
  struct qiban_video_state_s *v = &ui->video_state;
  if (v->selected < v->entry_count - 1)
    {
      v->selected++;
      video_browser_scan(v, v->cwd);
    }
}

static void video_btn_play_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui = (struct qiban_ui_s *)lv_event_get_user_data(event);
  if (ui == NULL) return;
  struct qiban_video_state_s *v = &ui->video_state;

  if (v->selected < 0 || v->selected >= v->entry_count)
    {
      return;
    }

  if (v->entries[v->selected].is_dir)
    {
      /* Enter directory */

      video_browser_scan(v, v->entries[v->selected].path);
    }
  else if (strstr(v->entries[v->selected].name, ".mp4") ||
           strstr(v->entries[v->selected].name, ".mkv") ||
           strstr(v->entries[v->selected].name, ".avi") ||
           strstr(v->entries[v->selected].name, ".flv") ||
           strstr(v->entries[v->selected].name, ".ts"))
    {
      /* Play video via system() call to video service */

      char cmd[256];
      snprintf(cmd, sizeof(cmd), "qiban_video_service play '%s' &",
               v->entries[v->selected].path);
      system(cmd);
      snprintf(v->current_name, sizeof(v->current_name),
               "%s", v->entries[v->selected].name);
      snprintf(v->state, sizeof(v->state), "playing");
    }
}

static void video_btn_back_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui = (struct qiban_ui_s *)lv_event_get_user_data(event);
  if (ui == NULL) return;
  struct qiban_video_state_s *v = &ui->video_state;

  /* Go to parent directory */

  FAR char *slash = strrchr(v->cwd, '/');
  if (slash != NULL && slash != v->cwd)
    {
      *slash = '\0';
    }
  else
    {
      snprintf(v->cwd, sizeof(v->cwd), "/");
    }

  v->selected = 0;
  video_browser_scan(v, v->cwd);
}

static void video_browser_item_cb(lv_event_t *event)
{
  struct qiban_ui_s *ui = (struct qiban_ui_s *)lv_event_get_user_data(event);
  if (ui == NULL) return;
  struct qiban_video_state_s *v = &ui->video_state;

  lv_obj_t *btn = lv_event_get_target(event);
  lv_obj_t *list = lv_obj_get_parent(btn);

  /* Find index of clicked item */

  int idx = 0;
  uint32_t child_count = lv_obj_get_child_count(list);
  for (uint32_t i = 0; i < child_count; i++)
    {
      if (lv_obj_get_child(list, i) == btn)
        {
          idx = (int)i;
          break;
        }
    }

  v->selected = idx;

  if (idx >= 0 && idx < v->entry_count)
    {
      if (v->entries[idx].is_dir)
        {
          video_browser_scan(v, v->entries[idx].path);
        }
      else
        {
          video_btn_play_cb(event);
        }
    }
}

static void qiban_ui_refresh_video(struct qiban_ui_s *ui)
{
  struct qiban_video_state_s *v = &ui->video_state;

  /* State indicator */

  if (strcmp(v->state, "playing") == 0)
    {
      lv_label_set_text(ui->video_state_label,
                        LV_SYMBOL_PLAY " \xe6\x92\xad\xe6\x94\xbe\xe4\xb8\xad");
      /* "播放中" */
      lv_obj_set_style_text_color(ui->video_state_label,
                                  lv_color_hex(0x22c55e), 0);
    }
  else if (strcmp(v->state, "paused") == 0)
    {
      lv_label_set_text(ui->video_state_label,
                        LV_SYMBOL_PAUSE " \xe5\xb7\xb2\xe6\x9a\x82\xe5\x81\x9c");
      /* "已暂停" */
      lv_obj_set_style_text_color(ui->video_state_label,
                                  lv_color_hex(0xfbbf24), 0);
    }
  else
    {
      lv_label_set_text(ui->video_state_label,
                        LV_SYMBOL_STOP " \xe5\xb7\xb2\xe5\x81\x9c\xe6\xad\xa2");
      /* "已停止" */
      lv_obj_set_style_text_color(ui->video_state_label,
                                  lv_color_hex(0x94a3b8), 0);
    }

  /* Current file name */

  if (v->current_name[0] != '\0')
    {
      lv_label_set_text(ui->video_name_label, v->current_name);
      lv_obj_set_style_text_color(ui->video_name_label,
                                  lv_color_hex(0xf8fafc), 0);
    }
  else
    {
      lv_label_set_text(ui->video_name_label,
                        "\xe6\x9c\xaa\xe9\x80\x89\xe6\x8b\xa9\xe8\xa7\x86\xe9\xa2\x91");
      /* "未选择视频" */
    }

  /* Progress bar */

  if (v->duration_sec > 0)
    {
      int pct = (v->position_sec * 100) / v->duration_sec;
      if (pct > 100) pct = 100;
      lv_bar_set_value(ui->video_progress_bar, pct, LV_ANIM_ON);
    }
  else
    {
      lv_bar_set_value(ui->video_progress_bar, 0, LV_ANIM_OFF);
    }

  /* Time display */

  {
    int pos_min = v->position_sec / 60;
    int pos_sec = v->position_sec % 60;
    int dur_min = v->duration_sec / 60;
    int dur_sec = v->duration_sec % 60;
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d",
             pos_min, pos_sec, dur_min, dur_sec);
    lv_label_set_text(ui->video_time_label, time_buf);
  }

  /* Path label */

  lv_label_set_text(ui->video_path_label, v->cwd);

  /* Browser list - refresh every 5 seconds */

  static int last_browser_refresh = 0;
  if (ui->tick - last_browser_refresh >= 5 || last_browser_refresh == 0)
    {
      video_browser_scan(v, v->cwd);
      last_browser_refresh = ui->tick;

      /* Clear and rebuild list */

      lv_obj_clean(ui->video_browser_list);

      for (int i = 0; i < v->entry_count; i++)
        {
          lv_obj_t *item = lv_list_add_btn(ui->video_browser_list,
            v->entries[i].is_dir ? LV_SYMBOL_DIRECTORY :
            (strstr(v->entries[i].name, ".mp4") ||
             strstr(v->entries[i].name, ".mkv") ||
             strstr(v->entries[i].name, ".avi") ||
             strstr(v->entries[i].name, ".flv"))
            ? LV_SYMBOL_IMAGE : LV_SYMBOL_FILE,
            v->entries[i].name);

          lv_obj_set_style_text_font(item, QIBAN_CJK_FONT, 0);

          /* Highlight selected */

          if (i == v->selected)
            {
              lv_obj_set_style_bg_color(item, lv_color_hex(0x1e40af), 0);
              lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
            }

          /* Color directories differently */

          if (v->entries[i].is_dir)
            {
              lv_obj_t *label = lv_obj_get_child(item, 1);
              if (label)
                {
                  lv_obj_set_style_text_color(label,
                    lv_color_hex(0x38bdf8), 0);
                }
            }
        }

      /* Selected item is highlighted above. */
    }

  /* Error message */

  if (v->error_msg[0] != '\0')
    {
      lv_label_set_text(ui->video_name_label, v->error_msg);
      lv_obj_set_style_text_color(ui->video_name_label,
                                  lv_color_hex(0xef4444), 0);
    }
}

static void qiban_ui_pinch_timer_cb(lv_timer_t *timer)
{
  struct qiban_ui_s *ui;
  struct qiban_touch_pinch_s pinch;
  lv_obj_t *container;
  int32_t distance;
  int32_t mid_x;
  int32_t mid_y;
  int32_t d_distance;
  int32_t d_mid_x;
  int32_t d_mid_y;
  int zoom;

  ui = (struct qiban_ui_s *)lv_timer_get_user_data(timer);
  if (ui == NULL)
    {
      return;
    }

  if (ui->active_page != 1 || ui->map_widget == NULL ||
      ui->touch_pinch_fd < 0)
    {
      if (ui->touch_pinch_active)
        {
          printf("[qiban-debug] pinch: reset (active_page=%d "
                "map_widget=%p fd=%d)\n",
                ui->active_page, (void *)ui->map_widget,
                ui->touch_pinch_fd);
        }
      ui->touch_pinch_active = false;
      return;
    }

  if (ioctl(ui->touch_pinch_fd, QIBAN_TOUCH_PINCH_IOC, (unsigned long)&pinch) < 0)
    {
      printf("[qiban-debug] pinch: ioctl failed errno=%d\n", errno);
      ui->touch_pinch_active = false;
      return;
    }

  if (pinch.npoints < 2)
    {
      if (ui->touch_pinch_active)
        {
          printf("[qiban-debug] pinch: dropped below 2 points "
                "(npoints=%d)\n", pinch.npoints);
        }
      ui->touch_pinch_active = false;
      return;
    }

  distance = (int32_t)sqrt((double)(pinch.x1 - pinch.x0) *
                           (double)(pinch.x1 - pinch.x0) +
                           (double)(pinch.y1 - pinch.y0) *
                           (double)(pinch.y1 - pinch.y0));
  mid_x = (pinch.x0 + pinch.x1) / 2;
  mid_y = (pinch.y0 + pinch.y1) / 2;

  if (!ui->touch_pinch_active)
    {
      printf("[qiban-debug] pinch: gesture start p0=(%d,%d) p1=(%d,%d) "
            "distance=%d\n", pinch.x0, pinch.y0, pinch.x1, pinch.y1,
            (int)distance);
      ui->touch_pinch_active = true;
      ui->touch_pinch_last_distance = distance;
      ui->touch_pinch_last_mid_x = mid_x;
      ui->touch_pinch_last_mid_y = mid_y;
      ui->touch_pinch_zoom_accum = 0;
      return;
    }

  d_mid_x = mid_x - ui->touch_pinch_last_mid_x;
  d_mid_y = mid_y - ui->touch_pinch_last_mid_y;
  d_distance = distance - ui->touch_pinch_last_distance;

  printf("[qiban-debug] pinch: distance=%d d_distance=%d "
        "d_mid=(%d,%d) accum=%d\n", (int)distance, (int)d_distance,
        (int)d_mid_x, (int)d_mid_y, (int)ui->touch_pinch_zoom_accum);

  container = lv_offline_map_get_container(ui->map_widget);
  if (container != NULL && (d_mid_x != 0 || d_mid_y != 0))
    {
      lv_obj_scroll_by(container, d_mid_x, d_mid_y, LV_ANIM_OFF);
      qiban_ui_capture_map_view_state(ui, false);
    }

  ui->touch_pinch_zoom_accum += d_distance;
  if (ui->touch_pinch_zoom_accum >= QIBAN_PINCH_ZOOM_STEP_PX)
    {
      zoom = lv_offline_map_get_zoom(ui->map_widget);
      lv_offline_map_set_zoom(ui->map_widget, zoom + 1);
      ui->touch_pinch_zoom_accum -= QIBAN_PINCH_ZOOM_STEP_PX;
      qiban_ui_capture_map_view_state(ui, true);
      qiban_ui_set_map_pending(ui, "Pinch zoom");
    }
  else if (ui->touch_pinch_zoom_accum <= -QIBAN_PINCH_ZOOM_STEP_PX)
    {
      zoom = lv_offline_map_get_zoom(ui->map_widget);
      lv_offline_map_set_zoom(ui->map_widget, zoom - 1);
      ui->touch_pinch_zoom_accum += QIBAN_PINCH_ZOOM_STEP_PX;
      qiban_ui_capture_map_view_state(ui, true);
      qiban_ui_set_map_pending(ui, "Pinch zoom");
    }

  ui->touch_pinch_last_distance = distance;
  ui->touch_pinch_last_mid_x = mid_x;
  ui->touch_pinch_last_mid_y = mid_y;
}

static void qiban_ui_timer_cb(lv_timer_t *timer)
{
  struct qiban_ui_s *ui;
  double view_center_longitude;
  double view_center_latitude;
  int view_zoom;
  int view_override_active;
  int64_t last_map_updated_at;

  (void)timer;
  ui = g_qiban_ui;
  if (ui == NULL)
    {
      return;
    }

  if (!qiban_state_load_from_file(&ui->state))
    {
      qiban_state_step_mock(&ui->state, ui->tick);
    }

  view_center_longitude = ui->map_state.center_longitude;
  view_center_latitude = ui->map_state.center_latitude;
  view_zoom = ui->map_state.zoom;
  view_override_active = ui->map_view_override_active;
  last_map_updated_at = ui->active_map_updated_at;

  qiban_map_set_defaults(&ui->map_state);
  if (!qiban_map_load_from_file(&ui->map_state))
    {
      ui->map_state.updated_at = last_map_updated_at;
    }

  if (view_override_active && ui->map_state.updated_at <= last_map_updated_at)
    {
      ui->map_state.center_longitude = view_center_longitude;
      ui->map_state.center_latitude = view_center_latitude;
      ui->map_state.zoom = view_zoom;
    }
  else
    {
      ui->active_map_updated_at = ui->map_state.updated_at;
      ui->map_view_override_active = 0;
    }

  qiban_nav_set_defaults(&ui->nav_state);
  qiban_nav_load_from_file(&ui->nav_state);
  qiban_location_set_defaults(&ui->location_state);
  qiban_location_load_from_file(&ui->location_state);
  qiban_weather_set_defaults(&ui->weather_state);
  qiban_weather_load_from_file(&ui->weather_state);
  qiban_music_set_defaults(&ui->music_state);
  qiban_music_load_from_file(&ui->music_state);
  qiban_voice_set_defaults(&ui->voice_state);
  if (qiban_voice_load_from_file(&ui->voice_state))
    {
      ui->voice_status_manual = 0;
    }

  qiban_video_set_defaults(&ui->video_state);
  qiban_video_load_from_file(&ui->video_state);

  if (ui->nav_state.active && !ui->nav_active_latched)
    {
      if (ui->map_local_route_active)
        {
          qiban_ui_clear_local_route(ui);
          qiban_ui_clear_picked_destination(ui);
        }

      qiban_ui_set_page(ui, 1, LV_ANIM_ON);
    }

  ui->nav_active_latched = ui->nav_state.active;

  ui->tick++;
  qiban_ui_refresh_labels(ui);
}

static void qiban_ui_create(struct qiban_ui_s *ui)
{
  lv_obj_t *screen;
  lv_obj_t *control_button;
  lv_obj_t *dashboard_page;
  lv_obj_t *map_page;
  lv_obj_t *voice_page;
  lv_obj_t *dashboard_title_label;
  lv_obj_t *hero_card;
  lv_obj_t *map_control_label;
  lv_obj_t *map_frame;
  lv_obj_t *metrics_card;
  lv_coord_t screen_width;
  lv_coord_t screen_height;
  lv_coord_t map_top;
  lv_coord_t map_width;
  lv_coord_t map_height;
  lv_coord_t voice_keyboard_height;
  lv_coord_t voice_candidate_height;
  lv_coord_t voice_icon_top;
  lv_coord_t voice_quick_top;

  screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x070b12), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  screen_width = lv_obj_get_content_width(screen);
  screen_height = lv_obj_get_content_height(screen);
  map_top = 42;
  map_width = screen_width;
  map_height = screen_height - map_top;
  if (map_height < 120)
    {
      map_height = screen_height;
    }

  voice_keyboard_height = screen_height >= 320 ? 176 : 148;
  voice_candidate_height = 28;
  voice_icon_top = 56;
  voice_quick_top = 138;

  ui->tileview = lv_tileview_create(screen);
  lv_obj_set_style_bg_opa(ui->tileview, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui->tileview, 0, 0);
  lv_obj_set_scrollbar_mode(ui->tileview, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(ui->tileview, qiban_ui_tileview_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  ui->dashboard_tile = lv_tileview_add_tile(ui->tileview, 0, 0, LV_DIR_HOR);
  ui->map_tile = lv_tileview_add_tile(ui->tileview, 1, 0, LV_DIR_HOR);
  ui->voice_tile = lv_tileview_add_tile(ui->tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(ui->dashboard_tile, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scrollbar_mode(ui->map_tile, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scrollbar_mode(ui->voice_tile, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_opa(ui->dashboard_tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(ui->map_tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(ui->voice_tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui->dashboard_tile, 0, 0);
  lv_obj_set_style_border_width(ui->map_tile, 0, 0);
  lv_obj_set_style_border_width(ui->voice_tile, 0, 0);

  dashboard_page = ui->dashboard_tile;
  map_page = ui->map_tile;
  voice_page = ui->voice_tile;
  lv_obj_set_style_text_font(voice_page, QIBAN_CJK_FONT, 0);

  ui->header_label = lv_label_create(screen);
  lv_obj_set_style_text_font(ui->header_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->header_label, lv_color_hex(0xf8fafc), 0);
  lv_label_set_text(ui->header_label, "骑伴 AI");
  lv_obj_align(ui->header_label, LV_ALIGN_TOP_LEFT, 10, 8);

  ui->page_label = lv_label_create(screen);
  lv_obj_set_style_text_color(ui->page_label, lv_color_hex(0x38bdf8), 0);
  lv_label_set_text(ui->page_label, "01 / 06");
  lv_obj_align(ui->page_label, LV_ALIGN_TOP_MID, 0, 10);

  ui->prev_button = lv_button_create(screen);
  lv_obj_set_size(ui->prev_button, 28, 28);
  qiban_apply_button_style(ui->prev_button, lv_color_hex(0x111827));
  lv_obj_align(ui->prev_button, LV_ALIGN_TOP_RIGHT, -44, 6);
  lv_obj_add_event_cb(ui->prev_button, qiban_ui_prev_page_event_cb,
                      LV_EVENT_CLICKED, NULL);
  ui->prev_button_label = lv_label_create(ui->prev_button);
  lv_label_set_text(ui->prev_button_label, LV_SYMBOL_LEFT);
  lv_obj_center(ui->prev_button_label);

  ui->next_button = lv_button_create(screen);
  lv_obj_set_size(ui->next_button, 28, 28);
  qiban_apply_button_style(ui->next_button, lv_color_hex(0x111827));
  lv_obj_align(ui->next_button, LV_ALIGN_TOP_RIGHT, -10, 6);
  lv_obj_add_event_cb(ui->next_button, qiban_ui_next_page_event_cb,
                      LV_EVENT_CLICKED, NULL);
  ui->next_button_label = lv_label_create(ui->next_button);
  lv_label_set_text(ui->next_button_label, LV_SYMBOL_RIGHT);
  lv_obj_center(ui->next_button_label);

  dashboard_title_label = lv_label_create(dashboard_page);
  lv_obj_set_style_text_font(dashboard_title_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(dashboard_title_label, lv_color_hex(0xcbd5e1), 0);
  lv_label_set_text(dashboard_title_label, "当前骑行");
  lv_obj_align(dashboard_title_label, LV_ALIGN_TOP_LEFT, 10, 8);
  lv_obj_add_flag(dashboard_title_label, LV_OBJ_FLAG_HIDDEN);

  ui->source_label = lv_label_create(dashboard_page);
  lv_obj_set_style_text_color(ui->source_label, lv_color_hex(0x64748b), 0);
  lv_obj_set_width(ui->source_label, screen_width - 20);
  lv_label_set_long_mode(ui->source_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(ui->source_label, "booting");
  lv_obj_align(ui->source_label, LV_ALIGN_TOP_LEFT, 10, 32);

  hero_card = lv_obj_create(dashboard_page);
  qiban_apply_card_style(hero_card, lv_color_hex(0x0f172a));
  lv_obj_set_size(hero_card, screen_width - 20, 94);
  lv_obj_align(hero_card, LV_ALIGN_TOP_LEFT, 10, 54);

  ui->speed_label = lv_label_create(hero_card);
  lv_obj_set_style_text_font(ui->speed_label, QIBAN_SPEED_FONT, 0);
  lv_obj_set_style_text_color(ui->speed_label, lv_color_hex(0xf8fafc), 0);
  lv_label_set_text(ui->speed_label, "0 km/h");
  lv_obj_center(ui->speed_label);

  metrics_card = lv_obj_create(dashboard_page);
  qiban_apply_card_style(metrics_card, lv_color_hex(0x101827));
  lv_obj_set_size(metrics_card, screen_width - 20, 104);
  lv_obj_align(metrics_card, LV_ALIGN_TOP_LEFT, 10, 156);

  ui->battery_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_font(ui->battery_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->battery_label, lv_color_hex(0xe2e8f0), 0);
  lv_label_set_text(ui->battery_label, "Battery");
  lv_obj_align(ui->battery_label, LV_ALIGN_TOP_LEFT, 0, 0);

  ui->range_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_font(ui->range_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->range_label, lv_color_hex(0xe2e8f0), 0);
  lv_label_set_text(ui->range_label, "Range");
  lv_obj_align(ui->range_label, LV_ALIGN_TOP_LEFT, 0, 24);

  ui->ride_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_font(ui->ride_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->ride_label, lv_color_hex(0xe2e8f0), 0);
  lv_label_set_text(ui->ride_label, "Ride");
  lv_obj_align(ui->ride_label, LV_ALIGN_TOP_LEFT, 0, 48);

  ui->distance_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_font(ui->distance_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->distance_label, lv_color_hex(0xe2e8f0), 0);
  lv_label_set_text(ui->distance_label, "Distance");
  lv_obj_align(ui->distance_label, LV_ALIGN_TOP_LEFT, 0, 72);

  ui->nav_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_font(ui->nav_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->nav_label, lv_color_hex(0x38bdf8), 0);
  lv_label_set_text(ui->nav_label, "Nav");
  lv_obj_align(ui->nav_label, LV_ALIGN_TOP_RIGHT, 0, 0);

  ui->alert_panel = lv_obj_create(dashboard_page);
  qiban_apply_card_style(ui->alert_panel, lv_color_hex(0x166534));
  lv_obj_set_size(ui->alert_panel, screen_width - 20, 40);
  lv_obj_align(ui->alert_panel, LV_ALIGN_TOP_LEFT, 10, 270);

  ui->alert_label = lv_label_create(ui->alert_panel);
  lv_obj_set_style_text_font(ui->alert_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->alert_label, lv_color_hex(0xf8fafc), 0);
  lv_label_set_text(ui->alert_label, "Status");
  lv_obj_center(ui->alert_label);

  map_frame = lv_obj_create(map_page);
  ui->map_frame = map_frame;
  lv_obj_set_style_bg_color(map_frame, lv_color_hex(0x0b1020), 0);
  lv_obj_set_style_bg_opa(map_frame, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(map_frame, 0, 0);
  lv_obj_set_style_radius(map_frame, 0, 0);
  lv_obj_set_size(map_frame, map_width, map_height);
  lv_obj_align(map_frame, LV_ALIGN_TOP_MID, 0, map_top);
  lv_obj_set_style_pad_all(map_frame, 0, 0);
  lv_obj_set_scrollbar_mode(map_frame, LV_SCROLLBAR_MODE_OFF);

  ui->map_page_image = NULL;
  ui->map_route_line = NULL;

  {
    lv_offline_map_config_t map_config;

    lv_offline_map_get_default_config(&map_config);
    map_config.map_dir = QIBAN_OFFLINE_MAP_DIR;
    map_config.tile_file_name = QIBAN_OFFLINE_MAP_TILE_FILE;
    map_config.view_width = map_width;
    map_config.view_height = map_height;
    map_config.min_zoom = QIBAN_OFFLINE_MAP_MIN_ZOOM;
    map_config.max_zoom = QIBAN_OFFLINE_MAP_MAX_ZOOM;
    map_config.default_zoom = QIBAN_OFFLINE_MAP_DEFAULT_ZOOM;
    map_config.track_max_points = QIBAN_OFFLINE_MAP_TRACK_POINTS;
    map_config.use_gcj02_tile = false;
    map_config.show_zoom_controls = false;
    map_config.show_vehicle_marker = true;
    map_config.zoom_anim_enable = true;
    ui->map_widget = lv_offline_map_create_with_config(map_frame, &map_config);
  }

  if (ui->map_widget != NULL)
    {
      lv_obj_t *map_container;

      lv_obj_set_size(ui->map_widget, map_width, map_height);
      lv_obj_align(ui->map_widget, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(ui->map_widget, lv_color_hex(0x020617), 0);
      lv_obj_set_style_bg_opa(ui->map_widget, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(ui->map_widget, 0, 0);
      lv_obj_set_style_radius(ui->map_widget, 0, 0);
      qiban_ui_get_effective_map_center(ui, &ui->map_view_center_lon_e7,
                                        &ui->map_view_center_lat_e7);
      ui->map_view_zoom = qiban_ui_effective_map_zoom(ui);
      lv_offline_map_center_lonlat_e7(ui->map_widget,
                                      ui->map_view_center_lon_e7,
                                      ui->map_view_center_lat_e7,
                                      ui->map_view_zoom);
      lv_obj_add_event_cb(ui->map_widget, qiban_ui_map_event_cb,
                          LV_EVENT_ALL, NULL);
      map_container = lv_offline_map_get_container(ui->map_widget);
      if (map_container != NULL)
        {
          lv_obj_add_event_cb(map_container, qiban_ui_map_event_cb,
                              LV_EVENT_ALL, NULL);
        }
    }

  ui->map_route_line = lv_line_create(map_frame);
  lv_line_set_points_mutable(ui->map_route_line, ui->map_route_points, 2);
  lv_obj_add_flag(ui->map_route_line, LV_OBJ_FLAG_HIDDEN);

  ui->location_marker = lv_obj_create(map_frame);
  lv_obj_set_size(ui->location_marker, QIBAN_MAP_MARKER_SIZE,
                  QIBAN_MAP_MARKER_SIZE);
  lv_obj_set_style_bg_color(ui->location_marker, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(ui->location_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ui->location_marker, lv_color_hex(0xef4444), 0);
  lv_obj_set_style_border_width(ui->location_marker, 2, 0);
  lv_obj_set_style_radius(ui->location_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(ui->location_marker, 0, 0);
  lv_obj_clear_flag(ui->location_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui->location_marker, LV_OBJ_FLAG_HIDDEN);

  ui->location_marker_core = lv_obj_create(ui->location_marker);
  lv_obj_set_size(ui->location_marker_core, 6, 6);
  lv_obj_set_style_bg_color(ui->location_marker_core,
                            lv_color_hex(0xef4444), 0);
  lv_obj_set_style_bg_opa(ui->location_marker_core, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ui->location_marker_core, 0, 0);
  lv_obj_set_style_radius(ui->location_marker_core, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(ui->location_marker_core, 0, 0);
  lv_obj_clear_flag(ui->location_marker_core, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(ui->location_marker_core);

  ui->location_label = lv_label_create(map_frame);
  lv_obj_set_style_text_font(ui->location_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_bg_color(ui->location_label, lv_color_hex(0x020617), 0);
  lv_obj_set_style_bg_opa(ui->location_label, LV_OPA_70, 0);
  lv_obj_set_style_text_color(ui->location_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_pad_hor(ui->location_label, 6, 0);
  lv_obj_set_style_pad_ver(ui->location_label, 3, 0);
  lv_obj_set_style_radius(ui->location_label, 6, 0);
  lv_label_set_text(ui->location_label, "我");
  lv_obj_add_flag(ui->location_label, LV_OBJ_FLAG_HIDDEN);

  ui->map_page_status_label = lv_label_create(map_frame);
  lv_obj_set_style_bg_color(ui->map_page_status_label, lv_color_hex(0x020617), 0);
  lv_obj_set_style_bg_opa(ui->map_page_status_label, LV_OPA_70, 0);
  lv_obj_set_style_text_color(ui->map_page_status_label, lv_color_hex(0xe5eefb), 0);
  lv_obj_set_style_pad_hor(ui->map_page_status_label, 8, 0);
  lv_obj_set_style_pad_ver(ui->map_page_status_label, 4, 0);
  lv_obj_set_style_radius(ui->map_page_status_label, 8, 0);
  lv_obj_set_width(ui->map_page_status_label, map_width - 16);
  lv_label_set_long_mode(ui->map_page_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(ui->map_page_status_label, "Map");
  lv_obj_align(ui->map_page_status_label, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_add_flag(ui->map_page_status_label, LV_OBJ_FLAG_HIDDEN);

  ui->map_pick_start_marker = lv_obj_create(map_frame);
  lv_obj_set_size(ui->map_pick_start_marker, QIBAN_MAP_PICK_MARKER_SIZE,
                  QIBAN_MAP_PICK_MARKER_SIZE);
  lv_obj_set_style_bg_color(ui->map_pick_start_marker,
                            lv_color_hex(0x22c55e), 0);
  lv_obj_set_style_bg_opa(ui->map_pick_start_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ui->map_pick_start_marker,
                                lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(ui->map_pick_start_marker, 2, 0);
  lv_obj_set_style_radius(ui->map_pick_start_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(ui->map_pick_start_marker, 0, 0);
  lv_obj_clear_flag(ui->map_pick_start_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui->map_pick_start_marker, LV_OBJ_FLAG_HIDDEN);

  ui->map_pick_marker = lv_obj_create(map_frame);
  lv_obj_set_size(ui->map_pick_marker, QIBAN_MAP_PICK_MARKER_SIZE,
                  QIBAN_MAP_PICK_MARKER_SIZE);
  lv_obj_set_style_bg_color(ui->map_pick_marker, lv_color_hex(0xff5a36), 0);
  lv_obj_set_style_bg_opa(ui->map_pick_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ui->map_pick_marker, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(ui->map_pick_marker, 2, 0);
  lv_obj_set_style_radius(ui->map_pick_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(ui->map_pick_marker, 0, 0);
  lv_obj_clear_flag(ui->map_pick_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui->map_pick_marker, LV_OBJ_FLAG_HIDDEN);

  ui->map_pick_confirm_button = lv_button_create(map_frame);
  lv_obj_set_size(ui->map_pick_confirm_button, QIBAN_MAP_PICK_CONFIRM_WIDTH,
                  QIBAN_MAP_PICK_CONFIRM_HEIGHT);
  qiban_apply_button_style(ui->map_pick_confirm_button,
                           lv_color_hex(0x22c55e));
  lv_obj_align(ui->map_pick_confirm_button, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_add_event_cb(ui->map_pick_confirm_button,
                      qiban_ui_map_pick_confirm_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(ui->map_pick_confirm_button, LV_OBJ_FLAG_HIDDEN);

  map_control_label = lv_label_create(ui->map_pick_confirm_button);
  lv_obj_set_style_text_font(map_control_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(map_control_label, lv_color_hex(0x03120a), 0);
  lv_label_set_text(map_control_label, "确认路线");
  lv_obj_center(map_control_label);

  ui->voice_status_label = lv_label_create(voice_page);
  lv_obj_set_style_text_font(ui->voice_status_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->voice_status_label,
                              lv_color_hex(0x94a3b8), 0);
  lv_obj_set_width(ui->voice_status_label, screen_width - 20);
  lv_label_set_long_mode(ui->voice_status_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(ui->voice_status_label,
                    "点击键盘输入目的地，或点击麦克风准备录音");
  lv_obj_align(ui->voice_status_label, LV_ALIGN_TOP_LEFT, 10, 32);

  ui->voice_icon_panel = lv_obj_create(voice_page);
  qiban_apply_card_style(ui->voice_icon_panel, lv_color_hex(0x0f172a));
  lv_obj_set_style_pad_all(ui->voice_icon_panel, 10, 0);
  lv_obj_set_size(ui->voice_icon_panel, screen_width - 20, 72);
  lv_obj_align(ui->voice_icon_panel, LV_ALIGN_TOP_LEFT, 10, voice_icon_top);

  control_button = lv_button_create(ui->voice_icon_panel);
  lv_obj_set_size(control_button, 88, 52);
  qiban_apply_button_style(control_button, lv_color_hex(0x111827));
  lv_obj_align(control_button, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(control_button, qiban_ui_voice_icon_event_cb,
                      LV_EVENT_CLICKED, "keyboard");
  map_control_label = lv_label_create(control_button);
  lv_obj_set_style_text_font(map_control_label, QIBAN_TITLE_FONT, 0);
  lv_label_set_text(map_control_label, LV_SYMBOL_KEYBOARD);
  lv_obj_center(map_control_label);

  control_button = lv_button_create(ui->voice_icon_panel);
  lv_obj_set_size(control_button, 88, 52);
  qiban_apply_button_style(control_button, lv_color_hex(0x0f766e));
  lv_obj_align(control_button, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(control_button, qiban_ui_voice_icon_event_cb,
                      LV_EVENT_CLICKED, "record");
  map_control_label = lv_label_create(control_button);
  lv_obj_set_style_text_font(map_control_label, QIBAN_TITLE_FONT, 0);
  lv_label_set_text(map_control_label, LV_SYMBOL_AUDIO);
  lv_obj_center(map_control_label);

  ui->voice_quick_panel = lv_obj_create(voice_page);
  qiban_apply_card_style(ui->voice_quick_panel, lv_color_hex(0x101827));
  lv_obj_set_style_text_font(ui->voice_quick_panel, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_pad_all(ui->voice_quick_panel, 6, 0);
  lv_obj_set_size(ui->voice_quick_panel, screen_width - 20, 38);
  lv_obj_align(ui->voice_quick_panel, LV_ALIGN_TOP_LEFT, 10, voice_quick_top);

  control_button = lv_button_create(ui->voice_quick_panel);
  lv_obj_set_size(control_button, 64, 24);
  qiban_apply_button_style(control_button, lv_color_hex(0x111827));
  lv_obj_align(control_button, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(control_button, qiban_ui_voice_preset_event_cb,
                      LV_EVENT_CLICKED, "清华大学");
  map_control_label = lv_label_create(control_button);
  lv_label_set_text(map_control_label, "清华大学");
  lv_obj_center(map_control_label);

  control_button = lv_button_create(ui->voice_quick_panel);
  lv_obj_set_size(control_button, 64, 24);
  qiban_apply_button_style(control_button, lv_color_hex(0x111827));
  lv_obj_align(control_button, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(control_button, qiban_ui_voice_preset_event_cb,
                      LV_EVENT_CLICKED, "北京大学东门");
  map_control_label = lv_label_create(control_button);
  lv_label_set_text(map_control_label, "北大东门");
  lv_obj_center(map_control_label);

  control_button = lv_button_create(ui->voice_quick_panel);
  lv_obj_set_size(control_button, 64, 24);
  qiban_apply_button_style(control_button, lv_color_hex(0x111827));
  lv_obj_align(control_button, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(control_button, qiban_ui_voice_preset_event_cb,
                      LV_EVENT_CLICKED, "软件园二期");
  map_control_label = lv_label_create(control_button);
  lv_label_set_text(map_control_label, "软件园二期");
  lv_obj_center(map_control_label);

  ui->voice_textarea_panel = lv_obj_create(voice_page);
  qiban_apply_card_style(ui->voice_textarea_panel, lv_color_hex(0x0f172a));
  lv_obj_set_style_pad_all(ui->voice_textarea_panel, 8, 0);
  lv_obj_set_size(ui->voice_textarea_panel, screen_width - 20, 44);
  lv_obj_align(ui->voice_textarea_panel, LV_ALIGN_TOP_LEFT, 10, voice_icon_top);

  ui->voice_textarea = lv_textarea_create(ui->voice_textarea_panel);
  lv_obj_set_size(ui->voice_textarea, screen_width - 36, 28);
  lv_obj_align(ui->voice_textarea, LV_ALIGN_CENTER, 0, 0);
  lv_textarea_set_one_line(ui->voice_textarea, true);
  lv_textarea_set_max_length(ui->voice_textarea, 120);
  lv_obj_set_style_text_font(ui->voice_textarea, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_color(ui->voice_textarea, lv_color_hex(0xe2e8f0), 0);
  lv_obj_set_style_bg_color(ui->voice_textarea, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_color(ui->voice_textarea, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(ui->voice_textarea, 1, 0);
  lv_obj_set_style_radius(ui->voice_textarea, 6, 0);
  lv_textarea_set_placeholder_text(ui->voice_textarea,
                                   "");

#ifdef CONFIG_LV_USE_IME_PINYIN
  ui->voice_ime = lv_ime_pinyin_create(voice_page);
  lv_obj_set_style_text_font(ui->voice_ime, QIBAN_CJK_FONT, 0);
  lv_ime_pinyin_set_mode(ui->voice_ime, LV_IME_PINYIN_MODE_K26);
#endif

  ui->voice_keyboard = lv_keyboard_create(voice_page);
  lv_obj_set_size(ui->voice_keyboard, screen_width, voice_keyboard_height);
  lv_obj_align(ui->voice_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(ui->voice_keyboard, QIBAN_CJK_FONT, 0);
  lv_obj_add_event_cb(ui->voice_keyboard, qiban_ui_voice_keyboard_event_cb,
                      LV_EVENT_ALL, NULL);

#ifdef CONFIG_LV_USE_IME_PINYIN
  lv_ime_pinyin_set_keyboard(ui->voice_ime, ui->voice_keyboard);
  lv_keyboard_set_textarea(ui->voice_keyboard, ui->voice_textarea);
  ui->voice_cand_panel = lv_ime_pinyin_get_cand_panel(ui->voice_ime);
  lv_obj_set_size(ui->voice_cand_panel, screen_width - 20,
                  voice_candidate_height);
  lv_obj_set_style_text_font(ui->voice_cand_panel, QIBAN_CJK_FONT, 0);
  lv_obj_align_to(ui->voice_cand_panel, ui->voice_keyboard,
                  LV_ALIGN_OUT_TOP_MID, 0, -4);
#else
  lv_keyboard_set_textarea(ui->voice_keyboard, ui->voice_textarea);
#endif

  qiban_ui_set_voice_input_visible(ui, false);

  /* --- Weather / Date tile (page 4) --- */

  {
    lv_obj_t *weather_page;
    lv_obj_t *date_card;
    lv_obj_t *weather_card;
    lv_obj_t *weather_icon_panel;

    ui->weather_tile = lv_tileview_add_tile(ui->tileview, 3, 0,
                                            LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui->weather_tile, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->weather_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->weather_tile, 0, 0);
    weather_page = ui->weather_tile;

    /* Date & Time card */

    date_card = lv_obj_create(weather_page);
    qiban_apply_card_style(date_card, lv_color_hex(0x0f172a));
    lv_obj_set_size(date_card, screen_width - 20, 100);
    lv_obj_align(date_card, LV_ALIGN_TOP_MID, 0, 10);

    ui->weather_date_label = lv_label_create(date_card);
    lv_obj_set_style_text_font(ui->weather_date_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_date_label,
                                lv_color_hex(0xf8fafc), 0);
    lv_label_set_text(ui->weather_date_label, "----年--月--日");
    lv_obj_align(ui->weather_date_label, LV_ALIGN_TOP_MID, 0, 8);

    ui->weather_time_label = lv_label_create(date_card);
    lv_obj_set_style_text_font(ui->weather_time_label,
                               QIBAN_SPEED_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_time_label,
                                lv_color_hex(0x38bdf8), 0);
    lv_label_set_text(ui->weather_time_label, "--:--");
    lv_obj_align(ui->weather_time_label, LV_ALIGN_TOP_MID, 0, 40);

    /* Weather info card */

    weather_card = lv_obj_create(weather_page);
    qiban_apply_card_style(weather_card, lv_color_hex(0x101827));
    lv_obj_set_size(weather_card, screen_width - 20, 152);
    lv_obj_align(weather_card, LV_ALIGN_TOP_MID, 0, 120);

    weather_icon_panel = lv_obj_create(weather_card);
    lv_obj_set_size(weather_icon_panel, 60, 60);
    lv_obj_set_style_bg_color(weather_icon_panel, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_bg_opa(weather_icon_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weather_icon_panel, 0, 0);
    lv_obj_set_style_radius(weather_icon_panel, 30, 0);
    lv_obj_align(weather_icon_panel, LV_ALIGN_TOP_LEFT, 8, 8);

    ui->weather_icon_label = lv_label_create(weather_icon_panel);
    lv_obj_set_style_text_font(ui->weather_icon_label,
                               QIBAN_TITLE_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_icon_label,
                                lv_color_hex(0xfbbf24), 0);
    lv_label_set_text(ui->weather_icon_label, LV_SYMBOL_REFRESH);
    lv_obj_center(ui->weather_icon_label);

    ui->weather_temp_label = lv_label_create(weather_card);
    lv_obj_set_style_text_font(ui->weather_temp_label,
                               QIBAN_SPEED_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_temp_label,
                                lv_color_hex(0xf8fafc), 0);
    lv_label_set_text(ui->weather_temp_label, "--°C");
    lv_obj_align(ui->weather_temp_label, LV_ALIGN_TOP_LEFT, 78, 8);

    ui->weather_cond_label = lv_label_create(weather_card);
    lv_obj_set_style_text_font(ui->weather_cond_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_cond_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_obj_set_width(ui->weather_cond_label, screen_width - 112);
    lv_label_set_long_mode(ui->weather_cond_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->weather_cond_label, "暂无天气数据");
    lv_obj_align(ui->weather_cond_label, LV_ALIGN_TOP_LEFT, 78, 56);

    ui->weather_detail_label = lv_label_create(weather_card);
    lv_obj_set_style_text_font(ui->weather_detail_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_detail_label,
                                lv_color_hex(0x64748b), 0);
    lv_obj_set_width(ui->weather_detail_label, screen_width - 44);
    lv_label_set_long_mode(ui->weather_detail_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->weather_detail_label, "");
    lv_obj_align(ui->weather_detail_label, LV_ALIGN_TOP_LEFT, 8, 90);

    ui->weather_source_label = lv_label_create(weather_card);
    lv_obj_set_style_text_font(ui->weather_source_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->weather_source_label,
                                lv_color_hex(0x64748b), 0);
    lv_obj_set_width(ui->weather_source_label, screen_width - 44);
    lv_label_set_long_mode(ui->weather_source_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->weather_source_label,
                      "等待 /data/qiban_weather.json");
    lv_obj_align(ui->weather_source_label, LV_ALIGN_TOP_LEFT, 8, 114);
  }

  /* --- Music player tile (page 5) --- */

  {
    lv_obj_t *music_page;
    lv_obj_t *music_info_card;
    lv_obj_t *music_ctrl_card;
    lv_obj_t *music_src_card;
    lv_obj_t *btn_prev;
    lv_obj_t *btn_next;
    lv_obj_t *btn_prev_label;
    lv_obj_t *btn_next_label;

    ui->music_tile = lv_tileview_add_tile(ui->tileview, 4, 0, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui->music_tile, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->music_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->music_tile, 0, 0);
    music_page = ui->music_tile;

    /* Song info card */

    music_info_card = lv_obj_create(music_page);
    qiban_apply_card_style(music_info_card, lv_color_hex(0x0f172a));
    lv_obj_set_size(music_info_card, screen_width - 20, 80);
    lv_obj_align(music_info_card, LV_ALIGN_TOP_MID, 0, 10);

    ui->music_source_label = lv_label_create(music_info_card);
    lv_obj_set_style_text_font(ui->music_source_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->music_source_label,
                                lv_color_hex(0x38bdf8), 0);
    lv_obj_set_width(ui->music_source_label, screen_width - 110);
    lv_label_set_long_mode(ui->music_source_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->music_source_label,
                      LV_SYMBOL_SD_CARD " SD\xe5\x8d\xa1");
    lv_obj_align(ui->music_source_label, LV_ALIGN_TOP_LEFT, 0, 0);

    ui->music_title_label = lv_label_create(music_info_card);
    lv_obj_set_style_text_font(ui->music_title_label, QIBAN_TITLE_FONT, 0);
    lv_obj_set_style_text_color(ui->music_title_label,
                                lv_color_hex(0xf8fafc), 0);
    lv_label_set_text(ui->music_title_label,
                      "\xe6\x9c\xaa\xe5\x9c\xa8\xe6\x92\xad\xe6\x94\xbe");
    lv_obj_set_width(ui->music_title_label, screen_width - 44);
    lv_label_set_long_mode(ui->music_title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(ui->music_title_label, LV_ALIGN_TOP_LEFT, 0, 22);

    ui->music_artist_label = lv_label_create(music_info_card);
    lv_obj_set_style_text_font(ui->music_artist_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->music_artist_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_obj_set_width(ui->music_artist_label, screen_width - 44);
    lv_label_set_long_mode(ui->music_artist_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->music_artist_label, "");
    lv_obj_align(ui->music_artist_label, LV_ALIGN_TOP_LEFT, 0, 50);

    ui->music_track_label = lv_label_create(music_info_card);
    lv_obj_set_style_text_font(ui->music_track_label, QIBAN_METRIC_FONT, 0);
    lv_obj_set_style_text_color(ui->music_track_label,
                                lv_color_hex(0x64748b), 0);
    lv_obj_set_width(ui->music_track_label, 58);
    lv_label_set_long_mode(ui->music_track_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->music_track_label, "- / -");
    lv_obj_align(ui->music_track_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Progress & controls card */

    music_ctrl_card = lv_obj_create(music_page);
    qiban_apply_card_style(music_ctrl_card, lv_color_hex(0x101827));
    lv_obj_set_size(music_ctrl_card, screen_width - 20, 90);
    lv_obj_align(music_ctrl_card, LV_ALIGN_TOP_MID, 0, 100);

    ui->music_progress_bar = lv_bar_create(music_ctrl_card);
    lv_obj_set_size(ui->music_progress_bar, screen_width - 50, 8);
    lv_bar_set_range(ui->music_progress_bar, 0, 100);
    lv_bar_set_value(ui->music_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->music_progress_bar,
                              lv_color_hex(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->music_progress_bar,
                              lv_color_hex(0x38bdf8), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->music_progress_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->music_progress_bar, 4, LV_PART_INDICATOR);
    lv_obj_align(ui->music_progress_bar, LV_ALIGN_TOP_MID, 0, 5);

    ui->music_time_label = lv_label_create(music_ctrl_card);
    lv_obj_set_style_text_font(ui->music_time_label, QIBAN_METRIC_FONT, 0);
    lv_obj_set_style_text_color(ui->music_time_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_label_set_text(ui->music_time_label, "0:00 / 0:00");
    lv_obj_align(ui->music_time_label, LV_ALIGN_TOP_MID, 0, 18);

    /* Control buttons: prev / play / next */

    btn_prev = lv_btn_create(music_ctrl_card);
    lv_obj_set_size(btn_prev, 40, 32);
    qiban_apply_button_style(btn_prev, lv_color_hex(0x1e293b));
    lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_LEFT, 15, -5);
    lv_obj_add_event_cb(btn_prev, qiban_ui_music_command_event_cb,
                        LV_EVENT_CLICKED, "prev");
    btn_prev_label = lv_label_create(btn_prev);
    lv_label_set_text(btn_prev_label, LV_SYMBOL_PREV);
    lv_obj_center(btn_prev_label);

    ui->music_play_btn = lv_btn_create(music_ctrl_card);
    lv_obj_set_size(ui->music_play_btn, 50, 32);
    qiban_apply_button_style(ui->music_play_btn, lv_color_hex(0x1e40af));
    lv_obj_align(ui->music_play_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_event_cb(ui->music_play_btn, qiban_ui_music_command_event_cb,
                        LV_EVENT_CLICKED, "toggle");
    ui->music_play_btn_label = lv_label_create(ui->music_play_btn);
    lv_label_set_text(ui->music_play_btn_label, LV_SYMBOL_PLAY);
    lv_obj_center(ui->music_play_btn_label);

    btn_next = lv_btn_create(music_ctrl_card);
    lv_obj_set_size(btn_next, 40, 32);
    qiban_apply_button_style(btn_next, lv_color_hex(0x1e293b));
    lv_obj_align(btn_next, LV_ALIGN_BOTTOM_RIGHT, -15, -5);
    lv_obj_add_event_cb(btn_next, qiban_ui_music_command_event_cb,
                        LV_EVENT_CLICKED, "next");
    btn_next_label = lv_label_create(btn_next);
    lv_label_set_text(btn_next_label, LV_SYMBOL_NEXT);
    lv_obj_center(btn_next_label);

    /* Source selection & volume card */

    music_src_card = lv_obj_create(music_page);
    qiban_apply_card_style(music_src_card, lv_color_hex(0x0f172a));
    lv_obj_set_size(music_src_card, screen_width - 20, 92);
    lv_obj_align(music_src_card, LV_ALIGN_TOP_MID, 0, 200);

    /* Source buttons: SD卡 / WiFi / 蓝牙 */

    ui->music_src_sdcard_btn = lv_btn_create(music_src_card);
    lv_obj_set_size(ui->music_src_sdcard_btn, 70, 28);
    qiban_apply_button_style(ui->music_src_sdcard_btn,
                             lv_color_hex(0x1e40af));
    lv_obj_align(ui->music_src_sdcard_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(ui->music_src_sdcard_btn,
                        qiban_ui_music_source_event_cb,
                        LV_EVENT_CLICKED, "sdcard");
    lv_obj_t *sdcard_lbl = lv_label_create(ui->music_src_sdcard_btn);
    lv_obj_set_style_text_font(sdcard_lbl, QIBAN_CJK_FONT, 0);
    lv_label_set_text(sdcard_lbl,
                      "SD\xe5\x8d\xa1");
    lv_obj_center(sdcard_lbl);

    ui->music_src_wifi_btn = lv_btn_create(music_src_card);
    lv_obj_set_size(ui->music_src_wifi_btn, 70, 28);
    qiban_apply_button_style(ui->music_src_wifi_btn,
                             lv_color_hex(0x1e293b));
    lv_obj_align(ui->music_src_wifi_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(ui->music_src_wifi_btn,
                        qiban_ui_music_source_event_cb,
                        LV_EVENT_CLICKED, "wifi");
    lv_obj_t *wifi_lbl = lv_label_create(ui->music_src_wifi_btn);
    lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI " WiFi");
    lv_obj_center(wifi_lbl);

    ui->music_src_bt_btn = lv_btn_create(music_src_card);
    lv_obj_set_size(ui->music_src_bt_btn, 70, 28);
    qiban_apply_button_style(ui->music_src_bt_btn,
                             lv_color_hex(0x1e293b));
    lv_obj_align(ui->music_src_bt_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(ui->music_src_bt_btn,
                        qiban_ui_music_source_event_cb,
                        LV_EVENT_CLICKED, "bluetooth");
    lv_obj_t *bt_lbl = lv_label_create(ui->music_src_bt_btn);
    lv_obj_set_style_text_font(bt_lbl, QIBAN_CJK_FONT, 0);
    lv_label_set_text(bt_lbl, LV_SYMBOL_BLUETOOTH " 蓝牙");
    lv_obj_center(bt_lbl);

    /* Volume label */

    ui->music_vol_label = lv_label_create(music_src_card);
    lv_obj_set_style_text_font(ui->music_vol_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->music_vol_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_obj_set_width(ui->music_vol_label, screen_width - 44);
    lv_label_set_long_mode(ui->music_vol_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->music_vol_label,
                      LV_SYMBOL_VOLUME_MAX " 60%");
    lv_obj_align(ui->music_vol_label, LV_ALIGN_TOP_LEFT, 0, 36);

    ui->music_conn_label = lv_label_create(music_src_card);
    lv_obj_set_style_text_font(ui->music_conn_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->music_conn_label,
                                lv_color_hex(0x64748b), 0);
    lv_obj_set_width(ui->music_conn_label, screen_width - 44);
    lv_label_set_long_mode(ui->music_conn_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->music_conn_label,
                      "SD卡 未插入  WiFi 未连接  蓝牙 未就绪");
    lv_obj_align(ui->music_conn_label, LV_ALIGN_TOP_LEFT, 0, 58);
  }

  /* --- Video player + file browser tile (page 6) --- */

  {
    lv_obj_t *video_page;
    lv_obj_t *video_header_card;
    lv_obj_t *video_ctrl_card;
    lv_obj_t *video_btn_panel;

    ui->video_tile = lv_tileview_add_tile(ui->tileview, 5, 0, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui->video_tile, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->video_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->video_tile, 0, 0);
    video_page = ui->video_tile;

    /* ── Header: path + state ── */

    video_header_card = lv_obj_create(video_page);
    qiban_apply_card_style(video_header_card, lv_color_hex(0x0f172a));
    lv_obj_set_size(video_header_card, screen_width - 20, 40);
    lv_obj_align(video_header_card, LV_ALIGN_TOP_MID, 0, 4);

    ui->video_path_label = lv_label_create(video_header_card);
    lv_obj_set_style_text_font(ui->video_path_label, QIBAN_METRIC_FONT, 0);
    lv_obj_set_style_text_color(ui->video_path_label,
                                lv_color_hex(0x38bdf8), 0);
    lv_label_set_text(ui->video_path_label, "/");
    lv_obj_set_width(ui->video_path_label, screen_width - 44);
    lv_label_set_long_mode(ui->video_path_label, LV_LABEL_LONG_DOT);
    lv_obj_align(ui->video_path_label, LV_ALIGN_TOP_LEFT, 0, 0);

    ui->video_state_label = lv_label_create(video_header_card);
    lv_obj_set_style_text_font(ui->video_state_label, QIBAN_METRIC_FONT, 0);
    lv_obj_set_style_text_color(ui->video_state_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_label_set_text(ui->video_state_label, LV_SYMBOL_STOP);
    lv_obj_align(ui->video_state_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* ── File browser list ── */

    ui->video_browser_list = lv_list_create(video_page);
    lv_obj_set_size(ui->video_browser_list, screen_width - 20, 150);
    lv_obj_align(ui->video_browser_list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(ui->video_browser_list,
                              lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(ui->video_browser_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui->video_browser_list,
                                  lv_color_hex(0x263244), 0);
    lv_obj_set_style_border_width(ui->video_browser_list, 1, 0);
    lv_obj_set_style_radius(ui->video_browser_list, 8, 0);
    lv_obj_set_style_pad_all(ui->video_browser_list, 4, 0);
    lv_obj_add_event_cb(ui->video_browser_list, video_browser_item_cb,
                        LV_EVENT_CLICKED, ui);

    /* Placeholder items */

    lv_obj_t *placeholder = lv_list_add_btn(ui->video_browser_list,
                                            LV_SYMBOL_DIRECTORY,
                                            "\xe6\x89\xab\xe6\x8f\x8f\xe4\xb8\xad...");
    /* "扫描中..." */
    lv_obj_set_style_text_font(placeholder, QIBAN_CJK_FONT, 0);

    /* ── Playback controls bar ── */

    video_ctrl_card = lv_obj_create(video_page);
    qiban_apply_card_style(video_ctrl_card, lv_color_hex(0x101827));
    lv_obj_set_size(video_ctrl_card, screen_width - 20, 60);
    lv_obj_align(video_ctrl_card, LV_ALIGN_TOP_MID, 0, 202);

    /* Now playing label */

    ui->video_name_label = lv_label_create(video_ctrl_card);
    lv_obj_set_style_text_font(ui->video_name_label, QIBAN_CJK_FONT, 0);
    lv_obj_set_style_text_color(ui->video_name_label,
                                lv_color_hex(0xf8fafc), 0);
    lv_label_set_text(ui->video_name_label,
                      "\xe6\x9c\xaa\xe9\x80\x89\xe6\x8b\xa9\xe8\xa7\x86\xe9\xa2\x91");
    /* "未选择视频" */
    lv_obj_set_width(ui->video_name_label, screen_width - 50);
    lv_label_set_long_mode(ui->video_name_label, LV_LABEL_LONG_DOT);
    lv_obj_align(ui->video_name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Progress bar */

    ui->video_progress_bar = lv_bar_create(video_ctrl_card);
    lv_obj_set_size(ui->video_progress_bar, screen_width - 80, 6);
    lv_bar_set_range(ui->video_progress_bar, 0, 100);
    lv_bar_set_value(ui->video_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->video_progress_bar,
                              lv_color_hex(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->video_progress_bar,
                              lv_color_hex(0x22c55e), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->video_progress_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->video_progress_bar, 3, LV_PART_INDICATOR);
    lv_obj_align(ui->video_progress_bar, LV_ALIGN_TOP_LEFT, 0, 22);

    /* Time label */

    ui->video_time_label = lv_label_create(video_ctrl_card);
    lv_obj_set_style_text_font(ui->video_time_label, QIBAN_METRIC_FONT, 0);
    lv_obj_set_style_text_color(ui->video_time_label,
                                lv_color_hex(0x94a3b8), 0);
    lv_label_set_text(ui->video_time_label, "0:00");
    lv_obj_align(ui->video_time_label, LV_ALIGN_TOP_RIGHT, 0, 22);

    /* ── Navigation buttons ── */

    video_btn_panel = lv_obj_create(video_page);
    lv_obj_set_size(video_btn_panel, screen_width - 20, 32);
    lv_obj_align(video_btn_panel, LV_ALIGN_TOP_MID, 0, 266);
    lv_obj_set_style_bg_opa(video_btn_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(video_btn_panel, 0, 0);
    lv_obj_set_style_pad_all(video_btn_panel, 0, 0);

    int btn_w = (screen_width - 30) / 4;

    /* Back button */

    ui->video_btn_back = lv_btn_create(video_btn_panel);
    lv_obj_set_size(ui->video_btn_back, btn_w, 28);
    qiban_apply_button_style(ui->video_btn_back, lv_color_hex(0x1e293b));
    lv_obj_align(ui->video_btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *back_lbl = lv_label_create(ui->video_btn_back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(ui->video_btn_back, video_btn_back_cb,
                        LV_EVENT_CLICKED, ui);

    /* Up button */

    ui->video_btn_up = lv_btn_create(video_btn_panel);
    lv_obj_set_size(ui->video_btn_up, btn_w, 28);
    qiban_apply_button_style(ui->video_btn_up, lv_color_hex(0x1e293b));
    lv_obj_align(ui->video_btn_up, LV_ALIGN_TOP_LEFT, btn_w + 2, 0);
    lv_obj_t *up_lbl = lv_label_create(ui->video_btn_up);
    lv_label_set_text(up_lbl, LV_SYMBOL_UP);
    lv_obj_center(up_lbl);
    lv_obj_add_event_cb(ui->video_btn_up, video_btn_up_cb,
                        LV_EVENT_CLICKED, ui);

    /* Down button */

    ui->video_btn_down = lv_btn_create(video_btn_panel);
    lv_obj_set_size(ui->video_btn_down, btn_w, 28);
    qiban_apply_button_style(ui->video_btn_down, lv_color_hex(0x1e293b));
    lv_obj_align(ui->video_btn_down, LV_ALIGN_TOP_LEFT,
                 (btn_w + 2) * 2, 0);
    lv_obj_t *down_lbl = lv_label_create(ui->video_btn_down);
    lv_label_set_text(down_lbl, LV_SYMBOL_DOWN);
    lv_obj_center(down_lbl);
    lv_obj_add_event_cb(ui->video_btn_down, video_btn_down_cb,
                        LV_EVENT_CLICKED, ui);

    /* Play/Enter button */

    ui->video_btn_play = lv_btn_create(video_btn_panel);
    lv_obj_set_size(ui->video_btn_play, btn_w, 28);
    qiban_apply_button_style(ui->video_btn_play, lv_color_hex(0x166534));
    lv_obj_align(ui->video_btn_play, LV_ALIGN_TOP_LEFT,
                 (btn_w + 2) * 3, 0);
    lv_obj_t *play_lbl = lv_label_create(ui->video_btn_play);
    lv_label_set_text(play_lbl, LV_SYMBOL_PLAY);
    lv_obj_center(play_lbl);
    lv_obj_add_event_cb(ui->video_btn_play, video_btn_play_cb,
                        LV_EVENT_CLICKED, ui);

    /* Initialize browser */

    ui->video_browser_top = 0;
    ui->video_browser_sel = 0;
    ui->video_browser_count = 0;
    snprintf(ui->video_browser_cwd, sizeof(ui->video_browser_cwd),
             "/sdcard");
  }

  lv_obj_set_style_text_font(ui->map_page_status_label, QIBAN_CJK_FONT, 0);
  lv_obj_set_style_text_font(ui->alert_label, QIBAN_CJK_FONT, 0);

  ui->active_page = 0;
  qiban_ui_update_page_indicator(ui);
  lv_tileview_set_tile(ui->tileview, ui->dashboard_tile, LV_ANIM_OFF);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  struct qiban_ui_s ui;
  lv_timer_t *timer;
  lv_timer_t *pinch_timer;
  uint32_t idle;

  (void)argc;
  (void)argv;

  if (lv_is_initialized())
    {
      LV_LOG_ERROR("LVGL already initialized! aborting.");
      return -1;
    }

#ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  memset(&ui, 0, sizeof(ui));
  qiban_state_set_defaults(&ui.state);
  qiban_map_set_defaults(&ui.map_state);
  ui.touch_pinch_fd = -1;
  g_qiban_ui = &ui;

  lv_init();
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = QIBAN_INPUT_PATH;
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      LV_LOG_ERROR("qiban_ui display initialization failure!");
      return 1;
    }

  lv_obj_set_style_text_font(lv_screen_active(), QIBAN_CJK_FONT, 0);

  qiban_ui_create(&ui);
  qiban_ui_refresh_labels(&ui);

  timer = lv_timer_create(qiban_ui_timer_cb, QIBAN_REFRESH_INTERVAL_MS, &ui);
  if (timer == NULL)
    {
      LV_LOG_ERROR("qiban_ui timer creation failed!");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 1;
    }

#ifdef CONFIG_INPUT_TOUCHSCREEN
  ui.touch_pinch_fd = open(QIBAN_INPUT_PATH, O_RDONLY | O_NONBLOCK);
  if (ui.touch_pinch_fd < 0)
    {
      LV_LOG_WARN("qiban_ui pinch touch open failed, "
                  "two-finger zoom/pan disabled");
    }
#endif

  pinch_timer = lv_timer_create(qiban_ui_pinch_timer_cb,
                                QIBAN_PINCH_TIMER_INTERVAL_MS, &ui);
  if (pinch_timer == NULL)
    {
      LV_LOG_WARN("qiban_ui pinch timer creation failed, "
                  "two-finger zoom/pan disabled");
    }

  while (1)
    {
      idle = lv_timer_handler();
      if (idle == 0)
        {
          idle = 1;
        }

      usleep(idle * 1000);
    }

  return 0;
}
