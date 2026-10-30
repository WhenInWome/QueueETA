#include <pebble.h>
#include "fit.h"
#include "grid.h"

// Keys 1/2 held an older layout that also stored a target number; use fresh keys.
#define PERSIST_META 3
#define PERSIST_LOG 4
#define PERSIST_SETTINGS 5  // Settings
#define PERSIST_WAKEUP 6    // WakeupId of the pending alarm
#define MAX_ALERT_MIN 999
#define MAX_NUMBER 9999
#define TARGET 0  // front of the line: the position counts down to this

typedef struct {
  int32_t current;  // position being edited / last logged
  int32_t count;    // entries in log
} Meta;

typedef struct {
  int32_t alert_min;  // vibrate this many minutes before ETA; 0 = off
  int32_t fired;      // 1 once this ETA's alert has gone off, so logging again doesn't re-buzz
  int32_t last_day;   // day key of the last launch (0 = unknown); a new day starts a clear queue
} Settings;

static Window *s_window;
static TextLayer *s_status, *s_number, *s_eta, *s_sub;
static Layer *s_graph;

static Meta s_meta;
static Settings s_set;
static Entry s_log[MAX_ENTRIES];
static Fit s_fit;
static bool s_has_eta;
static float s_eta_x;

static char s_status_buf[40], s_number_buf[8], s_eta_buf[24], s_sub_buf[40];

// ---------- persistence ----------

static void save_state(void) {
  persist_write_data(PERSIST_META, &s_meta, sizeof(s_meta));
  persist_write_data(PERSIST_LOG, s_log, sizeof(s_log));
  persist_write_data(PERSIST_SETTINGS, &s_set, sizeof(s_set));
}

static bool load_state(void) {
  if (persist_exists(PERSIST_SETTINGS)) persist_read_data(PERSIST_SETTINGS, &s_set, sizeof(s_set));
  if (!persist_exists(PERSIST_META)) return false;
  persist_read_data(PERSIST_META, &s_meta, sizeof(s_meta));
  if (persist_exists(PERSIST_LOG)) persist_read_data(PERSIST_LOG, s_log, sizeof(s_log));
  if (s_meta.count < 0 || s_meta.count > MAX_ENTRIES) s_meta.count = 0;
  return true;
}

// ---------- model ----------

static void recompute(void) {
  s_fit = fit_line(s_log, s_meta.count, FIT_WINDOW);
  s_has_eta = fit_eta_x(s_fit, TARGET, &s_eta_x) && s_fit.slope < 0;
}

static bool reached_target(void) {
  return s_meta.count > 0 && s_log[s_meta.count - 1].n <= TARGET;
}

// ---------- alert ----------

static const uint32_t ALERT_SEGMENTS[] = { 400, 150, 400, 150, 400 };
static const VibePattern ALERT_VIBE = { .durations = ALERT_SEGMENTS, .num_segments = 5 };

static time_t eta_time(void) {
  return (time_t)s_log[0].t + (time_t)(s_eta_x * 60.0f);
}

// Cancel any pending wakeup, then schedule one alert_min before the current ETA.
// Called whenever the ETA or the setting changes. A wakeup relaunches the app, so the
// alert still fires if the app was closed.
static void reschedule_alert(void) {
  if (persist_exists(PERSIST_WAKEUP)) {
    wakeup_cancel(persist_read_int(PERSIST_WAKEUP));
    persist_delete(PERSIST_WAKEUP);
  }
  if (s_set.alert_min <= 0 || !s_has_eta || reached_target()) return;

  time_t now = time(NULL);
  time_t alarm_t = eta_time() - (time_t)s_set.alert_min * 60;
  if (alarm_t <= now + 5) {  // already inside the alert window
    if (!s_set.fired) { s_set.fired = 1; vibes_enqueue_custom_pattern(ALERT_VIBE); }
    return;
  }
  s_set.fired = 0;  // ETA moved out again: re-arm
  WakeupId id = wakeup_schedule(alarm_t, 0, false);
  if (id >= 0) persist_write_int(PERSIST_WAKEUP, id);
}

