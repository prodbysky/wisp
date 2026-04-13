#include "audio.h"
#include "library.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define BG_COLOR GetColor(0x181818ff)

typedef enum {
    PANE_ALBUMS,
    PANE_QUEUE,
    PANE_COUNT,
} Pane;

typedef struct {
    Library    library;
    Font       font;
    Texture2D* covers;
    size_t selected_album;
    size_t selected_track;

    float actual_album_offset;
    float wanted_album_offset;

    float track_scroll;
    float wanted_track_scroll;

    Pane pane;

    Audio audio;
} Wisp;


Wisp wisp_init(int argc, char** argv);
void wisp_update(Wisp* w);
void wisp_draw(const Wisp* w);

int main(int argc, char** argv) {
    if (argc > 1 && strcmp("--help", argv[1]) == 0) {
        printf("Usage:\n");
        printf("%s [--help]: print this help message\n", argv[0]);
        printf("%s <PATH>: use this path as the base of the music on your system\n", argv[0]);
        return 1;
    }
    Wisp ws = wisp_init(argc, argv);

    while (!WindowShouldClose()) {
        wisp_update(&ws);
        wisp_draw(&ws);
    }

    audio_stop_playback(&ws.audio);
    UnloadFont(ws.font);
    unload_library(&ws.library);
    free(ws.covers);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

const float ALBUM_COVER_SIDE_LENGTH = 128;

static void draw_album_list(const Wisp* w, Rectangle bound);
static void draw_tracklist(const Wisp* w, Rectangle bound);
static void draw_queue(const Wisp* w, Rectangle bound);

// TODO: Shift colors with the selected albums cover color
void wisp_draw(const Wisp* w) {
    const float window_w = GetScreenWidth();
    const float window_h = GetScreenHeight();
    BeginDrawing();
    ClearBackground(BG_COLOR);

    switch (w->pane) {
        case PANE_ALBUMS: {
            const Rectangle side_bar = {
                .width = window_w,
                .height = ALBUM_COVER_SIDE_LENGTH,
            };
            draw_album_list(w, side_bar);

            const Rectangle track_list = {
                .x = 8,
                .y = side_bar.height + 8,
                .width = window_w,
                .height = window_h - side_bar.height
            };
            draw_tracklist(w, track_list);
            break;
        }
        case PANE_QUEUE: {
            const Rectangle track_list = {
                .x = 8,
                .y = 8,
                .width = window_w,
                .height = window_h
            };
            draw_queue(w, track_list);
            break;
        }
        case PANE_COUNT: assert(false);
    }
    EndDrawing();
}

static void draw_queue(const Wisp* w, Rectangle bound) {
        const float child_spacing = 8;
        for (size_t i = 0; i < w->audio.queue.tracks.count; i++) {
            const float font_size = 24;
            float y = (font_size + child_spacing) * i
                      + bound.y
                      - w->track_scroll;
            const Color text_color = i == 0 ? WHITE : GRAY;
            DrawTextEx(w->font, w->audio.queue.tracks.items[i]->title,
                       (Vector2){ bound.x, y },
                       font_size, 0.0, text_color);
        }
}

static void draw_tracklist(const Wisp* w, Rectangle bound) {
    BeginScissorMode(bound.x, bound.y, bound.width, bound.height);
    const float child_spacing = 8;
    Album* selected = &w->library.albums.items[w->selected_album];
    for (size_t i = 0; i < selected->tracks.count; i++) {
        const float font_size = 24;

        float y = (font_size + child_spacing) * i
                  + bound.y
                  - w->track_scroll;

        if (y < bound.y - font_size || y > bound.y + bound.height) continue;

        if (i == w->selected_track) {
            DrawTextEx(w->font, selected->tracks.items[i]->title,
                       (Vector2){ bound.x, y },
                       font_size, 0.0, WHITE);
        } else {
            DrawTextEx(w->font, selected->tracks.items[i]->title,
                       (Vector2){ bound.x, y },
                       font_size, 0.0, GRAY);
        }
    }
    EndScissorMode();
}

static void draw_album_list(const Wisp* w, Rectangle bound) {
    BeginScissorMode(bound.x, bound.y, bound.width, bound.height);
        for (size_t i = 0; i < w->library.albums.count; i++) {
            const Rectangle cover_rect_dest = {
                .width = ALBUM_COVER_SIDE_LENGTH,
                .height = ALBUM_COVER_SIDE_LENGTH,
                .x = i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x,
            };
            const Rectangle cover_rect_src = {
                .width = w->covers[i].width,
                .height = w->covers[i].height,
            };
            DrawTexturePro(w->covers[i], cover_rect_src, cover_rect_dest, (Vector2){}, 0.0, WHITE);
            if (i != w->selected_album) {
                DrawRectangleRec(cover_rect_dest, GetColor(0x000000aa));
            }
        }
    EndScissorMode();
}

void wisp_update(Wisp* wisp) {
    // todo: if (ctrl && IsKeyPressed(KEY_Q)) w->show_queue = !w->show_queue;
    // todo: prev track with SHIFT-comma?
   
    const bool  ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool  shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

    // screen controls
    {
        if (IsKeyPressed(KEY_TAB)) {
            wisp->pane++;
            wisp->pane %= PANE_COUNT;
        }
    }

    // audio related shit
    { 
        if (IsKeyPressed(KEY_SPACE)) audio_toggle_playing_state(&wisp->audio);

        if (IsKeyPressed(KEY_PERIOD) && shift) audio_skip_track_forward(&wisp->audio);
        // todo: prev track with SHIFT-comma?
        
        if (IsKeyPressed(KEY_RIGHT)) audio_try_seeking_by(&wisp->audio, 5.0);
        if (IsKeyPressed(KEY_LEFT)) audio_try_seeking_by(&wisp->audio, -5.0);
        audio_update(&wisp->audio);
    } 
    // album selection top or side bar still dont know?
    {
        if (IsKeyPressed(KEY_L) && wisp->selected_album < wisp->library.albums.count - 1) {
            wisp->wanted_album_offset -= ALBUM_COVER_SIDE_LENGTH;
            wisp->selected_album++;
            wisp->selected_track = 0;
        }

        if (IsKeyPressed(KEY_H) && wisp->selected_album != 0) {
            wisp->wanted_album_offset += ALBUM_COVER_SIDE_LENGTH;
            wisp->selected_album--;
            wisp->selected_track = 0;
        }
    }

    // track selection
    {
        Album* selected_album = &wisp->library.albums.items[wisp->selected_album];
        size_t album_track_count = selected_album->tracks.count;
        if (IsKeyPressed(KEY_J) && wisp->selected_track < album_track_count - 1) wisp->selected_track++;
        if (IsKeyPressed(KEY_K) && wisp->selected_track != 0) wisp->selected_track--;

        if (IsKeyPressed(KEY_ENTER)) audio_start_playback(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        // push currently selected song into the queue
        if (IsKeyPressed(KEY_Q) && !ctrl && !shift) audio_enqueue_single(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        // push songs from the current track to the end from the currently selected album
        if (shift && IsKeyPressed(KEY_Q)) {
            for (size_t i = wisp->selected_track; i < selected_album->tracks.count; i++) {
                audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
            }
        }
    }
    // track offset calculation :))
    {
        const float font_size = 24;
        const float spacing = 8;
        const float item_height = font_size + spacing;
        const float track_area_height = GetScreenHeight() - ALBUM_COVER_SIDE_LENGTH - 8;
        const float desired_y_from_top = track_area_height - item_height * 5;
        const float selected_y = wisp->selected_track * item_height;

        wisp->wanted_track_scroll = selected_y - desired_y_from_top;

        Album* album = &wisp->library.albums.items[wisp->selected_album];
        float max_scroll = fmaxf(0.0f, album->tracks.count * item_height - track_area_height);

        if (wisp->wanted_track_scroll < 0.0f)
            wisp->wanted_track_scroll = 0.0f;
        if (wisp->wanted_track_scroll > max_scroll)
            wisp->wanted_track_scroll = max_scroll;

        wisp->track_scroll += (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;
    }

    const float min_offset = fminf(0.0f, GetScreenWidth() - wisp->library.albums.count * ALBUM_COVER_SIDE_LENGTH);

    if (wisp->wanted_album_offset < min_offset)
        wisp->wanted_album_offset = min_offset;
    if (wisp->wanted_album_offset > 0.0)
        wisp->wanted_album_offset = 0.0;

    wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.1f;
}

Wisp wisp_init(int argc, char** argv) {
    char* prog = argv[0];
    char* home = getenv("HOME");
    char default_path[512] = {0};
    snprintf(default_path, 512, "%s/Music/", home);
    char* path = default_path;
    if (argc > 1) path = argv[1];
    Library lib = prepare_library(path);

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    InitAudioDevice();
    SetTargetFPS(180);

    Font font = LoadFontEx("res/Iosevka.ttf", 24, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

    Image*     imgs         = malloc(lib.albums.count * sizeof(Image));
    Texture2D* tex          = malloc(lib.albums.count * sizeof(Texture2D));

    for (size_t i = 0; i < lib.albums.count; i++) {
        Track* t = lib.albums.items[i].tracks.items[0];
        imgs[i] = (Image){
            .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width   = t->cover_w,
            .height  = t->cover_h,
            .data    = t->cover,
            .mipmaps = 1,
        };
        tex[i] = LoadTextureFromImage(imgs[i]);
        free(lib.albums.items[i].tracks.items[0]->cover);
    }
    free(imgs);


    return (Wisp){
        .font           = font,
        .library        = lib,
        .covers         = tex,
    };
}
