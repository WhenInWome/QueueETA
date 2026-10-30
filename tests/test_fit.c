#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/c/fit.h"

int main(void) {
  // perfect 2 numbers/min starting at 10
  Entry a[4] = {{1000,10},{1060,12},{1120,14},{1180,16}};
  Fit f = fit_line(a, 4, 32);
  assert(f.valid && fabsf(f.slope - 2.0f) < 1e-3f && fabsf(f.intercept - 10.0f) < 1e-3f);
  float x; assert(fit_eta_x(f, 40, &x) && fabsf(x - 15.0f) < 1e-2f);

  // noisy data: slope should be near 1/min
  Entry b[5] = {{0,0},{60,2},{120,1},{180,4},{240,4}};
  f = fit_line(b, 5, 32);
  assert(f.valid && f.slope > 0.8f && f.slope < 1.2f);

  // degenerate cases
  assert(!fit_line(a, 1, 32).valid);
  Entry c[2] = {{500,3},{500,5}};
  assert(!fit_line(c, 2, 32).valid);
  Entry d[2] = {{0,5},{60,5}};
  f = fit_line(d, 2, 32);
  assert(f.valid && !fit_eta_x(f, 20, &x));
  // window of 3 ignores old, slower history: 0..2 at 1/min, then 2/min
  Entry w[5] = {{0,0},{60,1},{120,2},{180,4},{240,6}};
  f = fit_line(w, 5, 3);
  assert(f.valid && fabsf(f.slope - 2.0f) < 1e-3f);
  assert(fit_eta_x(f, 16, &x) && fabsf(x - 9.0f) < 1e-2f);  // x relative to e[0]
  assert(!fit_line(w, 1, 3).valid);
  f = fit_line(w, 2, 3);  // fewer than 3 entries: uses what exists
  assert(f.valid && fabsf(f.slope - 1.0f) < 1e-3f);
  // counting down: 100 -> 94 at 2/min toward target 80
  Entry dn[4] = {{0,100},{60,98},{120,96},{180,94}};
  f = fit_line(dn, 4, 3);
  assert(f.valid && fabsf(f.slope + 2.0f) < 1e-3f);
  assert(fit_eta_x(f, 80, &x) && fabsf(x - 10.0f) < 1e-2f);
  puts("ok");
  return 0;
}
