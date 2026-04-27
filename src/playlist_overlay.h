#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "../extern/raylib/src/raylib.h"
#include "compile_time_config.h"
#include "draw_utils.h"
#include "library.h"
#include "playlist.h"

typedef enum {
    OVERLAY_NONE,
    OVERLAY_PLAYLIST_PICK,
    OVERLAY_PLAYLIST_NEW,
} OverlayMode;

#define OVERLAY_BUF_MAX 256

typedef struct {
    OverlayMode mode;

    char buf[OVERLAY_BUF_MAX];
    int buf_len;

    size_t* filtered_indices;
    size_t filtered_count;

    size_t selected;

    bool add_whole_album;
    size_t pending_album_idx;
    size_t pending_track_idx;
} Overlay;

void overlay_open(Overlay* overlay, bool whole_album, size_t selected_album, size_t selected_track,
                  const Playlists* playlists);
void overlay_close(Overlay* overlay);
void overlay_rebuild_filter(Overlay* overlay, const Playlists* playlists);
void overlay_handle_char(Overlay* overlay, int ch, const Playlists* playlists);
void overlay_confirm(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums);

void overlay_update(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums) {
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && overlay->buf_len > 0) {
        overlay->buf[--overlay->buf_len] = 0;
        overlay_rebuild_filter(overlay, playlists);
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        overlay->mode = OVERLAY_NONE;
        return;
    }
    if (overlay->mode == OVERLAY_PLAYLIST_PICK) {
        if (IsKeyPressed(KEY_J) && overlay->selected + 1 < overlay->filtered_count) overlay->selected++;
        if (IsKeyPressed(KEY_K) && overlay->selected > 0) overlay->selected--;
        if (ctrl && IsKeyPressed(KEY_N)) {
            overlay->mode = OVERLAY_PLAYLIST_NEW;
            overlay->buf_len = 0;
            overlay->buf[0] = '\0';
        }
    }
    if (IsKeyPressed(KEY_ENTER)) overlay_confirm(overlay, playlists, playlists_dir, albums);
    int ch = 0;
    while ((ch = GetCharPressed()) != 0) {
        if (ch >= 32 && ch < 127) {
            if (overlay->buf_len >= OVERLAY_BUF_MAX - 1) return;
            overlay->buf[overlay->buf_len++] = ch;
            overlay->buf[overlay->buf_len] = 0;
            overlay_rebuild_filter(overlay, playlists);
        }
    }
}

void overlay_draw(const Overlay* overlay, Rectangle bound, Font font, const Playlists* playlists) {
    DrawRectangle(bound.x, bound.y, bound.width, bound.height, ColorAlpha(BLACK, 0.55));

    const float box_w = bound.width * 0.5f;
    const float box_x = (bound.width - box_w) * 0.5f;
    const float box_y = bound.height * 0.15f;
    const float line_h = FONT_SIZE + 8.0f;

    Vector2 cursor = (Vector2){.x = box_x + PAD, .y = box_y + PAD};

    switch (overlay->mode) {
        case OVERLAY_PLAYLIST_NEW: {
            const Rectangle box = {.x = box_x, .y = box_y, .width = box_w, .height = 3.5f * line_h};
            {
                draw_round_rect(box, UNFOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);

                draw_text_with_shadow("new playlist name:", font, FOCUSED_TEXT_COLOR, SHADOW_COLOR, cursor);
                cursor.y += line_h;
            }
            {
                draw_round_rect((Rectangle){cursor.x, cursor.y, box.width - 2 * PAD, line_h}, FOCUSED_PANEL_COLOR,
                                RECTANGLE_ROUNDNESS);

                draw_text_with_shadow(overlay->buf, font, FOCUSED_TEXT_COLOR, SHADOW_COLOR,
                                      (Vector2){cursor.x + PAD, cursor.y + 4});
                if ((int)(GetTime() * 2) % 2 == 0) {
                    float cx = box_x + 16 + MeasureTextEx(font, overlay->buf, FONT_SIZE, 0).x;
                    DrawRectangle((int)cx, (int)(cursor.y + 4), 2, FONT_SIZE, FOCUSED_TEXT_COLOR);
                }
                cursor.y += line_h;
            }
            draw_text_with_shadow("[Enter] confirm  [Esc] cancel", font, UNFOCUSED_TEXT_COLOR, SHADOW_COLOR,
                                  (Vector2){cursor.x, cursor.y + PAD});
            break;
        }
        case OVERLAY_PLAYLIST_PICK: {
            size_t visible = overlay->filtered_count < 8 ? overlay->filtered_count : 8;
            if (visible < 1) visible = 1;
            const float height = line_h * (visible * 4);

            draw_round_rect((Rectangle){box_x, box_y, box_w, height}, UNFOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);
            draw_round_rect((Rectangle){box_x + PAD, box_y + PAD, box_w - 2 * PAD, line_h}, FOCUSED_PANEL_COLOR,
                            RECTANGLE_ROUNDNESS);
            const char* placeholder = "Search playlist…";
            const char* search_text = overlay->buf_len > 0 ? overlay->buf : placeholder;
            Color search_col = overlay->buf_len > 0 ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;

            draw_text_with_shadow(search_text, font, search_col, SHADOW_COLOR,
                                  (Vector2){box_x + 2 * PAD, box_y + (float)(PAD * 1.5)});
            if (overlay->buf_len > 0 && (int)(GetTime() * 2) % 2 == 0) {
                float cx = box_x + 16 + MeasureTextEx(font, overlay->buf, FONT_SIZE, 0).x;
                DrawRectangle((int)cx, (int)(box_y + 10), 2, FONT_SIZE, FOCUSED_TEXT_COLOR);
            }
            float ry = box_y + line_h + 8;
            if (overlay->filtered_count == 0) {
                draw_text_with_shadow("no playlists found.", font, FOCUSED_TEXT_COLOR, SHADOW_COLOR,
                                      (Vector2){box_x + 2 * PAD, ry + (float)(PAD * .5)});
            } else {
                for (size_t i = 0; i < 8 && i < overlay->filtered_count; i++) {
                    size_t pi = overlay->filtered_indices[i];
                    const bool sel = (i == overlay->selected);
                    Color rect_color = sel ? FOCUSED_PANEL_COLOR : UNFOCUSED_PANEL_COLOR;
                    if (sel)
                        draw_round_rect((Rectangle){box_x + PAD, ry + PAD, box_w - 2 * PAD, line_h}, rect_color,
                                        RECTANGLE_ROUNDNESS);
                    Color tc = sel ? UNFOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;

                    draw_text_with_shadow(playlists->items[pi].name, font, tc, SHADOW_COLOR,
                                          (Vector2){box_x + 2 * PAD, ry + (float)(PAD * 1.5)});
                    ry += line_h;
                }
            }

            draw_text_with_shadow("[C-n] new   [Enter] add   [Esc] cancel", font, FOCUSED_TEXT_COLOR, SHADOW_COLOR,
                                  (Vector2){box_x + (float)(1.5 * PAD), box_y + height - line_h + PAD / 2});
            break;
        }
    }
}

