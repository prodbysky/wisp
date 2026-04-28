#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "../extern/raylib/src/raylib.h"

static Color* kmeans(Image image, int k, int max_it) {
    int count = image.width * image.height;
    Color* pixels = LoadImageColors(image);
    if (!pixels) return NULL;

    int* labels = (int*)malloc(count * sizeof(int));
    if (!labels) {
        UnloadImageColors(pixels);
        return NULL;
    }

    float* cr = (float*)malloc(k * sizeof(float));
    float* cg = (float*)malloc(k * sizeof(float));
    float* cb = (float*)malloc(k * sizeof(float));

    float* sum_r = (float*)calloc(k, sizeof(float));
    float* sum_g = (float*)calloc(k, sizeof(float));
    float* sum_b = (float*)calloc(k, sizeof(float));
    int* cnt = (int*)calloc(k, sizeof(int));

    if (!cr || !cg || !cb) {
        free(labels);
        UnloadImageColors(pixels);
        free(cr);
        free(cg);
        free(cb);
        return NULL;
    }

    for (int i = 0; i < k; i++) {
        int idx = GetRandomValue(0, count - 1);
        cr[i] = pixels[idx].r;
        cg[i] = pixels[idx].g;
        cb[i] = pixels[idx].b;
    }

    for (int it = 0; it < max_it; it++) {
        for (int i = 0; i < count; i++) {
            float best_dist = 1e30f;
            int best = 0;

            for (int j = 0; j < k; j++) {
                float dr = pixels[i].r - cr[j];
                float dg = pixels[i].g - cg[j];
                float db = pixels[i].b - cb[j];
                float dist = dr * dr + dg * dg + db * db;

                if (dist < best_dist) {
                    best_dist = dist;
                    best = j;
                }
            }
            labels[i] = best;
        }

        for (int i = 0; i < count; i++) {
            int c = labels[i];
            sum_r[c] += pixels[i].r;
            sum_g[c] += pixels[i].g;
            sum_b[c] += pixels[i].b;
            cnt[c]++;
        }

        for (int j = 0; j < k; j++) {
            if (cnt[j] > 0) {
                cr[j] = sum_r[j] / cnt[j];
                cg[j] = sum_g[j] / cnt[j];
                cb[j] = sum_b[j] / cnt[j];
            } else {
                int idx = GetRandomValue(0, count - 1);
                cr[j] = pixels[idx].r;
                cg[j] = pixels[idx].g;
                cb[j] = pixels[idx].b;
            }
        }
    }

    Color* palette = (Color*)malloc(k * sizeof(Color));
    if (!palette) {
        free(labels);
        free(cr);
        free(cg);
        free(cb);
        UnloadImageColors(pixels);
        return NULL;
    }

    for (int i = 0; i < k; i++) {
        palette[i].r = (unsigned char)cr[i];
        palette[i].g = (unsigned char)cg[i];
        palette[i].b = (unsigned char)cb[i];
        palette[i].a = 255;
    }

    free(labels);
    free(cr);
    free(cg);
    free(cb);
    UnloadImageColors(pixels);

    return palette;
}
