#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../nob.h"


int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    const char* cc = getenv("CC");
    if(!cc) {
        setenv("CC", 
            cc=
#if _WIN32
            "clang"
#else
            "cc"
#endif
        , 0);
    }
    const char* bindir = getenv("BINDIR");
    if(!bindir) bindir = ".build";
    if(!mkdir_if_not_exists(bindir)) return 1;
    Cmd cmd = { 0 };
    const char* obj = temp_sprintf("%s/vendor.o", bindir);
    const char* src = "vendor.c";
    File_Paths pathb = { 0 };
    String_Builder stb = { 0 };
    if(nob_c_needs_rebuild1(&stb, &pathb, obj, src)) {
        cmd_append(&cmd, cc, "-O2", "-MMD", "-c", src, "-o", obj);
        return cmd_run_sync_and_reset(&cmd) ? 0 : 1;
    }
    return 0;
}
