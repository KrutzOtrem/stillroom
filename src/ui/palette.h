#ifndef PALETTE_H
#define PALETTE_H

#include <stdint.h>

typedef struct NamedColor {
  const char *name;
  uint8_t r, g, b;
} NamedColor;

static const NamedColor PALETTE[] = {
    /* neutrals */
    {"pearl", 245, 245, 250},
    {"mist", 240, 245, 255},
    {"warm white", 252, 247, 235},
    {"cream", 255, 253, 208},
    {"paper", 244, 239, 229},
    {"oat", 235, 230, 210},
    {"sand", 230, 215, 185},
    {"tan", 210, 180, 140},
    {"graphite", 200, 205, 215},
    {"stone", 170, 175, 180},
    {"smoke", 150, 150, 150},
    {"slate", 130, 145, 165},
    {"ash", 110, 115, 120},
    {"charcoal", 75, 80, 90},
    {"espresso", 85, 60, 45},
    {"ink", 40, 40, 50},
    /* reds */
    {"salmon", 250, 128, 114},
    {"coral pink", 248, 131, 121},
    {"crimson", 235, 90, 105},
    {"scarlet", 255, 85, 95},
    {"candy", 255, 60, 80},
    {"cherry", 220, 20, 60},
    {"brick", 210, 90, 70},
    {"rust", 183, 65, 14},
    {"ruby", 155, 17, 30},
    {"maroon", 150, 50, 70},
    {"wine", 114, 47, 55},
    /* pinks / magentas */
    {"blush", 255, 200, 215},
    {"bubblegum", 255, 180, 210},
    {"rose", 255, 155, 175},
    {"watermelon", 255, 120, 150},
    {"hot pink", 255, 105, 180},
    {"magenta", 230, 120, 195},
    {"fuschia", 255, 0, 255},
    {"neon pink", 255, 110, 190},
    /* oranges */
    {"peach", 255, 195, 160},
    {"apricot", 251, 206, 177},
    {"cantaloupe", 255, 204, 153},
    {"coral", 255, 145, 120},
    {"tangerine", 255, 165, 90},
    {"orange", 255, 140, 0},
    {"ginger", 235, 145, 60},
    {"pumpkin", 235, 125, 60},
    {"amber", 255, 196, 90},
    {"neon orange", 255, 135, 50},
    /* yellows */
    {"butter", 255, 245, 180},
    {"lemon", 255, 235, 130},
    {"gold", 255, 210, 95},
    {"sunflower", 255, 200, 50},
    {"honey", 230, 180, 60},
    {"mustard", 225, 173, 1},
    {"banana", 255, 225, 53},
    /* greens */
    {"honeydew", 240, 255, 240},
    {"mint", 168, 235, 214},
    {"pistachio", 147, 197, 114},
    {"sage", 156, 175, 136},
    {"chartreuse", 200, 255, 95},
    {"lime", 140, 235, 120},
    {"neon green", 120, 255, 120},
    {"retro green", 120, 235, 165},
    {"grass", 100, 200, 100},
    {"emerald", 80, 200, 120},
    {"olive", 160, 190, 95},
    {"moss", 138, 154, 91},
    {"jade", 0, 168, 107},
    {"forest", 80, 150, 90},
    {"pine", 50, 120, 90},
    /* cyans / teals */
    {"ice", 200, 255, 250},
    {"arctic", 180, 240, 255},
    {"seafoam", 120, 214, 196},
    {"aqua", 0, 255, 255},
    {"turquoise", 80, 220, 200},
    {"teal", 70, 180, 185},
    {"cyan", 105, 210, 220},
    {"neon cyan", 80, 255, 245},
    /* blues */
    {"baby blue", 137, 207, 240},
    {"sky", 145, 200, 255},
    {"azure", 100, 185, 255},
    {"periwinkle", 204, 204, 255},
    {"cornflower", 100, 149, 237},
    {"denim", 90, 135, 210},
    {"neon blue", 90, 160, 255},
    {"cobalt", 70, 110, 235},
    {"royal", 65, 105, 225},
    {"steel", 70, 130, 180},
    {"indigo", 75, 0, 130},
    {"navy", 0, 0, 128},
    {"midnight", 45, 60, 120},
    /* purples */
    {"thistle", 216, 191, 216},
    {"lavender", 195, 170, 255},
    {"lilac", 200, 162, 200},
    {"orchid", 218, 112, 214},
    {"violet", 145, 110, 240},
    {"neon purple", 190, 110, 255},
    {"plum", 155, 95, 170},
    {"amethyst", 153, 102, 204},
    {"grape", 111, 45, 168},
    {"eggplant", 97, 64, 81},
};

#define PALETTE_SIZE ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

#endif // PALETTE_H