void overlay_rebuild_filter(Overlay* overlay, const Playlists* playlists) {
    free(overlay->filtered_indices);
    overlay->filtered_indices = NULL;
    overlay->filtered_count = 0;
    overlay->selected = 0;

    size_t cap = playlists->count;
    if (cap == 0) return;

    overlay->filtered_indices = (size_t*)malloc(cap * sizeof(size_t));

    const char* needle = overlay->buf;
    for (size_t i = 0; i < playlists->count; i++) {
        if (needle[0] == '\0' || strstr(playlists->items[i].name, needle)) {
            overlay->filtered_indices[overlay->filtered_count++] = i;
        }
    }
}

void overlay_open(Overlay* overlay, bool whole_album, size_t selected_album, size_t selected_track,
                  const Playlists* playlists) {
    overlay->mode = OVERLAY_PLAYLIST_PICK;
    overlay->buf_len = 0;
    overlay->buf[0] = '\0';
    overlay->add_whole_album = whole_album;
    overlay->pending_album_idx = selected_album;
    overlay->pending_track_idx = selected_track;
    overlay_rebuild_filter(overlay, playlists);

    if (playlists->count == 0) overlay->mode = OVERLAY_PLAYLIST_NEW;
}

void overlay_close(Overlay* overlay) {
    overlay->mode = OVERLAY_NONE;
    free(overlay->filtered_indices);
    overlay->filtered_indices = NULL;
    overlay->filtered_count = 0;
}

void overlay_handle_char(Overlay* overlay, int ch, const Playlists* playlists) {
    if (overlay->buf_len >= OVERLAY_BUF_MAX - 1) return;
    overlay->buf[overlay->buf_len++] = (char)ch;
    overlay->buf[overlay->buf_len] = '\0';
    if (overlay->mode == OVERLAY_PLAYLIST_PICK) overlay_rebuild_filter(overlay, playlists);
}

void overlay_add_pending_to_playlist(Overlay* overlay, Playlist* pl, const Albums* albums) {
    const Album* alb = &albums->items[overlay->pending_album_idx];
    if (overlay->add_whole_album) {
        for (size_t i = overlay->pending_track_idx; i < alb->tracks.count; i++)
            playlist_add_track(pl, alb->tracks.items[i]);
    } else {
        playlist_add_track(pl, alb->tracks.items[overlay->pending_track_idx]);
    }
}

void overlay_confirm(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums) {
    if (overlay->mode == OVERLAY_PLAYLIST_PICK) {
        if (overlay->filtered_count == 0) return;
        size_t idx = overlay->filtered_indices[overlay->selected];
        overlay_add_pending_to_playlist(overlay, &playlists->items[idx], albums);
        overlay_close(overlay);
    } else if (overlay->mode == OVERLAY_PLAYLIST_NEW) {
        if (overlay->buf_len == 0) return;
        Playlist* pl = playlists_create(playlists, overlay->buf);
        playlist_save_one(playlists_dir, pl);
        overlay_add_pending_to_playlist(overlay, pl, albums);
        overlay_close(overlay);
    }
}
