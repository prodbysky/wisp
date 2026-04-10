#include "audio.h"


static void queue_push(Queue* q, Track* track);
static void queue_remove(Queue* q, size_t n);
static Track* queue_advance(Queue* q);


void audio_stop_playback(Audio* a) {
    if (a->current_track == NULL) return;
    StopMusicStream(a->music);
    UnloadMusicStream(a->music);
    a->current_track = NULL;
    a->playing       = false;
}

bool audio_has_loaded_track(const Audio* a) {
    return a->current_track != NULL;
}

const char* audio_get_current_track_title(const Audio* a) {
    if (a->current_track == NULL) return NULL;
    return a->current_track->title;
}

void audio_start_playback(Audio* a, Track* track) {
    a->music             = LoadMusicStream(track->path);
    a->music.looping     = false;
    a->current_track = track;
    a->playing       = true;
    PlayMusicStream(a->music);
}

bool audio_queue_is_empty(const Audio* a) {
    return a->queue.tracks.count == 0;
}

void audio_advance_queue(Audio* a) {
    Track* next = queue_advance(&a->queue);
    if (next == NULL) {
        audio_stop_playback(a);
    }
    else {
        audio_start_playback(a, next);
    }
}

float audio_get_current_track_progress(const Audio* a) {
    if (a->current_track == NULL) return 0.0f;
    return GetMusicTimeLength(a->music) > 0.0f
        ? GetMusicTimePlayed(a->music) / GetMusicTimeLength(a->music)
        : 0.0f;
}

void audio_update(Audio* a) {
    if (a->current_track != NULL) {
        UpdateMusicStream(a->music);
        if (a->playing && !IsMusicStreamPlaying(a->music)) audio_advance_queue(a);
    }
}

void audio_toggle_playing_state(Audio* a) {
    if (a->current_track != NULL) {
        if (a->playing) { 
            PauseMusicStream(a->music);
        }
        else { 
            ResumeMusicStream(a->music);
        }
        a->playing = !a->playing;
    }
}

void audio_enqueue_single(Audio* a, Track* track) {
    queue_push(&a->queue, track);
}

void audio_try_seeking_by(Audio* a, float diff) {
    if (diff != 0.0f) {
        float next_pos = GetMusicTimePlayed(a->music) + diff;
        float len = GetMusicTimeLength(a->music);
        if (next_pos < 0.0f) next_pos = 0.0f;
        if (next_pos > len)  next_pos = len;
        SeekMusicStream(a->music, next_pos);
    }
}

void audio_skip_track(Audio* a) {
    Track* next = queue_advance(&a->queue);
    if (next == NULL) {
        audio_stop_playback(a);
    }
    else {
        audio_start_playback(a, next);
    }
}

static void queue_push(Queue* q, Track* track) {
    *yar_append(&q->tracks) = track;
}

static void queue_remove(Queue* q, size_t n) {
    yar_remove(&q->tracks, n, 1);
}

static Track* queue_advance(Queue* q) {
    if (q->tracks.count == 0) return NULL;
    Track* next = q->tracks.items[0];
    queue_remove(q, 0);
    return next;
}