// Wipe the queue. Also drops any pending alert.
static void reset_queue(void) {
  s_meta.count = 0;
  s_meta.current = 0;
  s_set.fired = 0;
  memset(s_log, 0, sizeof(s_log));
  recompute();
  reschedule_alert();
  save_state();
}

static int32_t today_key(void) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  return tm->tm_year * 400 + tm->tm_yday + 1;  // never 0
}

static void wakeup_handler(WakeupId id, int32_t cookie) {
  persist_delete(PERSIST_WAKEUP);
  s_set.fired = 1;
  persist_write_data(PERSIST_SETTINGS, &s_set, sizeof(s_set));
  vibes_enqueue_custom_pattern(ALERT_VIBE);
  light_enable_interaction();
}

static void log_current(void) {
  if (s_meta.count == MAX_ENTRIES) {  // drop oldest; recent pace matters most
    memmove(&s_log[0], &s_log[1], sizeof(Entry) * (MAX_ENTRIES - 1));
    s_meta.count--;
  }
  s_log[s_meta.count++] = (Entry){ (int32_t)time(NULL), s_meta.current };
  recompute();
  reschedule_alert();
  save_state();
  if (reached_target()) vibes_long_pulse(); else vibes_short_pulse();
}

// ---------- display ----------

static void format_clock(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  int h = tm->tm_hour % 12;
  snprintf(buf, len, "%d:%02d %s", h == 0 ? 12 : h, tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
}

static void update_display(void) {
  {
    snprintf(s_status_buf, sizeof(s_status_buf), "Queue position  |  %d logged", (int)s_meta.count);
    snprintf(s_number_buf, sizeof(s_number_buf), "%d", (int)s_meta.current);

    if (reached_target()) {
      snprintf(s_eta_buf, sizeof(s_eta_buf), "YOUR TURN!");
      s_sub_buf[0] = '\0';
    } else if (s_has_eta) {
      time_t eta_t = eta_time();
      char clock[12];
      format_clock(eta_t, clock, sizeof(clock));
      snprintf(s_eta_buf, sizeof(s_eta_buf), "ETA %s", clock);

      int rem = (int)((eta_t - time(NULL)) / 60);
      int rate10 = (int)(-s_fit.slope * 10.0f + 0.5f);
      char rem_buf[16];
      if (rem <= 0) snprintf(rem_buf, sizeof(rem_buf), "now-ish");
      else if (rem < 60) snprintf(rem_buf, sizeof(rem_buf), "%dm", rem);
      else snprintf(rem_buf, sizeof(rem_buf), "%dh %02dm", rem / 60, rem % 60);
      snprintf(s_sub_buf, sizeof(s_sub_buf), "in %s | %d.%d/min", rem_buf, rate10 / 10, rate10 % 10);
    } else if (s_meta.count < 2) {
      snprintf(s_eta_buf, sizeof(s_eta_buf), "ETA --");
      snprintf(s_sub_buf, sizeof(s_sub_buf), "Log 2+ numbers");
    } else if (s_fit.valid && s_fit.slope > 1e-4f) {
      snprintf(s_eta_buf, sizeof(s_eta_buf), "ETA --");
      snprintf(s_sub_buf, sizeof(s_sub_buf), "Position is going up");
    } else {
      snprintf(s_eta_buf, sizeof(s_eta_buf), "ETA --");
      snprintf(s_sub_buf, sizeof(s_sub_buf), "Line not moving yet");
    }
  }
  text_layer_set_text(s_status, s_status_buf);
  text_layer_set_text(s_number, s_number_buf);
  text_layer_set_text(s_eta, s_eta_buf);
  text_layer_set_text(s_sub, s_sub_buf);
  layer_mark_dirty(s_graph);
}

// ---------- big 7-segment clock (scales to the graph box; no font resource needed) ----------

//                         a=top b=upper-right c=lower-right d=bottom e=lower-left f=upper-left g=middle
static const uint8_t SEG[10] = {  // bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

static void draw_digit(GContext *ctx, int x, int y, int w, int h, int t, int d) {
  uint8_t s = SEG[d];
  int mid = y + (h - t) / 2;
  if (s & 1)  graphics_fill_rect(ctx, GRect(x, y, w, t), 0, GCornerNone);
  if (s & 2)  graphics_fill_rect(ctx, GRect(x + w - t, y, t, mid - y + t), 0, GCornerNone);
  if (s & 4)  graphics_fill_rect(ctx, GRect(x + w - t, mid, t, y + h - mid), 0, GCornerNone);
  if (s & 8)  graphics_fill_rect(ctx, GRect(x, y + h - t, w, t), 0, GCornerNone);
  if (s & 16) graphics_fill_rect(ctx, GRect(x, mid, t, y + h - mid), 0, GCornerNone);
  if (s & 32) graphics_fill_rect(ctx, GRect(x, y, t, mid - y + t), 0, GCornerNone);
  if (s & 64) graphics_fill_rect(ctx, GRect(x, mid, w, t), 0, GCornerNone);
}

// Current time, 12-hour, drawn grey as a watermark filling the graph box.
static void draw_clock_watermark(GContext *ctx, GRect b) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  int h12 = tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12;
  int digits[4], nd = 0;
  if (h12 >= 10) digits[nd++] = h12 / 10;
  digits[nd++] = h12 % 10;
  digits[nd++] = tm->tm_min / 10;
  digits[nd++] = tm->tm_min % 10;

  const int ampm_w = 22, margin = 10;  // 10px clear border around the time
  int avail_w = b.size.w - 2 * margin - ampm_w;
  int dh = b.size.h - 2 * margin;
  int dw, t, gap, total;
  for (int pass = 0; pass < 2; pass++) {  // second pass shrinks to fit the width
    t = dh / 8 < 2 ? 2 : dh / 8;
    dw = dh * 4 / 9;
    gap = t;
    total = nd * dw + t + gap * nd;  // digits + colon, with a gap between each of the nd+1 items
    if (total <= avail_w) break;
    dh = dh * avail_w / total;
  }

  graphics_context_set_fill_color(ctx, GColorDarkGray);
  int x = margin + (avail_w - total) / 2, y = (b.size.h - dh) / 2;
  for (int i = 0; i < nd; i++) {
    if (i == nd - 2) {  // colon before the minutes
      graphics_fill_rect(ctx, GRect(x, y + dh / 3 - t / 2, t, t), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x, y + dh * 2 / 3 - t / 2, t, t), 0, GCornerNone);
      x += t + gap;
    }
    draw_digit(ctx, x, y, dw, dh, t, digits[i]);
    x += dw + gap;
  }
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, tm->tm_hour < 12 ? "AM" : "PM", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(b.size.w - margin - ampm_w, b.size.h - margin - 20, ampm_w, 20),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void graph_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, b);

  draw_clock_watermark(ctx, b);

  if (s_meta.count < 1) return;

  const int pad = 4;
  int32_t ymin = TARGET, ymax = s_log[0].n;
  for (int i = 0; i < s_meta.count; i++) {
    if (s_log[i].n > ymax) ymax = s_log[i].n;
  }
  if (ymax <= ymin) ymax = ymin + 1;
  float xmax = fit_x(s_log, s_meta.count - 1);
  if (s_has_eta && s_eta_x > xmax) xmax = s_eta_x;
  if (xmax < 1.0f) xmax = 1.0f;

  int w = b.size.w - 2 * pad, h = b.size.h - 2 * pad;
