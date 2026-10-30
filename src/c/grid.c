#include "grid.h"

#define ROWS 4
#define COLS 3
#define MAX_DIGITS 4

static const char *KEYS[ROWS][COLS] = {
  { "1", "2", "3" }, { "4", "5", "6" }, { "7", "8", "9" }, { "<", "0", "OK" },
};

typedef enum { STAGE_ROW, STAGE_COL } Stage;

static Window *s_win;
static Layer *s_canvas;
static GridDoneCallback s_done;
static char s_title[24];
static char s_digits[MAX_DIGITS + 1];
static Stage s_stage;
static int s_row, s_col;

typedef struct { int gx, top, cw, ch; } Geom;

static Geom geom(GRect b) {
  Geom g;
  g.gx = PBL_IF_ROUND_ELSE(24, 0);
  g.top = PBL_IF_ROUND_ELSE(44, 40);
  g.cw = (b.size.w - 2 * g.gx) / COLS;
  g.ch = (b.size.h - g.top - PBL_IF_ROUND_ELSE(10, 0)) / ROWS;
  return g;
}

static void press_key(void) {
  const char *k = KEYS[s_row][s_col];
  size_t len = strlen(s_digits);
  if (k[0] >= '0' && k[0] <= '9') {
    if (len < MAX_DIGITS && !(len == 0 && k[0] == '0')) { s_digits[len] = k[0]; s_digits[len + 1] = '\0'; }
    else if (len == 0) { s_digits[0] = '0'; s_digits[1] = '\0'; }
    s_stage = STAGE_ROW;
  } else if (k[0] == '<') {
    if (len > 0) s_digits[len - 1] = '\0';
    s_stage = STAGE_ROW;
  } else if (len > 0) {  // OK
    GridDoneCallback cb = s_done;
    int32_t v = atoi(s_digits);
    window_stack_pop(true);
    if (cb) cb(v);
    return;
  } else {
    window_stack_pop(true);  // OK with nothing typed = cancel
    return;
  }
}

static void step(int dir) {
  if (s_stage == STAGE_ROW) s_row = (s_row + dir + ROWS) % ROWS;
  else s_col = (s_col + dir + COLS) % COLS;
  layer_mark_dirty(s_canvas);
}

static void up_h(ClickRecognizerRef r, void *c) { step(-1); }
static void down_h(ClickRecognizerRef r, void *c) { step(+1); }

static void select_h(ClickRecognizerRef r, void *c) {
  if (s_stage == STAGE_ROW) { s_stage = STAGE_COL; s_col = 0; }
  else press_key();
  if (window_stack_get_top_window() == s_win) layer_mark_dirty(s_canvas);
}

static void back_h(ClickRecognizerRef r, void *c) {
  if (s_stage == STAGE_COL) { s_stage = STAGE_ROW; layer_mark_dirty(s_canvas); }
  else window_stack_pop(true);
}

#ifdef PBL_TOUCH
// Tap a key directly (Pebble Time 2). Buttons keep working as the fallback.
static void touch_handler(const TouchEvent *e, void *ctx) {
  if (e->type != TouchEvent_Touchdown || !s_win) return;
  Geom g = geom(layer_get_bounds(s_canvas));
  int c = (e->x - g.gx) / g.cw, r = (e->y - g.top) / g.ch;
  if (e->x < g.gx || e->y < g.top || c >= COLS || r >= ROWS) return;
  s_row = r; s_col = c;
  press_key();
  if (s_win) layer_mark_dirty(s_canvas);
}
#endif

static void click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 250, up_h);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 250, down_h);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_h);
  window_single_click_subscribe(BUTTON_ID_BACK, back_h);
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  Geom g = geom(b);
  int gx = g.gx, top = g.top, cw = g.cw, ch = g.ch;

  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, s_title, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(0, PBL_IF_ROUND_ELSE(14, 0), b.size.w, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_digits[0] ? s_digits : "_",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, PBL_IF_ROUND_ELSE(24, 12), b.size.w, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  GColor accent = PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite);
  for (int r = 0; r < ROWS; r++) {
    if (s_stage == STAGE_ROW && r == s_row) {
      graphics_context_set_fill_color(ctx, accent);
      graphics_fill_rect(ctx, GRect(gx + 1, top + r * ch + 1, COLS * cw - 2, ch - 2), 4, GCornersAll);
    }
    for (int c = 0; c < COLS; c++) {
      GRect cell = GRect(gx + c * cw, top + r * ch, cw, ch);
      bool row_hl = (s_stage == STAGE_ROW && r == s_row);
      bool key_hl = (s_stage == STAGE_COL && r == s_row && c == s_col);
      if (key_hl) {
        graphics_context_set_fill_color(ctx, accent);
        graphics_fill_rect(ctx, grect_crop(cell, 2), 4, GCornersAll);
      } else if (!row_hl) {
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_draw_round_rect(ctx, grect_crop(cell, 2), 4);
      }
      graphics_context_set_text_color(ctx, (row_hl || key_hl) ? GColorBlack : GColorWhite);
      graphics_draw_text(ctx, KEYS[r][c], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                         GRect(cell.origin.x, cell.origin.y + (ch - 28) / 2 - 1, cw, 28),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
}

#ifdef PBL_TOUCH
// Claim the touch service while visible; the main window re-claims it on appear.
static void appear(Window *w) {
  touch_service_subscribe(touch_handler, NULL);
}
#endif

static void unload(Window *w) {
  layer_destroy(s_canvas);
  window_destroy(w);
  s_win = NULL;
}

void grid_push(const char *title, int32_t initial, GridDoneCallback done) {
  if (s_win) return;
  s_done = done;
  strncpy(s_title, title, sizeof(s_title) - 1);
  s_title[sizeof(s_title) - 1] = '\0';
  snprintf(s_digits, sizeof(s_digits), "%d", (int)initial);
  if (initial <= 0) s_digits[0] = '\0';
  s_stage = STAGE_ROW;
  s_row = 0; s_col = 0;
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_click_config_provider(s_win, click_config);
  window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .unload = unload,
#ifdef PBL_TOUCH
    .appear = appear,
#endif
  });
  window_stack_push(s_win, true);
}
