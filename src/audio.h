#pragma once
#include "../extern/yar.h"
#include "library.h"

typedef struct {
    yar(Track*) tracks;
} Queue;



typedef struct {
    Track* current_track;
    bool   playing;
    Music  music;
    Queue  queue;
} Audio;

void audio_stop_playback(Audio* a);
void audio_start_playback(Audio* a, Track* track);
void audio_toggle_playing_state(Audio* a);

bool audio_has_loaded_track(const Audio* a);

void audio_advance_queue(Audio* a);
void audio_enqueue_single(Audio* a, Track* track);

void audio_update(Audio* a);

bool audio_queue_is_empty(const Audio* a);

void audio_try_seeking_by(Audio* a, float diff);
float audio_get_current_track_progress(const Audio* a);

const char* audio_get_current_track_title(const Audio* a);

void audio_skip_track_forward(Audio* a);

