#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../extern/raylib/src/raylib.h"
#include "../extern/stb_image.h"
#include "../extern/yar.h"

typedef struct {
    char** items;
    size_t count;
    size_t capacity;
} Paths;

typedef struct {
    char *title, *album, *artist;
    uint8_t* cover;
    int cover_w, cover_h;
    char* path;
    int number;
} Track;

typedef struct {
    char* artist;
    char* name;
    yar(Track*) tracks;
} Album;

typedef struct {
    char* name;
    yar(Album*) albums;
} Artist;

typedef yar(Track) Tracks;
typedef yar(Album) Albums;
typedef yar(Artist) Artists;

typedef struct {
    Tracks tracks;
    Albums albums;
    Artists artists;
    Paths ps;
} Library;

Library prepare_library(const char* root_path);
void unload_library(Library* lib);
