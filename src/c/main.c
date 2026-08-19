#include <pebble.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// RuneScape-themed watchface for Pebble Time 2 (emery platform, 200x228).
//
// Everything visible is drawn procedurally (no bitmap assets) so the whole
// thing is original vector artwork inspired by the game's classic stone /
// gold interface, minimap orbs and a firemaking campfire -- it does not use
// any Jagex-owned images, fonts or text.
// ---------------------------------------------------------------------------

static Window *s_window;

static Layer *s_bg_layer;
static Layer *s_orb_layer;
static Layer *s_fire_layer;
static Layer *s_weather_icon_layer;

static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_weather_text_layer;

static char s_time_buffer[8];
static char s_date_buffer[16];
static char s_weather_buffer[8];

static AppTimer *s_fire_timer;
static int s_fire_frame = 0;

static bool s_weather_available = false;
static int s_weather_temp = 0;
static int s_weather_cond = 0; // 0 clear, 1 cloud, 2 rain, 3 snow, 4 storm

#define PERSIST_KEY_TEMP 0
#define PERSIST_KEY_COND 1
#define PERSIST_KEY_HAS_WEATHER 2

#define FIRE_ANIMATION_INTERVAL_MS 220
#define WEATHER_REFRESH_MINUTES 30

// Layout constants, tuned for the emery (Pebble Time 2) 200x228 display.
#define ORB_TOP_Y 18
#define ORB_BOTTOM_Y 42
#define CLOCK_PANEL_Y 54
#define CLOCK_PANEL_H 84
#define TIME_Y 58
#define TIME_H 54
#define DATE_Y 114
#define DATE_H 22
#define WEATHER_PANEL_Y 146
#define WEATHER_PANEL_H 34
#define WEATHER_ICON_Y 148
#define WEATHER_TEXT_Y 150
#define FIRE_Y 184
#define FIRE_H 40

// ---------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------

// Draws a black panel with a two-tone gold border, RuneScape interface style.
static void draw_panel(GContext *ctx, GRect rect, uint16_t radius) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, rect, radius, GCornersAll);

  graphics_context_set_stroke_color(ctx, GColorChromeYellow);
  graphics_draw_round_rect(ctx, rect, radius);

  GRect inner = grect_inset(rect, GEdgeInsets(1));
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_draw_round_rect(ctx, inner, radius > 1 ? radius - 1 : 0);
}

// A minimap-style orb: filled circle, "drained" from the top by `percent`.
static void draw_orb(GContext *ctx, GPoint center, int radius, GColor color, float percent) {
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 1.0f) percent = 1.0f;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, radius);

  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, center, radius - 1);

  if (percent < 1.0f) {
    int empty_h = (int)((1.0f - percent) * (float)(radius * 2));
    GRect mask = GRect(center.x - radius - 1, center.y - radius, radius * 2 + 2, empty_h);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, mask, 0, GCornerNone);
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_circle(ctx, center, radius);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_circle(ctx, center, radius - 2);
}

// ---------------------------------------------------------------------------
// Background layer: stone-block wall + gold frame + text panels
// ---------------------------------------------------------------------------

static void bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Offset-brick stonework, like an RS dungeon wall.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  const int brick_h = 14;
  const int brick_w = 26;
  for (int y = 0; y < bounds.size.h; y += brick_h) {
    graphics_draw_line(ctx, GPoint(0, y), GPoint(bounds.size.w, y));
    int row = y / brick_h;
    int offset = (row % 2 == 0) ? 0 : brick_w / 2;
    for (int x = -brick_w + offset; x < bounds.size.w; x += brick_w) {
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x, y + brick_h));
    }
  }

  // Outer gold frame.
  graphics_context_set_stroke_color(ctx, GColorChromeYellow);
  graphics_draw_rect(ctx, GRect(1, 1, bounds.size.w - 2, bounds.size.h - 2));
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_draw_rect(ctx, GRect(3, 3, bounds.size.w - 6, bounds.size.h - 6));

  // Panel behind the clock.
  draw_panel(ctx, GRect(10, CLOCK_PANEL_Y, bounds.size.w - 20, CLOCK_PANEL_H), 8);

  // Panel behind the weather row.
  draw_panel(ctx, GRect(10, WEATHER_PANEL_Y, bounds.size.w - 20, WEATHER_PANEL_H), 8);
}

