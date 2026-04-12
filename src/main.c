#include <assert.h>
#include <math.h>
#include "../extern/rgfw.h"
#include "simp.h"
#include "audio.h"
#include "library.h"
#include "../extern/yar.h"

#define BG_COLOR           (SimpColor){.r = 0x18/255.0, .g = 0x18/255.0, .b = 0x18/255.0, .a = 1 }
#define DARKEN_OVERLAY_COLOR (SimpColor){.a = 0xaa/255.0 }

typedef enum {
    PANE_ALBUMS,
    PANE_QUEUE,
    PANE_COUNT,
} Pane;

typedef struct {
    SimpRender   renderer;
    Library      library;
    SimpFont     font;
    SimpTexture* covers;
    size_t       selected_album;
    size_t       selected_track;

    float actual_album_offset;
    float wanted_album_offset;

    float track_scroll;
    float wanted_track_scroll;

    Pane  pane;
    Audio audio;
} Wisp;

void wisp_init(Wisp* w);
void wisp_update(Wisp* w);
void wisp_draw(Wisp* w);

int main(void) {
    Wisp* ws = malloc(sizeof(Wisp));
    wisp_init(ws);

    while (!simp_should_close(&ws->renderer)) {
        RGFW_pollEvents();
        wisp_update(ws);
        wisp_draw(ws);
    }

    audio_stop_playback(&ws->audio);
    audio_deinit(&ws->audio);
    unload_library(&ws->library);
    free(ws->covers);
    free(ws);
    return 0;
}

const float ALBUM_COVER_SIDE_LENGTH = 128;

static void draw_album_list(Wisp* w, SimpRectangle bound);
static void draw_tracklist(Wisp* w, SimpRectangle bound);
static void draw_queue(Wisp* w, SimpRectangle bound);

// TODO: Shift colors with the selected album's cover color
void wisp_draw(Wisp* w) {
    const float window_w = simp_get_screen_w(&w->renderer);
    const float window_h = simp_get_screen_h(&w->renderer);
    simp_clear_background(&w->renderer, BG_COLOR);

    switch (w->pane) {
        case PANE_ALBUMS: {
            const SimpRectangle side_bar = {
                .w = window_w,
                .h = ALBUM_COVER_SIDE_LENGTH,
            };
            draw_album_list(w, side_bar);

            const SimpRectangle track_list = {
                .x = 8,
                .y = side_bar.h + 8,
                .w = window_w,
                .h = window_h - side_bar.h,
            };
            draw_tracklist(w, track_list);
            break;
        }
        case PANE_QUEUE: {
            const SimpRectangle track_list = {
                .x = 8,
                .y = 8,
                .w = window_w,
                .h = window_h,
            };
            draw_queue(w, track_list);
            break;
        }
        case PANE_COUNT: assert(false);
    }
    simp_flush(&w->renderer);
}

static void draw_queue(Wisp* w, SimpRectangle bound) {
    const float child_spacing = 8;
    for (size_t i = 0; i < w->audio.queue.tracks.count; i++) {
        const float font_size = 24;
        float y = (font_size + child_spacing) * i
                  + bound.y
                  - w->track_scroll;
        const SimpColor text_color = i == 0 ? SIMP_COLOR_WHITE : SIMP_COLOR_GRAY;
        simp_text(&w->renderer, &w->font, w->audio.queue.tracks.items[i]->title, bound.x, y, text_color);
    }
}

static void draw_tracklist(Wisp* w, SimpRectangle bound) {
    const float child_spacing = 8;
    Album* selected = &w->library.albums.items[w->selected_album];
    for (size_t i = 0; i < selected->tracks.count; i++) {
        const float font_size = 24;
        float y = (font_size + child_spacing) * i
                  + bound.y
                  - w->track_scroll;

        if (y < bound.y - font_size || y > bound.y + bound.h) continue;

        SimpColor color = (i == w->selected_track) ? SIMP_COLOR_WHITE : SIMP_COLOR_GRAY;
        simp_text(&w->renderer, &w->font, selected->tracks.items[i]->title, bound.x, y, color);
    }
}

static void draw_album_list(Wisp* w, SimpRectangle bound) {
    for (size_t i = 0; i < w->library.albums.count; i++) {
        const SimpRectangle cover_rect_dest = {
            .w = ALBUM_COVER_SIDE_LENGTH,
            .h = ALBUM_COVER_SIDE_LENGTH,
            .x = i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x,
        };
        const SimpRectangle cover_rect_src = {
            .w = w->covers[i].w,
            .h = w->covers[i].h,
        };
        simp_draw_texture_ex(&w->renderer, &w->covers[i], cover_rect_src, cover_rect_dest, SIMP_COLOR_WHITE);
        if (i != w->selected_album) {
            simp_rectangle(&w->renderer, cover_rect_dest, DARKEN_OVERLAY_COLOR);
        }
    }
}

