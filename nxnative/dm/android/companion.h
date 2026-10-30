// The companion page on a second screen (kalasyan fork): companion_android.c shows it, companion_dread.c draws Metroid Dread's map on it.
#pragma once
#include <stdint.h>
typedef struct { uint32_t *px; int w, h, stride; } page;   // RGBA_8888 pixels (0xAABBGGRR)
void fill(page *p, int x0, int y0, int x1, int y1, uint32_t c);
void text(page *p, int x, int y, int s, uint32_t c, const char *t);   // the 5x7 font, s = the size of one font dot in pixels
int dm_dread_active(void);   // the game is Metroid Dread 2.1.0
void dm_dread_tick(void);    // the companion thread, each pass: finds and reads the player's state
int dm_dread_draw(page *p);  // the map with the player on it; 0 = nothing to show yet
