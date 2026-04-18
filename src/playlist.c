#include "playlist.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void chomp(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static char* stem(const char* filename) {
    const char* dot = strrchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    char* out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, filename, len);
    out[len] = '\0';
    return out;
}

static Track* resolve_track(Library* lib, const char* needle) {
    for (size_t i = 0; i < lib->tracks.count; i++) {
        if (strcmp(lib->tracks.items[i].path, needle) == 0) return &lib->tracks.items[i];
    }
    return NULL;
}

char* playlist_dir_path(const char* override_dir) {
    if (override_dir) return strdup(override_dir);
    const char* home = getenv("HOME");
    if (!home) home = ".";
    size_t len = strlen(home) + sizeof("/.wisp/playlists") + 1;
    char* out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s/.wisp/playlists", home);
    return out;
}

bool playlist_ensure_dir(const char* dir) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return true;
}

void playlists_load(const char* dir, Library* lib, Playlists* out) {
    DIR* d = opendir(dir);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        const char* ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".m3u") != 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        FILE* f = fopen(path, "r");
        if (!f) continue;

        Playlist pl = {0};
        pl.name = stem(ent->d_name);
        pl.dirty = false;

        char line[2048];
        bool in_header = false;

        while (fgets(line, sizeof(line), f)) {
            chomp(line);
            if (line[0] == '\0') continue;

            if (strcmp(line, "#EXTM3U") == 0) {
                in_header = true;
                continue;
            }

            if (line[0] == '#') continue;

            Track* t = resolve_track(lib, line);
            if (t) *yar_append(&pl.tracks) = t;
        }

        fclose(f);
        (void)in_header;
        *yar_append(out) = pl;
    }
    closedir(d);
}

void playlist_save_one(const char* dir, const Playlist* pl) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.m3u", dir, pl->name);

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "playlist: cannot write %s\n", path);
        return;
    }

    fprintf(f, "#EXTM3U\n");
    for (size_t i = 0; i < pl->tracks.count; i++) {
        const Track* t = pl->tracks.items[i];
        const char* title = t->title ? t->title : t->path;
        const char* artist = t->artist ? t->artist : "Unknown";
        fprintf(f, "#EXTINF:-1,%s - %s\n", artist, title);
        fprintf(f, "%s\n", t->path);
    }

    fclose(f);
}

void playlists_save(const char* dir, Playlists* pl) {
    for (size_t i = 0; i < pl->count; i++) {
        if (pl->items[i].dirty) playlist_save_one(dir, &pl->items[i]);
    }
}

Playlist* playlists_create(Playlists* pl, const char* name) {
    Playlist p = {0};
    p.name = strdup(name);
    p.dirty = true;
    *yar_append(pl) = p;
    return &pl->items[pl->count - 1];
}

void playlist_add_track(Playlist* pl, Track* track) {
    for (size_t i = 0; i < pl->tracks.count; i++) {
        if (pl->tracks.items[i] == track) return;
    }
    *yar_append(&pl->tracks) = track;
    pl->dirty = true;
}

void playlist_free(Playlist* pl) {
    free(pl->name);
    free(pl->tracks.items);
    pl->name = NULL;
    pl->tracks.items = NULL;
    pl->tracks.count = 0;
    pl->tracks.capacity = 0;
}

void playlists_free(Playlists* pl) {
    for (size_t i = 0; i < pl->count; i++) playlist_free(&pl->items[i]);
    free(pl->items);
    pl->items = NULL;
    pl->count = 0;
    pl->capacity = 0;
}
