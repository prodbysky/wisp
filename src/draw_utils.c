#include "draw_utils.h"

#include <math.h>
#include <raylib.h>

#include "compile_time_config.h"

void draw_round_rect(Rectangle rect, Color c, float radius) {
    float roundness = (radius * 2.0f) / fminf(rect.width, rect.height);
    if (roundness > 1.0f) roundness = 1.0f;
    DrawRectangleRounded(rect, roundness, 16, c);
}

void draw_text_with_shadow(const char* text, Font font, Color base, Color shadow, Vector2 pos) {
    DrawTextPro(font, text, pos, (Vector2){-2, -2}, 0, FONT_SIZE, 0, shadow);
    DrawTextPro(font, text, pos, (Vector2){}, 0, FONT_SIZE, 0, base);
}
