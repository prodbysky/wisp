#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

static bool build_deps();
static void common_flags(Cmd* c);

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    mkdir_if_not_exists("build");
    if (!build_deps()) return 1;
    Cmd c = {0};
    if (!file_exists("extern/raylib/src/libraylib.a")) {
        cmd_append(&c, "make", "-C", "extern/raylib/src/", "-j");
        if (!cmd_run(&c)) return 1;
    }

    cmd_append(&c,
               "gcc",
               "build/yar.o",
               "src/main.c",
               "src/library.c",
               "src/audio.c",
               "src/playlist.c",
               "src/draw_utils.c",
               "src/fft.c",
               "-o", "build/wisp",
               "-lm",
               "-Lextern/raylib/src/",
               "-l:libraylib.a",
               "-lX11",
               "-lFLAC");
    common_flags(&c);
    if (!cmd_run(&c)) return 1;
    return 0;
}

static bool build_deps() {
    Cmd c = {0};
    if (needs_rebuild1("build/yar.o", "extern/yar.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/yar.h", "-c", "-DYAR_IMPLEMENTATION", "-o", "build/yar.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    cmd_free(c);
    return true;
}

static void common_flags(Cmd* c) {
    cmd_append(c, "-ggdb", "-std=c23", "-Wall", "-Wextra", "-O3");
}
