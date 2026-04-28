#include "runtime_config.h"

bool config_parse_args(int* argc, char*** argv, Config* cc) {
    cc->program_name = **argv;
    if (*argc == 1) return true;
    (*argc)--;
    (*argv)++;
    while (*argc != 0) {
        if (strcmp(**argv, "--help") == 0) {
            cc->help = true;
            return true;
        } else if (strcmp(**argv, "--path") == 0) {
            if (cc->custom_root_path) {
                printf("ERROR: --path given twice\n");
                return false;
            }
            if (*argc < 2) {
                printf("ERROR: --path requires an argument\n");
                return false;
            }
            cc->custom_root_path = *((*argv) + 1);
            *argv += 2;
            (*argc) -= 2;
        } else if (strcmp(**argv, "--playlist-dir") == 0) {
            if (cc->custom_playlist_dir) {
                printf("ERROR: --playlist-dir given twice\n");
                return false;
            }
            if (*argc < 2) {
                printf("ERROR: --playlist-dir requires an argument\n");
                return false;
            }
            cc->custom_playlist_dir = *((*argv) + 1);
            *argv += 2;
            (*argc) -= 2;
        } else {
            printf("ERROR: Unknown flag `%s`\n", **argv);
            return false;
        }
    }
    return true;
}

bool config_parse_file(const char* path, Config* cc) {
    FILE* file = fopen(path, "r");
    if (!file) return false;

    static const struct {
        const char* key;
        size_t klen;
        ptrdiff_t off;
    } pairs[] = {
        {"library_path", 12, offsetof(Config, custom_root_path)},
        {"playlist_dir", 12, offsetof(Config, custom_playlist_dir)},
    };

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        bool matched = false;
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
            size_t klen = strlen(pairs[i].key);
            if (strncmp(line, pairs[i].key, klen) == 0) {
                char* val = line + klen;
                while (*val == ' ' || *val == '\t') val++;
                char* end = val + strlen(val) - 1;
                while (end > val && (*end == ' ' || *end == '\t')) *end-- = '\0';
                char** dst = (char**)((char*)cc + pairs[i].off);
                if (*dst == NULL) *dst = strdup(val);
                matched = true;
                break;
            }
        }
        if (!matched) { printf("WARN: Unknown config key: %s\n", line); }
    }
    fclose(file);
    return true;
}

void help_and_exit(const Config* cfg) {
    printf("USAGE:\n");
    printf("  %s [FLAGS]\n", cfg->program_name ? cfg->program_name : "wisp");
    printf("  --help                : show this help message\n");
    printf("  --path <DIR>          : set custom music library path\n");
    printf("  --playlist-dir <DIR>  : set custom playlist directory\n");
}
