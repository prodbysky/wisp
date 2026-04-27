#ifndef DRAW_UTILS_H
#define DRAW_UTILS_H

#include "../extern/raylib/src/raylib.h"

void draw_round_rect(Rectangle rect, Color c, float radius);
void draw_text_with_shadow(const char* text, Font font, Color base, Color shadow, Vector2 pos);
#endif
