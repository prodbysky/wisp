#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

static bool build_glad();

static void common_flags(Cmd* c);

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    mkdir_if_not_exists("build");
    if (!build_glad()) return 1;
    Cmd c = {0};
    if (!file_exists("extern/raylib/src/libraylib.a")) {
        cmd_append(&c, "make", "-C", "extern/raylib/src/", "-j");
        if (!cmd_run(&c)) return 1;
    }

    cmd_append(&c, "gcc", "src/main.c", "src/library.c", "src/audio.c", "-o", "build/wisp", "-lm", "-Lextern/raylib/src/", "-l:libraylib.a", "-lX11", "-lFLAC");
    common_flags(&c);
    if (!cmd_run(&c)) return 1;

    cmd_append(&c, "gcc", "src/simp.c", "src/rgfw.c", "build/glad.o", "-o", "build/rgfw", "-lGL", "-lm", "-lX11", "-lXrandr");
    common_flags(&c);
    if (!cmd_run(&c)) return 1;
}

static bool build_glad() {
    Cmd c = {0};
    if (needs_rebuild1("build/glad.o", "extern/glad/src/glad.c")) {
        cmd_append(&c, "gcc",  "extern/glad/src/glad.c", "-c", "-o", "build/glad.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    cmd_free(c);
    return true;
}


static void common_flags(Cmd* c) {
    cmd_append(c, "-ggdb", "-std=c23", "-Wall", "-Wextra");
}
