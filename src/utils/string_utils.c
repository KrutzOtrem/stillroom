#include "string_utils.h"

void safe_snprintf(char *dst, size_t cap, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(dst, cap, fmt, args);
  dst[cap - 1] = '\0';
  va_end(args);
}

void trim_ascii_inplace(char *s) {
  if (!s)
    return;
  // trim leading
  size_t len = strlen(s);
  size_t i = 0;
  while (i < len &&
         (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
    i++;
  if (i > 0)
    memmove(s, s + i, len - i + 1);
  // trim trailing
  len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                     s[len - 1] == '\n' || s[len - 1] == '\r')) {
    s[len - 1] = 0;
    len--;
  }
}

void ascii_lower_inplace(char *s) {
  if (!s)
    return;
  for (; *s; s++) {
    if (*s >= 'A' && *s <= 'Z')
      *s = (char)(*s - 'A' + 'a');
  }
}

void utf8_pop_back_inplace(char *s) {
  if (!s)
    return;
  size_t n = strlen(s);
  if (n == 0)
    return;
  /* Walk left over UTF-8 continuation bytes (10xxxxxx). */
  while (n > 0 && (((unsigned char)s[n - 1]) & 0xC0) == 0x80)
    n--;
  /* Now n is at the start byte of the last codepoint (or 0). */
  if (n > 0)
    n--;
  s[n] = 0;
}

char *str_dup(const char *s) {
  size_t n = strlen(s);
  char *r = (char *)malloc(n + 1);
  if (!r)
    return NULL;
  memcpy(r, s, n + 1);
  return r;
}

void trim_newline(char *s) {
  if (!s)
    return;
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
    s[--len] = 0;
  }
}

bool ends_with_icase(const char *s, const char *ext) {
  if (!s || !ext)
    return false;
  size_t ls = strlen(s), le = strlen(ext);
  if (le > ls)
    return false;
  const char *a = s + (ls - le);
  for (size_t i = 0; i < le; i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)ext[i]))
      return false;
  }
  return true;
}

/* ----------------------------- StrList ----------------------------- */

static int cmp_cstr(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  return strcmp(*sa, *sb);
}

void sl_sort(StrList *l) {
  if (!l || l->count <= 1)
    return;
  qsort(l->items, (size_t)l->count, sizeof(char *), cmp_cstr);
}

void sl_free(StrList *l) {
  if (!l)
    return;
  for (int i = 0; i < l->count; i++)
    free(l->items[i]);
  free(l->items);
  memset(l, 0, sizeof(*l));
}

void sl_push(StrList *l, const char *s) {
  if (!l || !s)
    return;
  if (l->count + 1 > l->cap) {
    int ncap = l->cap ? l->cap * 2 : 16;
    char **nitems = (char **)realloc(l->items, sizeof(char *) * ncap);
    if (!nitems)
      return;
    l->items = nitems;
    l->cap = ncap;
  }
  l->items[l->count++] = str_dup(s);
}

void sl_push_owned(StrList *l, char *s) {
  if (!l || !s) {
    free(s);
    return;
  }
  if (l->count + 1 > l->cap) {
    int ncap = l->cap ? l->cap * 2 : 16;
    char **nitems = (char **)realloc(l->items, sizeof(char *) * ncap);
    if (!nitems) {
      free(s);
      return;
    }
    l->items = nitems;
    l->cap = ncap;
  }
  l->items[l->count++] = s;
}

void sl_remove_idx(StrList *l, int idx) {
  if (!l || idx < 0 || idx >= l->count)
    return;
  free(l->items[idx]);
  for (int i = idx + 1; i < l->count; i++) {
    l->items[i - 1] = l->items[i];
  }
  l->count--;
  if (l->count == 0) {
    free(l->items);
    l->items = NULL;
    l->cap = 0;
  }
}

void sl_clear(StrList *l) {
  if (!l)
    return;
  for (int i = 0; i < l->count; i++) {
    free(l->items[i]);
  }
  l->count = 0;
}