// ---------------------------------------------------------------------------
// Orb layer: HP orb = battery, Prayer orb = bluetooth, plus two decorative
// orbs, mirroring the classic RuneScape minimap HUD.
// ---------------------------------------------------------------------------

static void orb_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int radius = 10;

  BatteryChargeState battery = battery_state_service_peek();
  bool bt_connected = connection_service_peek_pebble_app_connection();

  // Left column: Hitpoints (battery) above Prayer (bluetooth).
  draw_orb(ctx, GPoint(radius + 4, ORB_TOP_Y), radius, GColorRed, battery.charge_percent / 100.0f);
  draw_orb(ctx, GPoint(radius + 4, ORB_BOTTOM_Y), radius, GColorPictonBlue, bt_connected ? 1.0f : 0.15f);

  // Right column: Run energy and Special attack (decorative, always full).
  draw_orb(ctx, GPoint(bounds.size.w - radius - 4, ORB_TOP_Y), radius, GColorIslamicGreen, 1.0f);
  draw_orb(ctx, GPoint(bounds.size.w - radius - 4, ORB_BOTTOM_Y), radius, GColorRajah, 1.0f);
}

// ---------------------------------------------------------------------------
// Fire layer: small animated campfire, RuneScape Firemaking style.
// ---------------------------------------------------------------------------

static void fire_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint base = GPoint(bounds.size.w / 2, bounds.size.h - 4);

  int flicker = s_fire_frame % 3;
  int flicker2 = (s_fire_frame + 1) % 4;

  // Logs.
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_fill_rect(ctx, GRect(base.x - 15, base.y - 2, 30, 4), 1, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, GRect(base.x - 15, base.y - 2, 30, 4));

  // Outer flame.
  GPoint outer_pts[] = {
    { base.x - 10, base.y - 2 },
    { base.x - 9, base.y - 12 },
    { base.x - 3 - flicker, base.y - 20 - flicker },
    { base.x, base.y - 26 - (flicker2 * 2) },
    { base.x + 3 + flicker, base.y - 20 - flicker },
    { base.x + 9, base.y - 12 },
    { base.x + 10, base.y - 2 },
  };
  GPathInfo outer_info = { .num_points = 7, .points = outer_pts };
  GPath *outer_path = gpath_create(&outer_info);
  graphics_context_set_fill_color(ctx, GColorRed);
  gpath_draw_filled(ctx, outer_path);
  gpath_destroy(outer_path);

  // Middle flame.
  GPoint mid_pts[] = {
    { base.x - 6, base.y - 2 },
    { base.x - 2, base.y - 14 - flicker2 },
    { base.x, base.y - 18 - flicker },
    { base.x + 2, base.y - 14 - flicker2 },
    { base.x + 6, base.y - 2 },
  };
  GPathInfo mid_info = { .num_points = 5, .points = mid_pts };
  GPath *mid_path = gpath_create(&mid_info);
  graphics_context_set_fill_color(ctx, GColorOrange);
  gpath_draw_filled(ctx, mid_path);
  gpath_destroy(mid_path);

  // Inner flame core.
  GPoint inner_pts[] = {
    { base.x - 3, base.y - 2 },
    { base.x, base.y - 10 - flicker },
    { base.x + 3, base.y - 2 },
  };
  GPathInfo inner_info = { .num_points = 3, .points = inner_pts };
  GPath *inner_path = gpath_create(&inner_info);
  graphics_context_set_fill_color(ctx, GColorChromeYellow);
  gpath_draw_filled(ctx, inner_path);
  gpath_destroy(inner_path);
}

static void fire_timer_callback(void *data) {
  s_fire_frame++;
  if (s_fire_layer) {
    layer_mark_dirty(s_fire_layer);
  }
  s_fire_timer = app_timer_register(FIRE_ANIMATION_INTERVAL_MS, fire_timer_callback, NULL);
}

