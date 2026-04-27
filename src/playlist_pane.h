#include <raymath.h>

#include "compile_time_config.h"
#include "draw_utils.h"
#include "playlist.h"

typedef enum {
    PPS_PLAYLIST,
    PPS_TRACK,
} PlaylistPaneSelected;

typedef struct {
    PlaylistPaneSelected selected_pane;
    size_t selected_playlist;
    size_t selected_track;
    float playlists_target_offset;
    float tracklist_target_offset;
    float playlists_offset;
    float tracklist_offset;
} PlaylistPane;

void playlist_pane_update(PlaylistPane* pane, Rectangle bound, const Playlists* playlists, Audio* audio) {
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (pane->selected_pane == PPS_PLAYLIST) {
        const Playlist* pl = &playlists->items[pane->selected_playlist];

        if (IsKeyPressed(KEY_J) && playlists->count > 0 && pane->selected_playlist < playlists->count - 1)
            pane->selected_playlist++;

        if (IsKeyPressed(KEY_K) && pane->selected_playlist > 0) {
            if (pane->selected_playlist > 0) pane->selected_playlist--;
        }
        if (IsKeyPressed(KEY_L) && playlists->count > 0) {
            pane->selected_pane = PPS_TRACK;
            pane->selected_track = 0;
        }

        if (IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
            for (size_t i = 0; i < pl->tracks.count; i++) { audio_enqueue_single(audio, pl->tracks.items[i]); }
        }

        {
            const float eh = TRACK_ENTRY_H;
            const float cur = (float)pane->selected_playlist * eh;
            float max_off = (float)playlists->count * eh - bound.width;
            if (max_off < 0) max_off = 0;
            if (pane->playlists_target_offset < 0) pane->playlists_target_offset = 0;
            if (pane->playlists_target_offset > max_off) pane->playlists_target_offset = max_off;
            float top = pane->playlists_offset + eh;
            float bottom = pane->playlists_offset + bound.height - eh;
            if (cur < top) pane->playlists_target_offset = cur;
            if (cur > bottom) pane->playlists_target_offset = cur - (bound.height - eh);
        }
    }

    if (pane->selected_pane == PPS_TRACK && playlists->count > 0) {
        const Playlist* pl = &playlists->items[pane->selected_playlist];
        const float eh = TRACK_ENTRY_H;
        const float cur_off = (float)pane->selected_track * eh;
        float max_scroll = (float)pl->tracks.count * eh - bound.height;
        if (max_scroll < 0) max_scroll = 0;
        if (pane->tracklist_target_offset < 0) pane->tracklist_target_offset = 0;
        if (pane->tracklist_target_offset > max_scroll) pane->tracklist_target_offset = max_scroll;
        float top = pane->tracklist_offset + eh;
        float bottom = pane->tracklist_offset + bound.height - eh;
        if (cur_off < top) pane->tracklist_target_offset = cur_off;
        if (cur_off > bottom) pane->tracklist_target_offset = cur_off - (bound.height - eh);

        if (IsKeyPressed(KEY_J)) {
            if (pl->tracks.count > 0 && pane->selected_track < pl->tracks.count - 1) pane->selected_track++;
        }
        if (IsKeyPressed(KEY_K)) {
            if (pane->selected_track > 0) pane->selected_track--;
        }
        if (IsKeyPressed(KEY_H)) pane->selected_pane = PPS_PLAYLIST;

        if (IsKeyPressed(KEY_ENTER) && pl->tracks.count > 0) {
            audio_start_playback(audio, pl->tracks.items[pane->selected_track]);
        }

        if (shift && IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
            for (size_t i = 0; i < pl->tracks.count; i++) { audio_enqueue_single(audio, pl->tracks.items[i]); }
        }
        if (!shift && IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
            audio_enqueue_single(audio, pl->tracks.items[pane->selected_track]);
        }
    }
    pane->playlists_offset += (pane->playlists_target_offset - pane->playlists_offset) * SCROLL_SMOOTH;
    pane->tracklist_offset += (pane->tracklist_target_offset - pane->tracklist_offset) * SCROLL_SMOOTH;
}

void playlist_pane_draw(const PlaylistPane* pane, Rectangle bound, Font font, const Playlists* playlists) {
    if (playlists->count == 0) {
        const char* msg = "No playlists yet.  Press 'a' in the main pane to add tracks.";

        Vector2 sz = MeasureTextEx(font, msg, FONT_SIZE, 0);

        draw_text_with_shadow(msg, font, FOCUSED_TEXT_COLOR, SHADOW_COLOR,
                              (Vector2){(bound.width - sz.x) / 2, (bound.height - sz.y) / 2});
        return;
    }

    {
        BeginScissorMode(0, 0, (int)(bound.width / 3 - PAD), (int)bound.height);
        Vector2 cursor = {PAD, PAD - pane->playlists_offset};
        for (size_t pi = 0; pi < playlists->count; pi++) {
            const bool focused = (pi == pane->selected_playlist);
            const Color text_color = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
            const Color bg_color = focused ? UNFOCUSED_PANEL_COLOR : FOCUSED_PANEL_COLOR;
            const Rectangle bg = {cursor.x - 2, cursor.y, bound.width / 3 - PAD * 2, FONT_SIZE + 4};

            draw_round_rect(bg, bg_color, RECTANGLE_ROUNDNESS);
            draw_text_with_shadow(playlists->items[pi].name, font, text_color, SHADOW_COLOR,
                                  Vector2SubtractValue(cursor, -2));
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }

    {
        const Playlist* pl = &playlists->items[pane->selected_playlist];
        Vector2 cursor = {PAD + bound.width / 3, PAD - pane->tracklist_offset};
        BeginScissorMode((int)(cursor.x - 3 * PAD), 0, (int)((bound.width * 2) / 3 + PAD), (int)bound.height);

        if (pl->tracks.count == 0) {
            draw_text_with_shadow("playlist is empty.", font, UNFOCUSED_TEXT_COLOR, SHADOW_COLOR, cursor);
        }
        for (size_t ti = 0; ti < pl->tracks.count; ti++) {
            const bool focused = (ti == pane->selected_track && pane->selected_pane == PPS_TRACK);
            const Color tc = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
            const Rectangle bg = {cursor.x - 2, cursor.y, 2 * (bound.width / 3) - PAD * 2, FONT_SIZE + 4};

            draw_round_rect(bg, FOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);
            draw_text_with_shadow(pl->tracks.items[ti]->title, font, tc, SHADOW_COLOR,
                                  Vector2SubtractValue(cursor, -2));
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }
}
