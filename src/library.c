#include "library.h"

#include "../extern/dr_mp3.h"
#include <FLAC/metadata.h>
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  Library *lib;
  atomic_size_t index;
} WorkerCtx;

static void collect_dir(const char *dir_name, Paths *paths);

static bool filter(char *c);
static void filter_paths(Paths *paths, bool (*pred)(char *));

static Track process_track(const char *path);
static void *worker(void *arg);

static int compare_track_by_number(const void *a, const void *b) {
  const Track *const *t1 = a;
  const Track *const *t2 = b;
  return (*t1)->number - (*t2)->number;
}

void mp3_processor(void *user_data, const drmp3_metadata *meta) {
  Track *track = user_data;
  if (meta->type != DRMP3_METADATA_TYPE_ID3V2) {
    printf("WARN: Skipping not id3v2 metadata: (%s)\n", track->path);
    return;
  }
  const unsigned char *data = meta->pRawData;
  size_t size = meta->rawDataSize;

  if (size < 10 || memcmp(data, "ID3", 3) != 0) {
    return;
  }

  // skip header
  size_t pos = 10;
  while (pos + 10 <= size) {
    const char *frame_id = (const char *)(data + pos);

    uint32_t frame_size = (data[pos + 4] << 24) | (data[pos + 5] << 16) |
                          (data[pos + 6] << 8) | (data[pos + 7]);

    if (frame_size == 0)
      break;

    const unsigned char *frame_data = data + pos + 10;

    // Text frames
    if (strncmp(frame_id, "TPE1", 4) == 0) {
      track->artist = strndup((char *)frame_data + 1, frame_size - 1);
    } else if (strncmp(frame_id, "TIT2", 4) == 0) {
      track->title = strndup((char *)frame_data + 1, frame_size - 1);
    } else if (strncmp(frame_id, "TALB", 4) == 0) {
      track->album = strndup((char *)frame_data + 1, frame_size - 1);
    } else if (strncmp(frame_id, "TRCK", 4) == 0) {
      track->number = atoi((char *)frame_data + 1);
    } else if (strncmp(frame_id, "APIC", 4) == 0) {
      const unsigned char *p = frame_data;

      uint8_t encoding = p[0];
      p += 1;

      const char *mime = (const char *)p;
      size_t mime_len = strlen(mime);
      p += mime_len + 1;

      uint8_t pic_type = *p;
      p += 1;

      if (encoding == 0 || encoding == 3) {
        size_t desc_len = strlen((char *)p);
        p += desc_len + 1;
      } else if (encoding == 1) {
        while (!(p[0] == 0 && p[1] == 0))
          p++;
        p += 2;
      }

      size_t image_size = (data + pos + 10 + frame_size) - p;

      if (pic_type == 3) {
        int w, h, chan;
        uint8_t *img = stbi_load_from_memory(p, image_size, &w, &h, &chan, 3);

        if (img) {
          track->cover = img;
          track->cover_w = w;
          track->cover_h = h;
        } else {
          printf("MP3 cover decode failed: %s\n", stbi_failure_reason());
        }
      }
    }

    pos += 10 + frame_size;
  }
}

Library prepare_library(const char *root_path) {
  Library lib = {0};
  collect_dir(root_path, &lib.ps);
  filter_paths(&lib.ps, filter);

  lib.tracks.count = lib.ps.count;
  lib.tracks.items = malloc(sizeof(Track) * lib.ps.count);

  size_t thread_count = 6;
  pthread_t threads[thread_count];

  WorkerCtx ctx = {.lib = &lib, .index = 0};

  for (size_t i = 0; i < thread_count; i++) {
    pthread_create(&threads[i], NULL, worker, &ctx);
  }
  for (size_t i = 0; i < thread_count; i++) {
    pthread_join(threads[i], NULL);
  }

  // sort them by album
  for (size_t i = 0; i < lib.tracks.count; i++) {
    bool found = false;
    for (size_t j = 0; j < lib.albums.count; j++) {
      if (strcmp(lib.tracks.items[i].album, lib.albums.items[j].name) == 0) {
        *yar_append(&lib.albums.items[j].tracks) = &lib.tracks.items[i];
        found = true;
      }
    }
    if (!found) {
      *yar_append(&lib.albums) = (Album){
          .name = lib.tracks.items[i].album,
          .artist = lib.tracks.items[i].artist,
      };
    }
  }

  // throw away some not needed album covers that are duplicate
  for (size_t i = 0; i < lib.albums.count; i++) {
    uint8_t *first_cover = lib.albums.items[i].tracks.items[0]->cover;
    for (size_t j = 1; j < lib.albums.items[i].tracks.count; j++) {
      free(lib.albums.items[i].tracks.items[j]->cover);
      lib.albums.items[i].tracks.items[j]->cover = first_cover;
    }
    // arrange each album in order of the tracklist
    qsort(lib.albums.items[i].tracks.items, lib.albums.items[i].tracks.count,
          sizeof(Track *), compare_track_by_number);
  }
  return lib;
}

