#pragma once
#include <stdbool.h>
#include <stdint.h>

// 32 * 8 bytes = 256, exactly one persist key (PERSIST_DATA_MAX_LENGTH)
#define MAX_ENTRIES 32
#define FIT_WINDOW 3  // most recent logs used for the ETA fit

typedef struct {
  int32_t t;  // unix seconds when logged
  int32_t n;  // queue position at that time
} Entry;

// Least-squares line: n = intercept + slope * minutes_since(e[0].t)
typedef struct {
  bool valid;
  float slope;      // queue numbers per minute (negative when counting down)
  float intercept;
} Fit;

static inline float fit_x(const Entry *e, int i) {
  return (float)(e[i].t - e[0].t) / 60.0f;
}

// Only the most recent `window` entries are used, so the ETA tracks the current pace.
// x stays relative to e[0] so the result lines up with the full log on the graph.
static inline Fit fit_line(const Entry *e, int count, int window) {
  Fit f = { false, 0.0f, 0.0f };
  int start = count > window ? count - window : 0;
  int used = count - start;
  if (used < 2) return f;
  float mx = 0, my = 0;
  for (int i = start; i < count; i++) { mx += fit_x(e, i); my += (float)e[i].n; }
  mx /= used; my /= used;
  float sxx = 0, sxy = 0;
  for (int i = start; i < count; i++) {
    float dx = fit_x(e, i) - mx;
    sxx += dx * dx;
    sxy += dx * ((float)e[i].n - my);
  }
  if (sxx < 1e-6f) return f;  // all logged within the same instant
  f.slope = sxy / sxx;
  f.intercept = my - f.slope * mx;
  f.valid = true;
  return f;
}

// Minutes since e[0].t at which the line reaches `target`, whether the queue position
// counts up or down. False if the line is flat. Callers check "already reached" first.
static inline bool fit_eta_x(Fit f, int32_t target, float *x_out) {
  if (!f.valid || (f.slope < 1e-4f && f.slope > -1e-4f)) return false;
  float x = ((float)target - f.intercept) / f.slope;
  if (x > 7 * 24 * 60) return false;  // absurdly far out
  *x_out = x;
  return true;
}
