#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char* program_name;
    char* custom_root_path;
    char* custom_playlist_dir;  // default ($HOME/.wisp/playlists)
    bool help;
} Config;

bool config_parse_args(int* argc, char*** argv, Config* cc);
bool config_parse_file(const char* path, Config* cc);
void help_and_exit(const Config* cfg);
