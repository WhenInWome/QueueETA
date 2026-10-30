# Queue ETA (Pebble)

Waiting in line? Enter your current queue position, then log your new position each
time it changes (it counts down to 0, the front of the line). The app timestamps every
entry, fits a least-squares line through the 3 most recent logs (position vs. time) and
projects when the line reaches 0.

## Controls
| Button | Action |
|---|---|
| Up / Down | change queue position by 1 (hold to speed up) |
| Select | log the current position with the current time |
| Hold Back | undo the last logged entry |
| Hold Select | menu: **Type queue position** (number grid, logs on OK), **Vibrate before ETA** (minutes before the ETA to buzz; 0 = off, the default), **Clear log** (starts a new queue) |

**Touch (Pebble Time 2):** tap the status line or big number to type a new queue position.

The ETA is shown in 12-hour time. The alert uses the wakeup service, so it also fires if the app was closed (it relaunches); it re-arms whenever the ETA changes after a new log, and buzzes right away if you set it when the ETA is already inside the window.

The first time the app is opened on a new calendar day, the queue starts clear (the app records the day it was last launched).

The number grid opens automatically when the log is empty (first launch, or after clearing).

### Number grid
Keys are `1-9`, `<` (delete), `0`, `OK`. Up/Down move between rows, Select locks the row,
Up/Down then move between keys in that row, Select presses the key and returns to row
selection. On touchscreen watches (Pebble Time 2) just tap a key. Back un-locks a row (or cancels the grid if none is locked). `OK` on an empty
entry cancels.

The graph shows logged points (dots), the best-fit line (cyan) and the front of the line (dotted yellow).
Needs 2+ logs with elapsed time to give an ETA. State persists between launches (last 32 logs kept).

## Build
    pebble build
    pebble install --emulator basalt     # or --phone <ip>

Fit math unit test (host): `gcc tests/test_fit.c -lm -o /tmp/t && /tmp/t`
