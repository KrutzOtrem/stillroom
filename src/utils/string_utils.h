#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------- Safey Utils ----------------------------- */
void safe_snprintf(char *dst, size_t cap, const char *fmt, ...);

/* ----------------------------- String Mods ----------------------------- */
void trim_ascii_inplace(char *s);
void ascii_lower_inplace(char *s);
void utf8_pop_back_inplace(char *s);
char *str_dup(const char *s);
void trim_newline(char *s);

bool ends_with_icase(const char *s, const char *ext);
void strip_extension(const char *in, char *out, size_t out_sz);
void strip_ext_inplace(char *s);
void append_char(char *buf, size_t cap, char ch);

/* ----------------------------- StrList ----------------------------- */
typedef struct StrList {
  char **items;
  int count;
  int cap;
} StrList;

void sl_sort(StrList *l);
void sl_free(StrList *l);
void sl_push(StrList *l, const char *s);
void sl_push_owned(StrList *l, char *s);
void sl_remove_idx(StrList *l, int idx);
void sl_clear(StrList *l);
int sl_find(const StrList *l, const char *s);

/* -------------------------- Phase ordering -------------------------- */
int phase_rank_from_leading_tag(const char *s);
const char *phase_strip_leading_tag(const char *s);
void sl_sort_phases(StrList *l);

/* -------------------------- Hashing -------------------------- */
uint32_t fnv1a32(const char *s);
void hashes_from_csv(const char *csv, uint32_t **out_arr, int *out_n);
void hashes_to_csv(const uint32_t *arr, int n, char *buf, size_t buf_sz);
bool hashes_contains(const uint32_t *arr, int n, uint32_t v);
void hashes_add(uint32_t **arr, int *n, uint32_t v);
void hashes_remove(uint32_t *arr, int *n, uint32_t v);
void hashes_free(uint32_t **arr, int *n);

void config_trim(char *s);

/* -------------------------- Date Strings -------------------------- */
const char *month_name_lower(int mon);
const char *day_ordinal_lower(int day);

#endif // STRING_UTILS_H