void unload_library(Library *lib) {
  for (size_t i = 0; i < lib->tracks.count; i++) {
    if (lib->tracks.items[i].album != NULL)
      free(lib->tracks.items[i].album);
    if (lib->tracks.items[i].artist != NULL)
      free(lib->tracks.items[i].artist);
    if (lib->tracks.items[i].title != NULL)
      free(lib->tracks.items[i].title);
  }
  for (size_t i = 0; i < lib->ps.count; i++) {
    free(lib->ps.items[i]);
  }
}

static void *worker(void *arg) {
  WorkerCtx *ctx = arg;
  Library *lib = ctx->lib;

  while (1) {
    size_t i = atomic_fetch_add(&ctx->index, 1);
    if (i >= lib->ps.count)
      break;

    lib->tracks.items[i] = process_track(lib->ps.items[i]);
  }

  return NULL;
}

static Track process_track(const char *path) {
  Track track = {.path = path};

  if (strstr(path, ".flac")) {
    FLAC__StreamMetadata *tags = NULL, *picture = NULL;

    if (FLAC__metadata_get_tags(path, &tags)) {
      for (uint32_t j = 0; j < tags->data.vorbis_comment.num_comments; j++) {
        char *entry = (char *)tags->data.vorbis_comment.comments[j].entry;

        if (strncmp(entry, "ARTIST=", 7) == 0)
          track.artist = strndup(entry + 7, strlen(entry) - 7);
        else if (strncmp(entry, "TITLE=", 6) == 0)
          track.title = strndup(entry + 6, strlen(entry) - 6);
        else if (strncmp(entry, "ALBUM=", 6) == 0)
          track.album = strndup(entry + 6, strlen(entry) - 6);
        else if (strncmp(entry, "TRACKNUMBER=", 12) == 0)
          track.number = atoi(entry + 12);
      }
    }

    if (FLAC__metadata_get_picture(
            path, &picture, FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER,
            NULL, NULL, -1, -1, -1, -1)) {

      int x, y, chan;
      uint8_t *image = stbi_load_from_memory(picture->data.picture.data,
                                             picture->data.picture.data_length,
                                             &x, &y, &chan, 3);

      if (image) {
        track.cover = image;
        track.cover_w = x;
        track.cover_h = y;
      }
    }

    if (tags)
      FLAC__metadata_object_delete(tags);
    if (picture)
      FLAC__metadata_object_delete(picture);

  } else if (strstr(path, ".mp3")) {
    drmp3 mp3 = {0};
    drmp3_init_file_with_metadata(&mp3, path, mp3_processor, &track, NULL);
  }

  return track;
}

static void collect_dir(const char *dir_name, Paths *paths) {
  DIR *dir = opendir(dir_name);
  if (dir == NULL) {
    perror("opendir");
    return;
  }
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    char path[257] = {0};
    snprintf(path, 257, "%s/%s", dir_name, entry->d_name);
    if (*entry->d_name == '.')
      continue;
    if (entry->d_type == 4) {
      collect_dir(path, paths);
    } else {
      char *malloced_path = strdup(path);
      *yar_append(paths) = malloced_path;
    }
  }
  closedir(dir);
}

static bool filter(char *c) {
  return strstr(c, ".flac") != NULL || strstr(c, ".mp3") != NULL;
}
static void filter_paths(Paths *paths, bool (*pred)(char *)) {
  for (size_t i = paths->count; i > 0; i--)
    if (!pred(paths->items[i - 1]))
      yar_remove(paths, i - 1, 1);
}
