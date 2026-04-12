#include "audio.h"
#include <stdio.h>


static void queue_push(Queue* q, Track* track);
static void queue_remove(Queue* q, size_t n);
static Track* queue_advance(Queue* q);

void audio_init(Audio* a) {
    *a = (Audio){0};
    ma_result r = ma_engine_init(NULL, &a->engine);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "audio_init: ma_engine_init failed (%d)\n", r);
    }
}

void audio_deinit(Audio* a) {
    audio_stop_playback(a);
    ma_engine_uninit(&a->engine);
}

static float _sound_length_seconds(Audio* a) {
    if (!a->sound_initialized) return 0.0f;
    ma_uint64 frames = 0;
    ma_sound_get_length_in_pcm_frames(&a->sound, &frames);
    ma_uint32 rate = ma_engine_get_sample_rate(&a->engine);
    return rate > 0 ? (float)frames / (float)rate : 0.0f;
}

static float _sound_played_seconds(Audio* a) {
    if (!a->sound_initialized) return 0.0f;
    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(&a->sound, &cursor);
    ma_uint32 rate = ma_engine_get_sample_rate(&a->engine);
    return rate > 0 ? (float)cursor / (float)rate : 0.0f;
}

void audio_stop_playback(Audio* a) {
    if (!a->sound_initialized) return;
    ma_sound_stop(&a->sound);
    ma_sound_uninit(&a->sound);
    a->sound_initialized = false;
    a->current_track     = NULL;
    a->playing           = false;
}

bool audio_has_loaded_track(const Audio* a) {
    return a->current_track != NULL;
}

const char* audio_get_current_track_title(const Audio* a) {
    if (a->current_track == NULL) return NULL;
    return a->current_track->title;
}

void audio_start_playback(Audio* a, Track* track) {
    if (a->sound_initialized) {
        ma_sound_stop(&a->sound);
        ma_sound_uninit(&a->sound);
        a->sound_initialized = false;
    }

    ma_engine_stop(&a->engine);

    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    ma_result r = ma_sound_init_from_file(&a->engine, track->path, flags, NULL, NULL, &a->sound);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "audio_start_playback: failed to load '%s' (%d)\n", track->path, r);
        ma_engine_start(&a->engine);
        return;
    }

    ma_sound_set_looping(&a->sound, MA_FALSE);
    a->sound_initialized = true;
    a->current_track     = track;
    a->playing           = true;

    ma_engine_start(&a->engine);
    ma_sound_start(&a->sound);
}
bool audio_queue_is_empty(const Audio* a) {
    return a->queue.tracks.count == 0;
}

void audio_advance_queue(Audio* a) {
    Track* next = queue_advance(&a->queue);
    if (next == NULL) {
        audio_stop_playback(a);
    } else {
        audio_start_playback(a, next);
    }
}

float audio_get_current_track_progress(const Audio* a) {
    float len = _sound_length_seconds((Audio*)a);
    if (len <= 0.0f) return 0.0f;
    return _sound_played_seconds((Audio*)a) / len;
}

void audio_update(Audio* a) {
    if (!a->sound_initialized) return;
    if (a->playing && ma_sound_at_end(&a->sound)) {
        audio_advance_queue(a);
    }
}

void audio_toggle_playing_state(Audio* a) {
    if (!a->sound_initialized) return;
    if (a->playing) {
        ma_sound_stop(&a->sound);
    } else {
        ma_sound_start(&a->sound);
    }
    a->playing = !a->playing;
}

void audio_enqueue_single(Audio* a, Track* track) {
    queue_push(&a->queue, track);
}

void audio_try_seeking_by(Audio* a, float diff) {
    if (!a->sound_initialized || diff == 0.0f) return;

    float played = _sound_played_seconds(a);
    float len    = _sound_length_seconds(a);
    float target = played + diff;
    if (target < 0.0f) target = 0.0f;
    if (target > len)  target = len;

    ma_uint32 rate = ma_engine_get_sample_rate(&a->engine);
    ma_uint64 frame = (ma_uint64)(target * (float)rate);
    ma_sound_seek_to_pcm_frame(&a->sound, frame);
}

void audio_skip_track_forward(Audio* a) {
    Track* next = queue_advance(&a->queue);
    if (next == NULL) {
        audio_stop_playback(a);
    } else {
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