#define PX(x) (pad + (int)((x) / xmax * w))
#define PY(y) (pad + h - (int)((float)((y) - ymin) / (float)(ymax - ymin) * h))

  // front-of-line (dotted)
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite));
  for (int x = pad; x < pad + w; x += 4) {
    graphics_draw_pixel(ctx, GPoint(x, PY(TARGET)));
    graphics_draw_pixel(ctx, GPoint(x + 1, PY(TARGET)));
  }

  // best-fit line
  if (s_fit.valid) {
    float x0 = 0, x1 = xmax;
    float y0 = s_fit.intercept, y1 = s_fit.intercept + s_fit.slope * xmax;
    if (y0 < ymin) { x0 = ((float)ymin - s_fit.intercept) / s_fit.slope; y0 = ymin; }
    if (y1 > ymax) { x1 = ((float)ymax - s_fit.intercept) / s_fit.slope; y1 = ymax; }
    if (x1 > x0) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(PX(x0), PY(y0)), GPoint(PX(x1), PY(y1)));
      graphics_context_set_stroke_width(ctx, 1);
    }
  }

  // logged points
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (int i = 0; i < s_meta.count; i++) {
    graphics_fill_circle(ctx, GPoint(PX(fit_x(s_log, i)), PY(s_log[i].n)), 2);
  }
}

