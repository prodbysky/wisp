#include <stddef.h>
#include <raylib.h>
#include <stdlib.h>
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

void overlay_open(Overlay* overlay, bool whole_album, size_t selected_album, size_t selected_track, const Playlists* playlists);
void overlay_close(Overlay* overlay);
void overlay_rebuild_filter(Overlay* overlay, const Playlists* playlists);
void overlay_handle_char(Overlay* overlay, int ch, const Playlists* playlists);
void overlay_confirm(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums);

void overlay_update(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums) {
    if (IsKeyPressedRepeat(KEY_BACKSPACE) && overlay->buf_len > 0) {
        overlay->buf[--overlay->buf_len] = 0;
        overlay_rebuild_filter(overlay, playlists);
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        overlay->mode = OVERLAY_NONE;
        return;
    }
    if (overlay->mode == OVERLAY_PLAYLIST_PICK) {
        if (IsKeyPressed(KEY_J) && overlay->selected + 1 < overlay->filtered_count)
            overlay->selected++;
        if (IsKeyPressed(KEY_K) && overlay->selected > 0) overlay->selected--;
        if (IsKeyPressed(KEY_N)) {
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

void overlay_open(Overlay* overlay, bool whole_album, size_t selected_album, size_t selected_track, const Playlists* playlists) {
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