int sl_find(const StrList *l, const char *s) {
  if (!l || !s)
    return -1;
  for (int i = 0; i < l->count; i++) {
    if (strcmp(l->items[i], s) == 0)
      return i;
  }
  return -1;
}

/* -------------------------- Phase ordering -------------------------- */

int phase_rank_from_leading_tag(const char *s) {
  if (!s)
    return 0;
  if (s[0] != '(')
    return 0;
  const char *p = s + 1;
  /* Accept lower/upper roman numerals i..vii */
  char buf[8] = {0};
  int bi = 0;
  while (*p && *p != ')' && bi < 7) {
    char c = *p++;
    if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
    buf[bi++] = c;
  }
  if (*p != ')')
    return 0;
  if (strcmp(buf, "i") == 0)
    return 1;
  if (strcmp(buf, "ii") == 0)
    return 2;
  if (strcmp(buf, "iii") == 0)
    return 3;
  if (strcmp(buf, "iv") == 0)
    return 4;
  if (strcmp(buf, "v") == 0)
    return 5;
  if (strcmp(buf, "vi") == 0)
    return 6;
  if (strcmp(buf, "vii") == 0)
    return 7;
  return 0;
}

const char *phase_strip_leading_tag(const char *s) {
  if (!s)
    return s;
  int r = phase_rank_from_leading_tag(s);
  if (r <= 0)
    return s;
  const char *rp = strchr(s, ')');
  if (!rp)
    return s;
  rp++; /* after ')' */
  while (*rp == ' ' || *rp == '\t')
    rp++;
  return rp;
}

static int cmp_phase_cstr(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  const int ra = phase_rank_from_leading_tag(*sa);
  const int rb = phase_rank_from_leading_tag(*sb);
  /* Ranked items come first, ordered by rank. */
  if (ra > 0 && rb > 0) {
    if (ra != rb)
      return (ra < rb) ? -1 : 1;
    return strcmp(*sa, *sb);
  }
  if (ra > 0 && rb == 0)
    return -1;
  if (ra == 0 && rb > 0)
    return 1;
  return strcmp(*sa, *sb);
}

void sl_sort_phases(StrList *l) {
  if (!l || l->count <= 1)
    return;
  qsort(l->items, (size_t)l->count, sizeof(char *), cmp_phase_cstr);
}

/* -------------------------- Hashing -------------------------- */
uint32_t fnv1a32(const char *s) {
  uint32_t h = 2166136261u;
  if (s) {
    for (; *s; s++) {
      h ^= (uint8_t)*s;
      h *= 16777619u;
    }
  }
  return h;
}

/* -------------------------- Date Strings -------------------------- */
const char *month_name_lower(int mon) {
  static const char *kMonths[12] = {
      "january", "february", "march",     "april",   "may",      "june",
      "july",    "august",   "september", "october", "november", "december"};
  if (mon < 0 || mon > 11)
    return "";
  return kMonths[mon];
}

const char *day_ordinal_lower(int day) {
  static const char *kDays[32] = {"",
                                  "first",
                                  "second",
                                  "third",
                                  "fourth",
                                  "fifth",
                                  "sixth",
                                  "seventh",
                                  "eighth",
                                  "ninth",
                                  "tenth",
                                  "eleventh",
                                  "twelfth",
                                  "thirteenth",
                                  "fourteenth",
                                  "fifteenth",
                                  "sixteenth",
                                  "seventeenth",
                                  "eighteenth",
                                  "nineteenth",
                                  "twentieth",
                                  "twenty-first",
                                  "twenty-second",
                                  "twenty-third",
                                  "twenty-fourth",
                                  "twenty-fifth",
                                  "twenty-sixth",
                                  "twenty-seventh",
                                  "twenty-eighth",
                                  "twenty-ninth",
                                  "thirtieth",
                                  "thirty-first"};
  if (day < 1 || day > 31)
    return "";
  return kDays[day];
}

