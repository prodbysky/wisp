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
  Library library;
  Font font;
  Texture2D *covers;
  size_t selected_album;
  size_t selected_track;

  float actual_album_offset;
  float wanted_album_offset;

  float track_scroll;
  float wanted_track_scroll;

  Pane pane;

  Audio audio;

  Color *album_average_colors;
  Color last_color;
  float last_switch;
} Wisp;

static float color_luminance(Color c) {
  float r = c.r / 255.0f;
  float g = c.g / 255.0f;
  float b = c.b / 255.0f;

  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

Wisp wisp_init(int argc, char **argv);
void wisp_update(Wisp *w);
void wisp_draw(const Wisp *w);

typedef struct {
  Color focused_text;
  Color unfocused_text;
  Color shadow;
  Color rectangle;
} Theme;

Theme wisp_derive_theme(const Wisp *w);

int main(int argc, char **argv) {
  if (argc > 1 && strcmp("--help", argv[1]) == 0) {
    printf("Usage:\n");
    printf("%s [--help]: print this help message\n", argv[0]);
    printf("%s <PATH>: use this path as the base of the music on your system\n",
           argv[0]);
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

static void draw_album_list(const Wisp *w, Rectangle bound);
static void draw_tracklist(const Wisp *w, Rectangle bound);
static void draw_queue(const Wisp *w, Rectangle bound);

// TODO: Shift colors with the selected albums cover color
void wisp_draw(const Wisp *w) {
  const float window_w = GetScreenWidth();
  const float window_h = GetScreenHeight();
  BeginDrawing();
  float t = (GetTime() - w->last_switch) / 0.2;
  if (t < 0)
    t = 0;
  if (t > 1)
    t = 1;
  ClearBackground(ColorLerp(
      w->last_color,
      ColorBrightness(w->album_average_colors[w->selected_album], 0.0), t));

  switch (w->pane) {
  case PANE_ALBUMS: {
    const Rectangle side_bar = {
        .width = window_w,
        .height = ALBUM_COVER_SIDE_LENGTH,
    };
    draw_album_list(w, side_bar);

    const Rectangle track_list = {.x = 8,
                                  .y = side_bar.height + 8,
                                  .width = window_w,
                                  .height = window_h - side_bar.height};
    draw_tracklist(w, track_list);
    break;
  }
  case PANE_QUEUE: {
    const Rectangle track_list = {
        .x = 8, .y = 8, .width = window_w, .height = window_h};
    draw_queue(w, track_list);
    break;
  }
  case PANE_COUNT:
    assert(false);
  }
  EndDrawing();
}

static void draw_queue(const Wisp *w, Rectangle bound) {
  const float font_size = 24.0f;
  const float child_spacing = 8.0f;
  const float item_height = font_size + child_spacing;

  const Theme theme = wisp_derive_theme(w);

  float center_y = bound.y + bound.height * 0.5f;

  if (w->audio.current_track) {
    Rectangle rect = {
        .x = bound.x,
        .y = center_y,
        .width = GetScreenWidth() - bound.x * 2,
        .height = font_size + 4,
    };

    DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);

    DrawTextEx(w->font, w->audio.current_track->title,
               (Vector2){bound.x + 5, center_y + 3}, font_size, 0.0f,
               theme.focused_text);
  }

  for (size_t i = 0; i < w->audio.queue.history.items.count; i++) {
    size_t idx = w->audio.queue.history.items.count - 1 - i;

    float y = center_y - item_height * (i + 1);

    Rectangle rect = {
        .x = bound.x,
        .y = y,
        .width = GetScreenWidth() - bound.x * 2,
        .height = font_size + 4,
    };

    DrawRectangleRounded(rect, 0.25f, 17,
                         ColorBrightness(theme.rectangle, -0.2f));

    DrawTextEx(w->font, w->audio.queue.history.items.items[idx]->title,
               (Vector2){bound.x + 5, y + 3}, font_size, 0.0f,
               ColorBrightness(theme.focused_text, -0.2f));
  }

  for (size_t i = 0; i < w->audio.queue.upcoming.items.count; i++) {
    float y = center_y + item_height * (i + 1);

    Rectangle rect = {
        .x = bound.x,
        .y = y,
        .width = GetScreenWidth() - bound.x * 2,
        .height = font_size + 4,
    };

    DrawRectangleRounded(rect, 0.25f, 17,
                         ColorBrightness(theme.rectangle, -0.2f));

    DrawTextEx(w->font, w->audio.queue.upcoming.items.items[i]->title,
               (Vector2){bound.x + 5, y + 3}, font_size, 0.0f,
               ColorBrightness(theme.focused_text, -0.2f));
  }
}

Theme wisp_derive_theme(const Wisp *w) {
  Color base = w->album_average_colors[w->selected_album];
  float lum = color_luminance(base);

  Color readable = (lum > 0.5f) ? BLACK : WHITE;

  Color styled = ColorLerp(readable, base, 0.15f);

  Color focused_text_color = styled;
  Color unfocused_text_color = ColorAlpha(styled, 0.0f);

  Color rect_color =
      (lum > 0.5f) ? ColorLerp(base, BLACK, 0.5) : ColorLerp(base, WHITE, 0.5);
  Color shadow_color = (lum > 0.5) ? BLACK : WHITE;
  shadow_color = ColorAlpha(shadow_color, 0.8);
  return (Theme){.focused_text = styled,
                 .unfocused_text = ColorAlpha(styled, -.2),
                 .rectangle = rect_color,
                 .shadow = shadow_color};
}

static void draw_tracklist(const Wisp *w, Rectangle bound) {
  BeginScissorMode(bound.x, bound.y, bound.width, bound.height);

  const float font_size = 24;
  const float child_spacing = 8;

  Album *selected = &w->library.albums.items[w->selected_album];

  const Theme theme = wisp_derive_theme(w);

  for (size_t i = 0; i < selected->tracks.count; i++) {
    float y = (font_size + child_spacing) * i + bound.y - w->track_scroll;

    if (y < bound.y - font_size || y > bound.y + bound.height)
      continue;

    Rectangle rect = {
        .x = bound.x,
        .y = y,
        .width = GetScreenWidth() - bound.x * 2,
        .height = font_size + 4,
    };

    DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);

    const char *title = selected->tracks.items[i]->title;

    Color text_color =
        (i == w->selected_track) ? theme.focused_text : theme.unfocused_text;

    DrawTextEx(w->font, title, (Vector2){rect.x + 5, rect.y + 3}, font_size,
               0.0f, theme.shadow);

    DrawTextEx(w->font, title, (Vector2){rect.x + 4, rect.y + 2}, font_size,
               0.0f, text_color);
  }

  EndScissorMode();
}
static void draw_album_list(const Wisp *w, Rectangle bound) {
  BeginScissorMode(bound.x, bound.y, bound.width, bound.height);
  for (size_t i = 0; i < w->library.albums.count; i++) {
    if ((i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x) >
        bound.width)
      continue;
    const Rectangle cover_rect_dest = {
        .width = ALBUM_COVER_SIDE_LENGTH,
        .height = ALBUM_COVER_SIDE_LENGTH,
        .x = i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x,
    };
    const Rectangle cover_rect_src = {
        .width = w->covers[i].width,
        .height = w->covers[i].height,
    };
    DrawTexturePro(w->covers[i], cover_rect_src, cover_rect_dest, (Vector2){},
                   0.0, WHITE);
    if (i != w->selected_album) {
      DrawRectangleRec(cover_rect_dest, GetColor(0x000000aa));
    }
  }
  EndScissorMode();
}

void wisp_update(Wisp *wisp) {

  const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
  const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

  // screen controls
  {
    if (IsKeyPressed(KEY_TAB)) {
      wisp->pane++;
      wisp->pane %= PANE_COUNT;
    }
  }

  // audio related shit
  {
    if (IsKeyPressed(KEY_SPACE))
      audio_toggle_playing_state(&wisp->audio);

    if (IsKeyPressed(KEY_PERIOD) && shift)
      audio_skip_track_forward(&wisp->audio);

    if (IsKeyPressed(KEY_RIGHT))
      audio_try_seeking_by(&wisp->audio, 5.0);
    if (IsKeyPressed(KEY_LEFT))
      audio_try_seeking_by(&wisp->audio, -5.0);

    if (IsKeyPressed(KEY_COMMA) && shift) {
      audio_skip_track_backward(&wisp->audio);
    }
    if (IsKeyPressed(KEY_S) && ctrl) {
      wisp->audio.shuffle = !wisp->audio.shuffle;
    }

    // cycle loop mode
    if (IsKeyPressed(KEY_R) && ctrl) {
      wisp->audio.loop_mode++;
      if (wisp->audio.loop_mode > LOOP_ALL)
        wisp->audio.loop_mode = LOOP_NONE;
    }
    audio_update(&wisp->audio);
  }
  // album selection top or side bar still dont know?
  {
    if (IsKeyPressed(KEY_L) &&
        wisp->selected_album < wisp->library.albums.count - 1) {
      wisp->wanted_album_offset -= ALBUM_COVER_SIDE_LENGTH;
      wisp->last_color = wisp->album_average_colors[wisp->selected_album];
      wisp->last_switch = GetTime();
      wisp->selected_album++;
      wisp->selected_track = 0;
    }

    if (IsKeyPressed(KEY_H) && wisp->selected_album != 0) {
      wisp->wanted_album_offset += ALBUM_COVER_SIDE_LENGTH;
      wisp->last_color = wisp->album_average_colors[wisp->selected_album];
      wisp->last_switch = GetTime();
      wisp->selected_album--;
      wisp->selected_track = 0;
    }
  }

  // track selection
  {
    Album *selected_album = &wisp->library.albums.items[wisp->selected_album];
    size_t album_track_count = selected_album->tracks.count;
    if (IsKeyPressed(KEY_J) && wisp->selected_track < album_track_count - 1)
      wisp->selected_track++;
    if (IsKeyPressed(KEY_K) && wisp->selected_track != 0)
      wisp->selected_track--;

    if (IsKeyPressed(KEY_ENTER))
      audio_start_playback(&wisp->audio,
                           selected_album->tracks.items[wisp->selected_track]);

    // push currently selected song into the queue
    if (IsKeyPressed(KEY_Q) && !ctrl && !shift)
      audio_enqueue_single(&wisp->audio,
                           selected_album->tracks.items[wisp->selected_track]);

    // push songs from the current track to the end from the currently selected
    // album
    if (shift && IsKeyPressed(KEY_Q)) {
      for (size_t i = wisp->selected_track; i < selected_album->tracks.count;
           i++) {
        audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
      }
    }
  }
  // track offset calculation :))
  {
    const float font_size = 24;
    const float spacing = 8;
    const float item_height = font_size + spacing;
    const float track_area_height =
        GetScreenHeight() - ALBUM_COVER_SIDE_LENGTH - 8;
    const float desired_y_from_top = track_area_height - item_height * 5;
    const float selected_y = wisp->selected_track * item_height;

    wisp->wanted_track_scroll = selected_y - desired_y_from_top;

    Album *album = &wisp->library.albums.items[wisp->selected_album];
    float max_scroll =
        fmaxf(0.0f, album->tracks.count * item_height - track_area_height);

    if (wisp->wanted_track_scroll < 0.0f)
      wisp->wanted_track_scroll = 0.0f;
    if (wisp->wanted_track_scroll > max_scroll)
      wisp->wanted_track_scroll = max_scroll;

    wisp->track_scroll +=
        (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;
  }

  const float min_offset =
      fminf(0.0f, GetScreenWidth() -
                      wisp->library.albums.count * ALBUM_COVER_SIDE_LENGTH);

  if (wisp->wanted_album_offset < min_offset)
    wisp->wanted_album_offset = min_offset;
  if (wisp->wanted_album_offset > 0.0)
    wisp->wanted_album_offset = 0.0;

  wisp->actual_album_offset +=
      (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.1f;
}

Wisp wisp_init(int argc, char **argv) {
  char *prog = argv[0];
  char *home = getenv("HOME");
  char default_path[512] = {0};
  snprintf(default_path, 512, "%s/Music/", home);
  char *path = default_path;
  if (argc > 1)
    path = argv[1];
  Library lib = prepare_library(path);

  SetWindowState(FLAG_MSAA_4X_HINT);
  InitWindow(1280, 720, "wispy");
  SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
  SetWindowState(FLAG_WINDOW_RESIZABLE);
  InitAudioDevice();
  SetTargetFPS(180);

  Font font = LoadFontEx("res/Iosevka.ttf", 24, NULL, 0);
  SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

  Image *imgs = malloc(lib.albums.count * sizeof(Image));
  Texture2D *tex = malloc(lib.albums.count * sizeof(Texture2D));
  Color *tints = malloc(lib.albums.count * sizeof(Color));

  for (size_t i = 0; i < lib.albums.count; i++) {
    float r = 0, g = 0, b = 0;
    Track *t = lib.albums.items[i].tracks.items[0];
    imgs[i] = (Image){
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
        .width = t->cover_w,
        .height = t->cover_h,
        .data = t->cover,
        .mipmaps = 1,
    };
    int sample_count = 10;
    int count = 0;
    for (int y = 0; y < t->cover_h; y += sample_count) {
      for (int x = 0; x < t->cover_w; x += sample_count) {
        size_t i = (y * t->cover_w + x) * 3;
        r += (float)t->cover[i] / 255;
        g += (float)t->cover[i + 1] / 255;
        b += (float)t->cover[i + 2] / 255;
        count++;
      }
    }
    r /= (float)count;
    g /= (float)count;
    b /= (float)count;
    r *= 255;
    g *= 255;
    b *= 255;
    tints[i].r = r;
    tints[i].g = g;
    tints[i].b = b;
    tints[i].a = 1;
    tex[i] = LoadTextureFromImage(imgs[i]);
  }

  return (Wisp){.font = font,
                .library = lib,
                .covers = tex,
                .album_average_colors = tints};
}
