#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "nob.h"

#ifdef _WIN32
# define EXE_SUFFIX ".exe"
#else
# define EXE_SUFFIX ""
#endif

static bool go_run_nob_inside(Nob_Cmd* cmd, const char* dir) {
    size_t temp = nob_temp_save();
    const char* cur = nob_get_current_dir_temp();
    assert(cur);
    cur = nob_temp_realpath(cur);
    if(!nob_set_current_dir(dir)) return false;
    if(nob_file_exists("nob"EXE_SUFFIX) != 1) {
        nob_cmd_append(cmd, NOB_REBUILD_URSELF("nob"EXE_SUFFIX, "nob.c"));
        if(!nob_cmd_run_sync_and_reset(cmd)) return false;
    }
    nob_cmd_append(cmd, "./nob"EXE_SUFFIX);
    bool res = nob_cmd_run_sync_and_reset(cmd);
    assert(nob_set_current_dir(cur));
    nob_temp_rewind(temp);
    return res;
}

void help(FILE* sink, const char* exe) {
    fprintf(sink, "%s (example-client|example-server (run)) \n", exe);
}
int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = { 0 };
    const char* exe = shift_args(&argc, &argv);
    setenv("CC", 
#if _WIN32
        "clang"
#else
        "cc"
#endif
    , 0);

    const char* bindir = getenv("BINDIR");
    if(!bindir) bindir = ".build";
    if(!mkdir_if_not_exists(bindir)) return 1;
    setenv("BINDIR", nob_temp_realpath(bindir), 0);
    enum {
        EXAMPLE_ALL,
        EXAMPLE_CLIENT,
        EXAMPLE_SERVER
    } example = EXAMPLE_ALL;
    bool run = false;
    while(argc) {
        const char* arg = shift_args(&argc, &argv);
        if(strcmp(arg, "example-client") == 0) example = EXAMPLE_CLIENT;
        else if(strcmp(arg, "example-server") == 0) example = EXAMPLE_SERVER;
        else if(strcmp(arg, "run") == 0) run = true;
        else {
            nob_log(NOB_ERROR, "Unexpected argument: `%s`", arg);
            help(stderr, exe);
            return 1;
        }
    }
    // I know there's more elegant ways to do it but frick you
    switch(example) {
    case EXAMPLE_ALL:
        if(!go_run_nob_inside(&cmd, "example-client")) return 1;
        if(!go_run_nob_inside(&cmd, "example-server")) return 1;
        break;
    case EXAMPLE_SERVER:
        if(!go_run_nob_inside(&cmd, "example-server")) return 1;
        break;
    case EXAMPLE_CLIENT:
        if(!go_run_nob_inside(&cmd, "example-client")) return 1;
        break;
    }

    if(run && example != EXAMPLE_ALL) {
        switch(example) {
        case EXAMPLE_CLIENT:
            cmd_append(&cmd, "./.build/example-client/example-client"EXE_SUFFIX);
            break;
        case EXAMPLE_SERVER:
            cmd_append(&cmd, "./.build/example-server/example-server"EXE_SUFFIX);
            break;
        }
        return cmd_run_sync_and_reset(&cmd) ? 0 : 1;
    }
    return 0;
}
