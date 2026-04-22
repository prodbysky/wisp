#include "audio.h"
#include "playlist.h"
#include "compile_time_config.h"

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

        if (IsKeyPressed(KEY_J) && playlists->count > 0 && pane->selected_playlist < playlists->count - 1) pane->selected_playlist++;

        if (IsKeyPressed(KEY_K) && pane->selected_playlist > 0) {
            if (pane->selected_playlist > 0) pane->selected_playlist--;
        }
        if (IsKeyPressed(KEY_L) && playlists->count > 0) {
            pane->selected_pane = PPS_TRACK;
            pane->selected_track = 0;
        }

        if (IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
            for (size_t i = 0; i < pl->tracks.count; i++) {
                audio_enqueue_single(audio, pl->tracks.items[i]);
            }
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
            if (cur < top) pane->playlists_target_offset = cur - eh;
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
        if (cur_off < top) pane->tracklist_target_offset = cur_off - eh;
        if (cur_off > bottom) pane->tracklist_target_offset = cur_off - (bound.height - eh);

        if (IsKeyPressed(KEY_J)) {
            if (pl->tracks.count > 0 && pane->selected_track < pl->tracks.count - 1)
                pane->selected_track++;
        }
        if (IsKeyPressed(KEY_K)) {
            if (pane->selected_track > 0) pane->selected_track--;
        }
        if (IsKeyPressed(KEY_H)) pane->selected_pane = PPS_PLAYLIST;

        if (IsKeyPressed(KEY_ENTER) && pl->tracks.count > 0) {
            audio_start_playback(audio, pl->tracks.items[pane->selected_track]);
        }

        if (shift && IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
            for (size_t i = 0; i < pl->tracks.count; i++) {
                audio_enqueue_single(audio, pl->tracks.items[i]);
            }
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
        DrawTextEx(font, msg, (Vector2){(bound.width - sz.x) * 0.5f, (bound.height - sz.y) * 0.5f}, FONT_SIZE, 0,
                   FOCUSED_TEXT_COLOR);
        return;
    }

    {
        BeginScissorMode(0, 0, (int)(bound.width / 3 - PAD), (int)bound.height);
        Vector2 cursor = {PAD, PAD - pane->tracklist_offset};
        for (size_t pi = 0; pi < playlists->count; pi++) {
            const bool focused = (pi == pane->selected_playlist);
            const Color text_color = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
            const Color bg_color = focused ? UNFOCUSED_PANEL_COLOR : FOCUSED_PANEL_COLOR;
            const Rectangle bg = {cursor.x - 2, cursor.y, bound.width / 3 - PAD * 2, FONT_SIZE + 4};
            DrawRectangleRounded(bg, RECTANGLE_ROUNDNESS, 16, bg_color);
            DrawTextPro(font, playlists->items[pi].name, cursor, (Vector2){-3, -3}, 0, FONT_SIZE, 0,
                        SHADOW_COLOR);
            DrawTextPro(font, playlists->items[pi].name, cursor, (Vector2){-2, -2}, 0, FONT_SIZE, 0,
                        text_color);
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }

    {
        const Playlist* pl = &playlists->items[pane->selected_playlist];
        Vector2 cursor = {PAD + bound.width / 3, PAD - pane->tracklist_offset};
        BeginScissorMode((int)(cursor.x - 3 * PAD), 0, (int)((bound.width * 2) / 3 + PAD), (int)bound.height);

        if (pl->tracks.count == 0) {
            DrawTextEx(font, "Playlist is empty.", (Vector2){cursor.x, cursor.y}, FONT_SIZE, 0,
                       UNFOCUSED_TEXT_COLOR);
        }
        for (size_t ti = 0; ti < pl->tracks.count; ti++) {
            const bool focused = (ti == pane->selected_track && pane->selected_pane == PPS_TRACK);
            const Color tc = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
            const Rectangle bg = {cursor.x - 2, cursor.y, 2 * (bound.width / 3) - PAD * 2, FONT_SIZE + 4};
            DrawRectangleRounded(bg, RECTANGLE_ROUNDNESS, 16, FOCUSED_PANEL_COLOR);
            DrawTextPro(font, pl->tracks.items[ti]->title, cursor, (Vector2){-3, -3}, 0, FONT_SIZE, 0,
                        SHADOW_COLOR);
            DrawTextPro(font, pl->tracks.items[ti]->title, cursor, (Vector2){-2, -2}, 0, FONT_SIZE, 0, tc);
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }
}
