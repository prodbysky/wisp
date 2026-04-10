#include "audio.h"
#include "library.h"

#define BG_COLOR GetColor(0x181818ff)

typedef struct {
    Library    library;
    Font       font;
    Texture2D* covers;
    size_t selected_album;
    size_t selected_track;
    Audio audio;
} Wisp;



Wisp wisp_init(void);
void wisp_update(Wisp* w);
void wisp_draw(const Wisp* w);

int main(void) {
    Wisp ws = wisp_init();

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

void wisp_draw(const Wisp* w) {

    BeginDrawing();
    ClearBackground(BG_COLOR);
    EndDrawing();
}

void wisp_update(Wisp* wisp) {
    // todo: if (ctrl && IsKeyPressed(KEY_Q)) w->show_queue = !w->show_queue;
    // todo: prev track with SHIFT-comma?
   
    const float delta = GetFrameTime();
    const bool  ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool  shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

    // audio related shit
        { 
        if (IsKeyPressed(KEY_SPACE)) {
            audio_toggle_playing_state(&wisp->audio);
        }

        if (IsKeyPressed(KEY_PERIOD) && shift) audio_skip_track_forward(&wisp->audio);
        // todo: prev track with SHIFT-comma?
        
        float seek = 0.0f;
        if (IsKeyPressed(KEY_RIGHT)) audio_try_seeking_by(&wisp->audio, 5.0);
        if (IsKeyPressed(KEY_LEFT)) audio_try_seeking_by(&wisp->audio, -5.0);
        audio_update(&wisp->audio);
    } 
    // album selection top or side bar still dont know?
    {
        if (IsKeyPressed(KEY_L) && wisp->selected_album < wisp->library.albums.count - 1) {
            wisp->selected_album++;
        }

        if (IsKeyPressed(KEY_H) && wisp->selected_album != 0) {
            wisp->selected_album--;
        }
    }

    // track selection
    {
        Album* selected_album = &wisp->library.albums.items[wisp->selected_album];
        size_t album_track_count = selected_album->tracks.count;
        if (IsKeyPressed(KEY_J) && wisp->selected_track < album_track_count - 1) {
            wisp->selected_track++;
        }
        if (IsKeyPressed(KEY_K) && wisp->selected_track != 0) {
            wisp->selected_track--;
        }

        if (IsKeyPressed(KEY_ENTER)) {
            audio_start_playback(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);
        }

        // push currently selected song into the queue
        if (IsKeyPressed(KEY_Q) && !ctrl && !shift) {
            audio_enqueue_single(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);
        }

        // push songs from the current track to the end from the currently selected album
        if (shift && IsKeyPressed(KEY_Q)) {
            for (size_t i = wisp->selected_track; i < selected_album->tracks.count; i++) {
                audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
            }
        }
    }

    
}

Wisp wisp_init(void) {
    Library lib = prepare_library("/home/shr/Downloads/Nicotine");

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