// ---------------------------------------------------------------------------
// Weather icon layer
// ---------------------------------------------------------------------------

static void weather_icon_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint c = GPoint(bounds.size.w / 2, bounds.size.h / 2);

  if (!s_weather_available) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_circle(ctx, c, 9);
    graphics_draw_text(ctx, "?", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                        GRect(bounds.origin.x, bounds.origin.y - 3, bounds.size.w, bounds.size.h),
                        GTextAlignmentCenter, GTextOverflowModeFill, NULL);
    return;
  }

  switch (s_weather_cond) {
    case 0: { // clear
      graphics_context_set_fill_color(ctx, GColorChromeYellow);
      graphics_fill_circle(ctx, c, 7);
      graphics_context_set_stroke_color(ctx, GColorChromeYellow);
      for (int i = 0; i < 8; i++) {
        int32_t angle = (TRIG_MAX_ANGLE * i) / 8;
        GPoint p1 = GPoint(c.x + (sin_lookup(angle) * 10) / TRIG_MAX_RATIO,
                            c.y - (cos_lookup(angle) * 10) / TRIG_MAX_RATIO);
        GPoint p2 = GPoint(c.x + (sin_lookup(angle) * 15) / TRIG_MAX_RATIO,
                            c.y - (cos_lookup(angle) * 15) / TRIG_MAX_RATIO);
        graphics_draw_line(ctx, p1, p2);
      }
      break;
    }
    case 1: { // clouds
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(c.x - 6, c.y + 3), 6);
      graphics_fill_circle(ctx, GPoint(c.x + 6, c.y + 3), 6);
      graphics_fill_circle(ctx, GPoint(c.x, c.y - 3), 7);
      break;
    }
    case 2: { // rain
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(c.x - 5, c.y - 3), 5);
      graphics_fill_circle(ctx, GPoint(c.x + 5, c.y - 3), 5);
      graphics_fill_circle(ctx, GPoint(c.x, c.y - 8), 6);
      graphics_context_set_stroke_color(ctx, GColorPictonBlue);
      graphics_draw_line(ctx, GPoint(c.x - 6, c.y + 4), GPoint(c.x - 9, c.y + 11));
      graphics_draw_line(ctx, GPoint(c.x, c.y + 4), GPoint(c.x - 3, c.y + 11));
      graphics_draw_line(ctx, GPoint(c.x + 6, c.y + 4), GPoint(c.x + 3, c.y + 11));
      break;
    }
    case 3: { // snow
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(c.x - 5, c.y - 3), 5);
      graphics_fill_circle(ctx, GPoint(c.x + 5, c.y - 3), 5);
      graphics_fill_circle(ctx, GPoint(c.x, c.y - 8), 6);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(c.x - 7, c.y + 9), 1);
      graphics_fill_circle(ctx, GPoint(c.x, c.y + 11), 1);
      graphics_fill_circle(ctx, GPoint(c.x + 7, c.y + 9), 1);
      break;
    }
    case 4: { // storm
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_circle(ctx, GPoint(c.x - 5, c.y - 5), 5);
      graphics_fill_circle(ctx, GPoint(c.x + 5, c.y - 5), 5);
      graphics_fill_circle(ctx, GPoint(c.x, c.y - 10), 6);
      GPoint bolt_pts[] = {
        { c.x + 2, c.y - 1 }, { c.x - 4, c.y + 7 }, { c.x, c.y + 7 },
        { c.x - 2, c.y + 15 }, { c.x + 6, c.y + 5 }, { c.x + 2, c.y + 5 },
      };
      GPathInfo bolt_info = { .num_points = 6, .points = bolt_pts };
      GPath *bolt_path = gpath_create(&bolt_info);
      graphics_context_set_fill_color(ctx, GColorChromeYellow);
      gpath_draw_filled(ctx, bolt_path);
      gpath_destroy(bolt_path);
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Time / date / weather text updates
// ---------------------------------------------------------------------------

static void update_time_and_date(void) {
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);

  strftime(s_time_buffer, sizeof(s_time_buffer),
            clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d %b", tick_time);
  for (char *p = s_date_buffer; *p; p++) {
    *p = (char)toupper((unsigned char)*p);
  }
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void update_weather_text(void) {
  if (s_weather_available) {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d°C", s_weather_temp);
  } else {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "--");
  }
  text_layer_set_text(s_weather_text_layer, s_weather_buffer);
  if (s_weather_icon_layer) {
    layer_mark_dirty(s_weather_icon_layer);
  }
}

static void request_weather_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time_and_date();
  if (tick_time->tm_min % WEATHER_REFRESH_MINUTES == 0) {
    request_weather_update();
  }
}

