#include "audio.h"

#include <stdlib.h>

#include "../extern/raylib/src/raymath.h"

static void list_push(TrackList* l, Track* t);
static Track* list_pop_front(TrackList* l);
static Track* list_pop_back(TrackList* l);
static void list_remove(TrackList* l, size_t index);
static size_t list_random_index(TrackList* l);

static Track* queue_next(Audio* a);
static Track* queue_prev(Audio* a);

Audio audio_init() { return (Audio){.master_volume = 1}; }

void audio_start_playback(Audio* a, Track* track) {
    if (track == NULL) return;

    // cancel any in-progress crossfade
    if (a->next_track != NULL) {
        StopMusicStream(a->next_music);
        UnloadMusicStream(a->next_music);
        a->next_track = NULL;
        a->crossfade_progress = 0.0f;
    }

    if (a->current_track != NULL) {
        StopMusicStream(a->music);
        UnloadMusicStream(a->music);
    }

    a->music = LoadMusicStream(track->path);
    a->music.looping = false;
    a->current_track = track;
    a->playing = true;
    a->crossfade_duration = 2.0f;

    PlayMusicStream(a->music);
    SetMusicVolume(a->music, 1.0f);
}

void audio_stop_playback(Audio* a) {
    if (a->next_track != NULL) {
        StopMusicStream(a->next_music);
        UnloadMusicStream(a->next_music);
        a->next_track = NULL;
        a->crossfade_progress = 0.0f;
    }

    if (a->current_track == NULL) return;

    StopMusicStream(a->music);
    UnloadMusicStream(a->music);
    a->current_track = NULL;
    a->playing = false;
}
bool audio_has_loaded_track(const Audio* a) { return a->current_track != NULL; }

const char* audio_get_current_track_title(const Audio* a) { return a->current_track ? a->current_track->title : NULL; }

bool audio_queue_is_empty(const Audio* a) { return a->queue.upcoming.items.count == 0; }

void audio_enqueue_single(Audio* a, Track* track) { list_push(&a->queue.upcoming, track); }

void audio_set_shuffle(Audio* a, bool enabled) { a->shuffle = enabled; }

void audio_set_loop_mode(Audio* a, LoopMode mode) { a->loop_mode = mode; }

static Track* queue_next(Audio* a) {
    if (a->loop_mode == LOOP_ONE && a->current_track) { return a->current_track; }

    if (a->current_track) { list_push(&a->queue.history, a->current_track); }

    if (audio_queue_is_empty(a)) {
        if (a->loop_mode == LOOP_ALL && a->queue.history.items.count > 0) {
            a->queue.upcoming = a->queue.history;
            a->queue.history.items.count = 0;
        } else {
            return NULL;
        }
    }

    if (a->shuffle) {
        size_t idx = list_random_index(&a->queue.upcoming);
        Track* t = a->queue.upcoming.items.items[idx];
        list_remove(&a->queue.upcoming, idx);
        return t;
    }

    return list_pop_front(&a->queue.upcoming);
}

static Track* queue_prev(Audio* a) {
    if (a->queue.history.items.count == 0) return NULL;

    if (a->current_track) { list_push(&a->queue.upcoming, a->current_track); }

    return list_pop_back(&a->queue.history);
}

void audio_skip_track_forward(Audio* a) {
    Track* next = queue_next(a);

    if (!next) {
        audio_stop_playback(a);
    } else {
        audio_start_playback(a, next);
    }
}

void audio_skip_track_backward(Audio* a) {
    Track* prev = queue_prev(a);

    if (prev) { audio_start_playback(a, prev); }
}

void audio_update(Audio* a) {
    if (!a->current_track) return;

    UpdateMusicStream(a->music);
    if (a->next_track != NULL) UpdateMusicStream(a->next_music);

    if (!a->playing) return;

    float len = GetMusicTimeLength(a->music);
    float played = GetMusicTimePlayed(a->music);
    float remaining = len - played;

    if (a->next_track == NULL && remaining <= a->crossfade_duration && remaining > 0.0f) {
        Track* next = queue_next(a);
        if (next != NULL) {
            a->next_track = next;
            a->next_music = LoadMusicStream(next->path);
            a->next_music.looping = false;
            PlayMusicStream(a->next_music);
            SetMusicVolume(a->next_music, 0.0f);
            a->crossfade_progress = 0.0f;
        }
    }

    if (a->next_track != NULL) {
        a->crossfade_progress = 1.0f - (remaining / a->crossfade_duration);
        if (a->crossfade_progress < 0.0f) a->crossfade_progress = 0.0f;
        if (a->crossfade_progress > 1.0f) a->crossfade_progress = 1.0f;

        float t = a->crossfade_progress;
        float t_in = t * t * (3.0f - 2.0f * t);
        float t_out = 1.0f - t_in;

        SetMusicVolume(a->music, t_out);
        SetMusicVolume(a->next_music, t_in);

        if (!IsMusicStreamPlaying(a->music) || a->crossfade_progress >= 1.0f) {
            StopMusicStream(a->music);
            UnloadMusicStream(a->music);

            a->music = a->next_music;
            a->current_track = a->next_track;
            a->next_track = NULL;
            a->crossfade_progress = 0.0f;

            SetMusicVolume(a->music, 1.0f);
        }
    } else if (!IsMusicStreamPlaying(a->music)) {
        Track* next = queue_next(a);
        if (!next) {
            audio_stop_playback(a);
        } else {
            audio_start_playback(a, next);
        }
    }
}

void audio_toggle_playing_state(Audio* a) {
    if (!a->current_track) return;

    if (a->playing) {
        PauseMusicStream(a->music);
    } else {
        ResumeMusicStream(a->music);
    }

    a->playing = !a->playing;
}

float audio_get_current_track_progress(const Audio* a) {
    if (!a->current_track) return 0.0f;

    float len = GetMusicTimeLength(a->music);
    if (len <= 0.0f) return 0.0f;

    return GetMusicTimePlayed(a->music) / len;
}

void audio_try_seeking_by(Audio* a, float diff) {
    if (!a->current_track || diff == 0.0f) return;

    float pos = GetMusicTimePlayed(a->music) + diff;
    float len = GetMusicTimeLength(a->music);

    if (pos < 0.0f) pos = 0.0f;
    if (pos > len) pos = len;

    SeekMusicStream(a->music, pos);
}

void audio_set_master_volume(Audio* a, float volume) {
    a->master_volume = Clamp(volume, 0, 1);
    SetMasterVolume(a->master_volume);
}

void audio_change_master_volume_by(Audio* a, float volume) {
    a->master_volume = Clamp(a->master_volume + volume, 0, 1);
    SetMasterVolume(a->master_volume);
}

static void list_push(TrackList* l, Track* t) { *yar_append(&l->items) = t; }

static Track* list_pop_front(TrackList* l) {
    if (l->items.count == 0) return NULL;

    Track* t = l->items.items[0];
    yar_remove(&l->items, 0, 1);
    return t;
}

static Track* list_pop_back(TrackList* l) {
    if (l->items.count == 0) return NULL;

    size_t i = l->items.count - 1;
    Track* t = l->items.items[i];
    yar_remove(&l->items, i, 1);
    return t;
}

static void list_remove(TrackList* l, size_t index) { yar_remove(&l->items, index, 1); }

static size_t list_random_index(TrackList* l) { return (size_t)(rand() % l->items.count); }
