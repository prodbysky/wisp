#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"


static bool build_glad();
static bool build_stbs();
static bool build_simp();

static void common_flags(Cmd* c);

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    mkdir_if_not_exists("build");
    if (!build_glad()) return 1;
    if (!build_stbs()) return 1;
    if (!build_simp()) return 1;
    Cmd c = {0};

    cmd_append(&c, "gcc",
        "build/dr_flac.o", "build/dr_mp3.o", "build/miniaudio.o",
        "build/yar.o",
        "src/main.c", "src/library.c", "src/audio.c",
        "-o", "build/wisp",
        "-lm", "-lX11", "-lFLAC", "-lXrandr", "-lGL", "-Lbuild/", "-l:libsimp.a");
    common_flags(&c);
    if (!cmd_run(&c)) return 1;
}

static bool build_simp() {
    Cmd c = {0};
    cmd_append(
        &c, 
        "gcc", 
        "src/simp.c", 
        "-lGL", "-lXrandr", "-lX11", "-lm",
        "-c",
        "-o", "build/simp.o"
    );
    if (!cmd_run(&c)) return false;
    cmd_append(
        &c, 
        "ar", "rcs", "build/libsimp.a",
        "build/glad.o", "build/stb_image.o", "build/yar.o", "build/stb_truetype.o",
        "build/simp.o", 
    );
    if (!cmd_run(&c)) return false;
    cmd_free(c);
    return true;

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

static bool build_stbs() {
    Cmd c = {0};
    if (needs_rebuild1("build/dr_flac.o", "extern/dr_flac.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/dr_flac.h", "-DDR_FLAC_IMPLEMENTATION" , "-c", "-o", "build/dr_flac.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    if (needs_rebuild1("build/dr_mp3.o", "extern/dr_mp3.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/dr_mp3.h", "-DDR_MP3_IMPLEMENTATION" , "-c", "-o", "build/dr_mp3.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    if (needs_rebuild1("build/miniaudio.o", "extern/miniaudio.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/miniaudio.h", "-DMINIAUDIO_IMPLEMENTATION" , "-c", "-o", "build/miniaudio.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    if (needs_rebuild1("build/stb_truetype.o", "extern/stb_truetype.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/stb_truetype.h", "-DSTB_TRUETYPE_IMPLEMENTATION" , "-c", "-o", "build/stb_truetype.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    if (needs_rebuild1("build/stb_image.o", "extern/stb_image.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/stb_image.h", "-DSTB_IMAGE_IMPLEMENTATION" , "-c", "-o", "build/stb_image.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    if (needs_rebuild1("build/yar.o", "extern/yar.h")) {
        cmd_append(&c, "gcc", "-xc", "extern/yar.h", "-DYAR_IMPLEMENTATION" , "-c", "-o", "build/yar.o");
        common_flags(&c);
        if (!cmd_run(&c)) return false;
    }
    cmd_free(c);
    return true;
}
