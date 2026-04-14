#pragma once
#include "../extern/yar.h"
#include "library.h"

typedef struct {
    yar(Track*) items;
} TrackList;

typedef struct {
    TrackList upcoming;
    TrackList history;
} Queue;

typedef enum { LOOP_NONE, LOOP_ONE, LOOP_ALL } LoopMode;

typedef struct {
    Track* current_track;
    bool playing;
    Music music;

    Queue queue;

    bool shuffle;
    LoopMode loop_mode;
} Audio;

void audio_start_playback(Audio* a, Track* track);
void audio_stop_playback(Audio* a);
void audio_toggle_playing_state(Audio* a);

void audio_enqueue_single(Audio* a, Track* track);
void audio_skip_track_forward(Audio* a);
void audio_skip_track_backward(Audio* a);

void audio_set_shuffle(Audio* a, bool enabled);
void audio_set_loop_mode(Audio* a, LoopMode mode);

void audio_update(Audio* a);

bool audio_has_loaded_track(const Audio* a);
bool audio_queue_is_empty(const Audio* a);
float audio_get_current_track_progress(const Audio* a);
const char* audio_get_current_track_title(const Audio* a);

void audio_try_seeking_by(Audio* a, float diff);
