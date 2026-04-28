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

void overlay_update(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums);
void overlay_draw(const Overlay* overlay, Rectangle bound, Font font, const Playlists* playlists);
void overlay_open(Overlay* overlay, bool whole_album, size_t selected_album, size_t selected_track,
                  const Playlists* playlists);
void overlay_close(Overlay* overlay);
void overlay_rebuild_filter(Overlay* overlay, const Playlists* playlists);
void overlay_handle_char(Overlay* overlay, int ch, const Playlists* playlists);
void overlay_confirm(Overlay* overlay, Playlists* playlists, const char* playlists_dir, const Albums* albums);
