#ifndef ROUTINE_TYPES_H
#define ROUTINE_TYPES_H

#include "../../utils/string_utils.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ROUTINE_PHASE_DAWN = 0,
  ROUTINE_PHASE_MORNING,
  ROUTINE_PHASE_AFTERNOON,
  ROUTINE_PHASE_EVENING,
  ROUTINE_PHASE_DUSK,
  ROUTINE_PHASE_NIGHT,
  ROUTINE_PHASE_ALL, /* special: show all phases */
  ROUTINE_PHASE_COUNT = ROUTINE_PHASE_ALL,
} RoutinePhase;

typedef struct {
  char action[64];     /* "meditate" */
  char trigger[64];    /* "wake up" */
  int duration_min;    /* 15 */
  uint8_t phases;      /* bitmask of RoutinePhase */
  uint8_t repeat_days; /* bitmask: bit 0=Mon, bit 6=Sun, 127=every day */
  int color_idx;       /* palette color index */
  char tag[64];        /* optional tag for statistics */
} RoutineItem;

typedef struct {
  int date;           /* YYYYMMDD */
  uint64_t completed; /* bitmask of completed routine indices */
} RoutineCompletion;

typedef struct {
  RoutineItem *items;
  int items_n;
  int items_cap;
  RoutineCompletion *completions;
  int completions_n;
  int completions_cap;
  int sel_row;   /* selected item in list */
  int sel_day;   /* 0=Mon..6=Sun */
  int sel_phase; /* RoutinePhase: 0=dawn..6=all */
  int list_scroll;
  int edit_return; /* Cast to/from Screen enum */
  char edit_action[64];
  char edit_trigger[64];
  int edit_duration;
  uint8_t edit_phases; /* bitmask */
  uint8_t edit_days;   /* bitmask */
  int edit_color;
  int edit_field; /* 0=action, 1=trigger, 2=duration, 3=phases, 4=days, 5=color
                   */
  bool edit_active; /* true if editing the selected field */
  int kb_row;
  int kb_col;
  bool delete_confirm_open;
  int delete_confirm_sel;
  int delete_idx;
  int edit_idx;
  bool grid_view;
  int grid_row;
  int grid_col;
  StrList history_actions;
  StrList history_triggers;
  int entry_picker_sel;
  int entry_picker_target; /* 0=action, 1=trigger */
} RoutineState;

#endif // ROUTINE_TYPES_H
