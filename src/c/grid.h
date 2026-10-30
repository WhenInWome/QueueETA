#pragma once
#include <pebble.h>

typedef void (*GridDoneCallback)(int32_t value);

// Push a number-pad window. Calls `done` with the typed value when the user picks OK.
// Controls: Up/Down pick a row, Select locks it, Up/Down pick a key, Select presses it.
// Back un-locks the row (or cancels when no row is locked).
void grid_push(const char *title, int32_t initial, GridDoneCallback done);
