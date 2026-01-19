static int quest_measure_width(TTF_Font *font, const char *text) { if (!text)
return 0; int w = 0; /* Measure width of replacement string "¤" */ int bw =
text_width(font, "¤"); while (*text) { if (*text == '\x1A') { w += bw; text++; }
else { const char *start = text; while (*text && *text != '\x1A') text++; size_t
len = text - start; if (len > 0) { char buf[256]; if (len >= sizeof(buf)) len =
sizeof(buf) - 1; memcpy(buf, start, len); buf[len] = 0; w += text_width(font,
buf); } } } return w; } static void quest_draw_line(UI *ui, TTF_Font *font, int
x, int y, const char *text, SDL_Color color) { if (!text) return; int cur_x = x;
int bw = text_width(font, "¤"); while (*text) { if (*text == '\x1A') {
draw_text(ui, font, cur_x, y, "¤", color, false); cur_x += bw; text++; } else {
const char *start = text; while (*text && *text != '\x1A') text++; size_t len =
text - start; if (len > 0) { char buf[256]; if (len >= sizeof(buf)) len =
sizeof(buf) - 1; memcpy(buf, start, len); buf[len] = 0; draw_text(ui, font,
cur_x, y, buf, color, false); cur_x += text_width(font, buf); } } } }