void strip_extension(const char *in, char *out, size_t out_sz) {
  if (!in || !out || out_sz == 0)
    return;
  safe_snprintf(out, out_sz, "%s", in);
  char *dot = strrchr(out, '.');
  if (dot)
    *dot = '\0';
}

/* Only strip a real audio extension (e.g. ".wav", ".mp3"). This preserves
     extra dots in base names, like "rain..wav" -> "rain." (dot kept). */
void strip_ext_inplace(char *s) {
  if (!s)
    return;
  char *dot = strrchr(s, '.');
  if (!dot)
    return;
  /* Preserve names that end with a dot (e.g. "rain.") */
  if (dot[1] == 0)
    return;
  if (strcasecmp(dot + 1, "wav") == 0 || strcasecmp(dot + 1, "mp3") == 0) {
    *dot = 0;
  }
}

/* -------------------------- Config Utils -------------------------- */
void config_trim(char *s) {
  if (!s)
    return;
  char *p = s;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (p != s)
    memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1]))
    s[--n] = 0;
}

/* -------------------------- Hashing Helpers -------------------------- */

bool hashes_contains(const uint32_t *arr, int n, uint32_t v) {
  if (!arr || n <= 0)
    return false;
  for (int i = 0; i < n; i++) {
    if (arr[i] == v)
      return true;
  }
  return false;
}

void hashes_add(uint32_t **arr, int *n, uint32_t v) {
  if (!arr || !n)
    return;
  if (hashes_contains(*arr, *n, v))
    return;
  uint32_t *new_arr = (uint32_t *)realloc(*arr, sizeof(uint32_t) * (*n + 1));
  if (new_arr) {
    *arr = new_arr;
    (*arr)[*n] = v;
    (*n)++;
  }
}

void hashes_remove(uint32_t *arr, int *n, uint32_t v) {
  if (!arr || !n || *n <= 0)
    return;
  int idx = -1;
  for (int i = 0; i < *n; i++) {
    if (arr[i] == v) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return;
  for (int i = idx; i < *n - 1; i++) {
    arr[i] = arr[i + 1];
  }
  (*n)--;
}

void hashes_free(uint32_t **arr, int *n) {
  if (arr && *arr) {
    free(*arr);
    *arr = NULL;
  }
  if (n)
    *n = 0;
}

void hashes_from_csv(const char *csv, uint32_t **out_arr, int *out_n) {
  if (!out_arr || !out_n)
    return;
  hashes_free(out_arr, out_n);
  if (!csv || !csv[0])
    return;

  /* Count commas */
  int count = 1;
  for (const char *p = csv; *p; p++) {
    if (*p == ',')
      count++;
  }

  *out_arr = (uint32_t *)malloc(sizeof(uint32_t) * count);
  if (!*out_arr)
    return;

  char buf[4096];
  safe_snprintf(buf, sizeof(buf), "%s", csv);
  char *p = buf;
  char *token;
  while ((token = strsep(&p, ",")) != NULL) {
    config_trim(token);
    if (!token[0])
      continue;
    /* hex or decimal? usually stored as decimal %u */
    (*out_arr)[*out_n] = (uint32_t)strtoul(token, NULL, 10);
    (*out_n)++;
  }
}

void hashes_to_csv(const uint32_t *arr, int n, char *buf, size_t buf_sz) {
  if (!buf || buf_sz == 0)
    return;
  buf[0] = 0;
  if (!arr || n <= 0)
    return;

  size_t cur = 0;
  for (int i = 0; i < n; i++) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%u", arr[i]);
    size_t len = strlen(tmp);
    if (cur + len + (i > 0 ? 1 : 0) + 1 >= buf_sz)
      break;
    if (i > 0)
      strcat(buf, ",");
    strcat(buf, tmp);
    cur += len + (i > 0 ? 1 : 0);
  }
}
void append_char(char *buf, size_t cap, char ch) {
  if (!buf || cap == 0)
    return;
  size_t n = strlen(buf);
  if (n + 1 < cap) {
    buf[n] = ch;
    buf[n + 1] = 0;
  }
}
