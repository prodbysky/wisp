
#include "compile_time_config.h"
#include "draw_utils.h"
#include "playlist.h"
#include "../extern/raylib/src/raymath.h"
#include "audio.h"

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

void playlist_pane_update(PlaylistPane* pane, Rectangle bound, const Playlists* playlists, Audio* audio);
void playlist_pane_draw(const PlaylistPane* pane, Rectangle bound, Font font, const Playlists* playlists);

