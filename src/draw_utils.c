#include <math.h>
#include <raylib.h>
#include "draw_utils.h"
void draw_round_rect(Rectangle rect, Color c, float radius) {
    float roundness = (radius * 2.0f) / fminf(rect.width, rect.height);
    if (roundness > 1.0f) roundness = 1.0f;
    DrawRectangleRounded(rect, roundness, 16, c);
}