// ---------------------------------------------------------------------------
// Battery / bluetooth service handlers (redraw the orb HUD)
// ---------------------------------------------------------------------------

static void battery_handler(BatteryChargeState charge) {
  if (s_orb_layer) layer_mark_dirty(s_orb_layer);
}

static void connection_handler(bool connected) {
  if (s_orb_layer) layer_mark_dirty(s_orb_layer);
}

// ---------------------------------------------------------------------------
// AppMessage: receive weather data from PebbleKit JS
// ---------------------------------------------------------------------------

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_TEMP);
  Tuple *cond_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_COND);

  if (temp_tuple && cond_tuple) {
    s_weather_temp = (int)temp_tuple->value->int32;
    s_weather_cond = (int)cond_tuple->value->int32;
    s_weather_available = true;

    persist_write_int(PERSIST_KEY_TEMP, s_weather_temp);
    persist_write_int(PERSIST_KEY_COND, s_weather_cond);
    persist_write_bool(PERSIST_KEY_HAS_WEATHER, true);

    update_weather_text();
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Weather message dropped, reason: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Weather request failed, reason: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_update_proc);
  layer_add_child(root, s_bg_layer);

  s_orb_layer = layer_create(bounds);
  layer_set_update_proc(s_orb_layer, orb_update_proc);
  layer_add_child(root, s_orb_layer);

  s_time_layer = text_layer_create(GRect(10, TIME_Y, bounds.size.w - 20, TIME_H));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorChromeYellow);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  s_date_layer = text_layer_create(GRect(10, DATE_Y, bounds.size.w - 20, DATE_H));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  s_weather_icon_layer = layer_create(GRect(18, WEATHER_ICON_Y, 30, 30));
  layer_set_update_proc(s_weather_icon_layer, weather_icon_update_proc);
  layer_add_child(root, s_weather_icon_layer);

  s_weather_text_layer = text_layer_create(GRect(52, WEATHER_TEXT_Y, bounds.size.w - 70, 26));
  text_layer_set_background_color(s_weather_text_layer, GColorClear);
  text_layer_set_text_color(s_weather_text_layer, GColorChromeYellow);
  text_layer_set_font(s_weather_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_weather_text_layer, GTextAlignmentLeft);
  layer_add_child(root, text_layer_get_layer(s_weather_text_layer));

  s_fire_layer = layer_create(GRect(bounds.size.w / 2 - 20, FIRE_Y, 40, FIRE_H));
  layer_set_update_proc(s_fire_layer, fire_update_proc);
  layer_add_child(root, s_fire_layer);

  update_time_and_date();
  update_weather_text();

  s_fire_timer = app_timer_register(FIRE_ANIMATION_INTERVAL_MS, fire_timer_callback, NULL);
}

static void window_unload(Window *window) {
  if (s_fire_timer) {
    app_timer_cancel(s_fire_timer);
    s_fire_timer = NULL;
  }

  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weather_text_layer);

  layer_destroy(s_bg_layer);
  layer_destroy(s_orb_layer);
  layer_destroy(s_fire_layer);
  layer_destroy(s_weather_icon_layer);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  if (persist_read_bool(PERSIST_KEY_HAS_WEATHER)) {
    s_weather_temp = persist_read_int(PERSIST_KEY_TEMP);
    s_weather_cond = persist_read_int(PERSIST_KEY_COND);
    s_weather_available = true;
  }

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler,
  });

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  request_weather_update();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