// ---------- input ----------

static void nudge(int dir) {
  static uint32_t last_ms;
  static int repeats;
  time_t s; uint16_t ms;
  time_ms(&s, &ms);
  uint32_t now = (uint32_t)s * 1000u + ms;
  repeats = (now - last_ms > 700) ? 0 : repeats + 1;
  last_ms = now;
  int step = repeats > 40 ? 50 : repeats > 15 ? 10 : 1;

  int32_t nv = s_meta.current + dir * step;
  if (nv < 0) nv = 0;
  if (nv > MAX_NUMBER) nv = MAX_NUMBER;
  s_meta.current = nv;
  update_display();
}

static void up_handler(ClickRecognizerRef r, void *c) { nudge(+1); }
static void down_handler(ClickRecognizerRef r, void *c) { nudge(-1); }

static void select_click(ClickRecognizerRef r, void *c) {
  log_current();
  update_display();
}

// ---------- long-press menu + grid entry ----------

static Window *s_menu_win;
static SimpleMenuLayer *s_menu;
static SimpleMenuSection s_menu_section;
static SimpleMenuItem s_menu_items[3];
static char s_alert_subtitle[24];

static void on_position_typed(int32_t v) {
  s_meta.current = v;
  log_current();
  update_display();
}

static void on_alert_typed(int32_t v) {
  s_set.alert_min = v > MAX_ALERT_MIN ? MAX_ALERT_MIN : v;
  s_set.fired = 0;
  reschedule_alert();
  save_state();
  vibes_short_pulse();
  update_display();
}

static void menu_pick(int index, void *ctx) {
  window_stack_pop(true);  // closes the menu
  if (index == 0) {
    grid_push("NEW QUEUE POSITION", s_meta.current, on_position_typed);
  } else if (index == 1) {
    grid_push("ALERT MIN BEFORE (0=off)", s_set.alert_min, on_alert_typed);
  } else {
    reset_queue();
    vibes_double_pulse();
    update_display();
    grid_push("NEW QUEUE POSITION", 0, on_position_typed);
  }
}

static void menu_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_menu_items[0] = (SimpleMenuItem){ .title = "Type queue position", .subtitle = "number grid", .callback = menu_pick };
  if (s_set.alert_min > 0) snprintf(s_alert_subtitle, sizeof(s_alert_subtitle), "%d min before ETA", (int)s_set.alert_min);
  else snprintf(s_alert_subtitle, sizeof(s_alert_subtitle), "Off");
  s_menu_items[1] = (SimpleMenuItem){ .title = "Vibrate before ETA", .subtitle = s_alert_subtitle, .callback = menu_pick };
  s_menu_items[2] = (SimpleMenuItem){ .title = "Clear log", .subtitle = "start a new queue", .callback = menu_pick };
  s_menu_section = (SimpleMenuSection){ .num_items = 3, .items = s_menu_items };
  s_menu = simple_menu_layer_create(layer_get_bounds(root), w, &s_menu_section, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_menu));
}

static void menu_unload(Window *w) {
  simple_menu_layer_destroy(s_menu);
  window_destroy(w);
  s_menu_win = NULL;
}

static void undo_long(ClickRecognizerRef r, void *c) {
  if (s_meta.count == 0) { vibes_short_pulse(); return; }
  s_meta.count--;
  memset(&s_log[s_meta.count], 0, sizeof(Entry));
  s_meta.current = s_meta.count > 0 ? s_log[s_meta.count - 1].n : 0;
  recompute();
  reschedule_alert();
  save_state();
  vibes_double_pulse();
  update_display();
}

static void select_long(ClickRecognizerRef r, void *c) {
  s_menu_win = window_create();
  window_set_window_handlers(s_menu_win, (WindowHandlers){ .load = menu_load, .unload = menu_unload });
  window_stack_push(s_menu_win, true);
}

