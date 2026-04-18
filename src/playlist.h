#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "../extern/yar.h"
#include "library.h"

typedef struct {
    char* name;
    yar(Track*) tracks;
    bool dirty;
} Playlist;

typedef yar(Playlist) Playlists;

char* playlist_dir_path(const char* override_dir);

// does the playlist directory exist
bool playlist_ensure_dir(const char* dir);

// tracks are resolved against lib, unknown paths are silently skipped.
void playlists_load(const char* dir, Library* lib, Playlists* out);

// save playlists that are marked dirty back to dir.
void playlists_save(const char* dir, Playlists* pl);

void playlist_save_one(const char* dir, const Playlist* pl);

// new empty playlist with the given name (duped).
Playlist* playlists_create(Playlists* pl, const char* name);

// no-op if already present
void playlist_add_track(Playlist* pl, Track* track);

// dree all memory owned by a single playlist (does NOT free the struct itself).
void playlist_free(Playlist* pl);

void playlists_free(Playlists* pl);
