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
    fprintf(sink, "%s (client|server|keygen|db-utils (run)) \n", exe);
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
        EXAMPLE_SERVER,
        EXAMPLE_KEYGEN,
        EXAMPLE_DB_UTILS,
    } example = EXAMPLE_ALL;
    bool run = false;
    bool gdb = false;
    bool capturing_args = false;
    bool slop_loop = false;

    File_Paths run_args = { 0 };
    while(argc) {
        const char* arg = shift_args(&argc, &argv);
        if(capturing_args) {
            da_append(&run_args, arg);
            continue;
        }
        if(strcmp(arg, "--") == 0) capturing_args = true;
        else if(strcmp(arg, "client") == 0) example = EXAMPLE_CLIENT;
        else if(strcmp(arg, "server") == 0) example = EXAMPLE_SERVER;
        else if(strcmp(arg, "keygen") == 0) example = EXAMPLE_KEYGEN;
        else if(strcmp(arg, "db-utils") == 0) example = EXAMPLE_DB_UTILS;
        else if(strcmp(arg, "run") == 0) run = true;
        else if(strcmp(arg, "-gdb") == 0) gdb = true;
        else if(strcmp(arg, "-slop-loop") == 0) slop_loop = true;
        else {
            nob_log(NOB_ERROR, "Unexpected argument: `%s`", arg);
            help(stderr, exe);
            return 1;
        }
    }

    if(!go_run_nob_inside(&cmd, "vendor")) return 1;
    // I know there's more elegant ways to do it but frick you
    switch(example) {
    case EXAMPLE_ALL:
        if(!go_run_nob_inside(&cmd, "client")) return 1;
        if(!go_run_nob_inside(&cmd, "server")) return 1;
        if(!go_run_nob_inside(&cmd, "db-utils")) return 1;
        if(!go_run_nob_inside(&cmd, "keygen")) return 1;
        break;
    case EXAMPLE_SERVER:
        if(!go_run_nob_inside(&cmd, "server")) return 1;
        break;
    case EXAMPLE_DB_UTILS:
        if(!go_run_nob_inside(&cmd, "db-utils")) return 1;
        break;
    case EXAMPLE_CLIENT:
        if(!go_run_nob_inside(&cmd, "client")) return 1;
        break;
    case EXAMPLE_KEYGEN:
        if(!go_run_nob_inside(&cmd, "keygen")) return 1;
        break;
    }

    if(run && example != EXAMPLE_ALL) {
        bool last_exit;
        const char* program = NULL;
        switch(example) {
        case EXAMPLE_CLIENT:
            program = "./.build/client/client"EXE_SUFFIX;
            break;
        case EXAMPLE_SERVER:
            program = "./.build/server/server"EXE_SUFFIX;
            break;
        case EXAMPLE_DB_UTILS:
            program = "./.build/db-utils/db-utils"EXE_SUFFIX;
            break;
        case EXAMPLE_KEYGEN:
            program = "./.build/keygen/keygen"EXE_SUFFIX;
            break;
        }
        do {
            if(gdb) {
                cmd_append(&cmd, "gdb", program, "--args");
            }
            cmd_append(&cmd, program);
            da_append_many(&cmd, run_args.items, run_args.count);
            last_exit = cmd_run_sync_and_reset(&cmd);
            // TODO: log timestamp of crash and save report
            if(slop_loop) nob_log(NOB_ERROR, "%s crashed!... Ah shit. Here we go again", program);
        } while(slop_loop);
        return last_exit ? 0 : 1;
    }
    return 0;
}

