#ifndef TIMER_TYPES_H
#define TIMER_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifndef MAX_STOPWATCH_LAPS
#define MAX_STOPWATCH_LAPS 5
#endif

typedef struct TimerState {
  /* Pomodoro picker state */
  int pomo_pick_sel;
  int pick_pomo_session_min;
  int pick_pomo_break_min;
  int pick_pomo_long_break_min;
  int pick_pomo_loops;

  /* Pomodoro runtime */
  uint32_t pomo_session_seconds;
  uint32_t pomo_break_seconds;
  uint32_t pomo_long_break_seconds;
  int pomo_session_in_pomo; /* 0=first session, 1=second session */
  bool pomo_break_is_long;
  int pomo_loops_total;
  int pomo_loops_done;
  bool pomo_is_break;
  uint32_t pomo_remaining_seconds;

  /* Custom timer picker */
  int pick_custom_hours;   /* 0..24 */
  int pick_custom_minutes; /* 0..59 */
  int pick_custom_seconds; /* 0..59 */
  int custom_field_sel;    /* 0=hh, 1=mm, 2=ss */

  /* Custom timer runtime */
  uint32_t custom_total_seconds;
  uint32_t custom_remaining_seconds;
  bool custom_counting_up_active;

  /* Stopwatch */
  uint32_t stopwatch_seconds;
  uint32_t stopwatch_laps[MAX_STOPWATCH_LAPS];
  int stopwatch_lap_count;
  int stopwatch_lap_base;

  /* Tick timing */
  uint64_t last_tick_ms;
  float tick_accum;

  /* Quick menu */
  bool timer_menu_open;
  int menu_sel;
} TimerState;

#endif /* TIMER_TYPES_H */
