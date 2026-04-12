#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../extern/yar.h"


#include "../extern/yar.h"
#include "../extern/stb_image.h"


typedef struct {
    char** items;
    size_t count;
    size_t capacity;
} Paths;


typedef struct {
    char* title, *album, *artist;
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

typedef yar(Track) Tracks;
typedef yar(Album) Albums;

typedef struct {
    Tracks tracks;
    Albums albums;
    Paths ps;
} Library;

Library prepare_library(const char* root_path);
void unload_library(Library* lib);