void wisp_update(Wisp* wisp) {
    const bool ctrl  = RGFW_isKeyDown(RGFW_keyControlL) || RGFW_isKeyDown(RGFW_keyControlR);
    const bool shift = RGFW_isKeyDown(RGFW_keyShiftR)   || RGFW_isKeyDown(RGFW_keyShiftL);

    if (RGFW_isKeyPressed(RGFW_keyTab)) {
        wisp->pane = (wisp->pane + 1) % PANE_COUNT;
    }

    if (RGFW_isKeyPressed(RGFW_keySpace))               audio_toggle_playing_state(&wisp->audio);
    if (RGFW_isKeyPressed(RGFW_keyPeriod) && shift)     audio_skip_track_forward(&wisp->audio);
    if (RGFW_isKeyPressed(RGFW_keyRight))               audio_try_seeking_by(&wisp->audio,  5.0f);
    if (RGFW_isKeyPressed(RGFW_keyLeft))                audio_try_seeking_by(&wisp->audio, -5.0f);
    audio_update(&wisp->audio);

    if (RGFW_isKeyPressed(RGFW_keyL) && wisp->selected_album < wisp->library.albums.count - 1) {
        wisp->wanted_album_offset -= ALBUM_COVER_SIDE_LENGTH;
        wisp->selected_album++;
        wisp->selected_track = 0;
    }
    if (RGFW_isKeyPressed(RGFW_keyH) && wisp->selected_album != 0) {
        wisp->wanted_album_offset += ALBUM_COVER_SIDE_LENGTH;
        wisp->selected_album--;
        wisp->selected_track = 0;
    }

    {
        Album* selected_album    = &wisp->library.albums.items[wisp->selected_album];
        size_t album_track_count = selected_album->tracks.count;

        if (RGFW_isKeyPressed(RGFW_keyJ) && wisp->selected_track < album_track_count - 1) wisp->selected_track++;
        if (RGFW_isKeyPressed(RGFW_keyK) && wisp->selected_track != 0)                    wisp->selected_track--;

        if (RGFW_isKeyPressed(RGFW_keyEnter))
            audio_start_playback(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        if (RGFW_isKeyPressed(RGFW_keyQ) && !ctrl && !shift)
            audio_enqueue_single(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        if (shift && RGFW_isKeyPressed(RGFW_keyQ)) {
            for (size_t i = wisp->selected_track; i < selected_album->tracks.count; i++)
                audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
        }
    }

    {
        const float font_size        = 24;
        const float spacing          = 8;
        const float item_height      = font_size + spacing;
        const float track_area_h     = simp_get_screen_h(&wisp->renderer) - ALBUM_COVER_SIDE_LENGTH - 8;
        const float desired_y        = track_area_h - item_height * 5;
        const float selected_y       = wisp->selected_track * item_height;

        wisp->wanted_track_scroll = selected_y - desired_y;

        Album* album     = &wisp->library.albums.items[wisp->selected_album];
        float  max_scroll = fmaxf(0.0f, album->tracks.count * item_height - track_area_h);
        if (wisp->wanted_track_scroll < 0.0f)       wisp->wanted_track_scroll = 0.0f;
        if (wisp->wanted_track_scroll > max_scroll) wisp->wanted_track_scroll = max_scroll;

        wisp->track_scroll += (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;
    }

    {
        const float min_offset = fminf(0.0f,
            simp_get_screen_w(&wisp->renderer) - wisp->library.albums.count * ALBUM_COVER_SIDE_LENGTH);

        if (wisp->wanted_album_offset < min_offset) wisp->wanted_album_offset = min_offset;
        if (wisp->wanted_album_offset > 0.0f)       wisp->wanted_album_offset = 0.0f;

        wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.1f;
    }
}

void wisp_init(Wisp* w) {
    memset(w, 0, sizeof(*w));

    w->library  = prepare_library("/home/shr/Downloads/Nicotine");
    w->renderer = simp_init("wispy", 1280, 720);
    w->font     = simp_font_load("res/Iosevka.ttf", 24);

    w->covers = malloc(w->library.albums.count * sizeof(SimpTexture));
    for (size_t i = 0; i < w->library.albums.count; i++) {
        Track* t = w->library.albums.items[i].tracks.items[0];
        w->covers[i] = simp_load_texture_from_pixels(t->cover, t->cover_w, t->cover_h);
        free(t->cover);
    }

    audio_init(&w->audio);
}