#ifdef PBL_TOUCH
// Tap the status line or big number to type a new queue position (logs on OK).
// Ignored while the menu or grid is on top.
static void touch_handler(const TouchEvent *e, void *ctx) {
  if (e->type != TouchEvent_Touchdown || window_stack_get_top_window() != s_window) return;
  GRect nf = layer_get_frame(text_layer_get_layer(s_number));
  if (e->y < nf.origin.y + nf.size.h) {
    grid_push("NEW QUEUE POSITION", 0, on_position_typed);  // start cleared
  }
}

static void window_appear(Window *w) {
  // The touch service has one handler; re-claim it whenever the grid closes.
  touch_service_subscribe(touch_handler, NULL);
}
#endif

static void click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long, NULL);
  window_long_click_subscribe(BUTTON_ID_BACK, 600, undo_long, NULL);  // undo last log
}

static void tick_handler(struct tm *t, TimeUnits u) { update_display(); }

// ---------- window ----------

static TextLayer *make_text(Layer *parent, GRect f, const char *font, GColor color) {
  TextLayer *t = text_layer_create(f);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, color);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  layer_add_child(parent, text_layer_get_layer(t));
  return t;
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  int W = b.size.w, H = b.size.h;
  int inset = PBL_IF_ROUND_ELSE(18, 0);  // keep text clear of round bezel
  int tw = W - 2 * inset;

  window_set_background_color(w, GColorBlack);
  s_status = make_text(root, GRect(inset, PBL_IF_ROUND_ELSE(10, 0), tw, 20), FONT_KEY_GOTHIC_14_BOLD, GColorLightGray);
  s_number = make_text(root, GRect(inset, PBL_IF_ROUND_ELSE(26, 16), tw, 44), FONT_KEY_BITHAM_34_MEDIUM_NUMBERS, GColorWhite);
  s_eta = make_text(root, GRect(inset, PBL_IF_ROUND_ELSE(70, 60), tw, 28), FONT_KEY_GOTHIC_24,  // regular weight: "ETA 12:59 PM" must fit 144px
                    PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite));
  s_sub = make_text(root, GRect(inset, PBL_IF_ROUND_ELSE(96, 86), tw, 20), FONT_KEY_GOTHIC_18, GColorWhite);

  int gy = PBL_IF_ROUND_ELSE(120, 110);
  int gx = PBL_IF_ROUND_ELSE(32, 4);
  s_graph = layer_create(GRect(gx, gy, W - 2 * gx, H - gy - PBL_IF_ROUND_ELSE(14, 4)));
  layer_set_update_proc(s_graph, graph_update);
  layer_add_child(root, s_graph);
}

static void window_unload(Window *w) {
  text_layer_destroy(s_status);
  text_layer_destroy(s_number);
  text_layer_destroy(s_eta);
  text_layer_destroy(s_sub);
  layer_destroy(s_graph);
}

static void init(void) {
  if (!load_state()) {
    s_meta = (Meta){ .current = 0, .count = 0 };
  } else if (s_meta.count > 0) {
    s_meta.current = s_log[s_meta.count - 1].n;
  }
  recompute();

  // First launch of a new calendar day: start from a clear queue. The app records its own
  // launch day, so no idle-time fallback is needed. (last_day == 0 means "unknown": just record it.)
  int32_t today = today_key();
  bool new_day = s_set.last_day != 0 && s_set.last_day != today;
  s_set.last_day = today;
  if (new_day && s_meta.count > 0) reset_queue();
  else save_state();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload,
#ifdef PBL_TOUCH
    .appear = window_appear,
#endif
  });
  window_stack_push(s_window, true);
  update_display();
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  wakeup_service_subscribe(wakeup_handler);
  if (launch_reason() == APP_LAUNCH_WAKEUP) {  // opened by the alert
    WakeupId id; int32_t cookie;
    if (wakeup_get_launch_event(&id, &cookie)) wakeup_handler(id, cookie);
  }
  if (s_meta.count == 0) grid_push("YOUR QUEUE POSITION", 0, on_position_typed);  // first entry
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  save_state();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
