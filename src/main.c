// chiron: run SMAC/SMACX natively from the player's own installation.
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "install/install.h"
#include "loader/modules.h"
#include "loader/teb.h"
#include "loader/thunks.h"
#include "win32/gdi.h"
#include "win32/kernel32.h"
#include "win32/win32.h"

#define GAME_STACK_SIZE (16u << 20)

static struct pe_image *exe;

static void usage(void) {
    fprintf(stderr,
            "usage: chiron [options] [-- game arguments]\n"
            "  --game DIR           game folder (default: $CHIRON_GAME_DIR or ~/.local/share/chiron/game)\n"
            "  --exe NAME           executable inside the game folder (default: terranx.exe)\n"
            "  --screen WxH         screen size reported to the game (default: 1024x768)\n"
            "  --trace              log every Win32 call the game makes\n"
            "  --allow-unknown-exe  run an executable whose checksum is not recognised\n");
}

static void on_fault(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx;
    char where[128];
    modules_describe_addr(uc->uc_mcontext.gregs[REG_EIP], where, sizeof(where));
    fprintf(stderr, "chiron: %s at %s (fault address %p)\n", strsignal(sig), where, si->si_addr);
    _exit(128 + sig);
}

static void on_terminate(int sig) {
    // Debugging aid: report what the game was calling when it was stopped.
    thunk_trace_summary();
    _exit(128 + sig);
}

static void *game_thread(void *arg) {
    teb_attach_current_thread(0);
    user32_set_sdl_thread();
    char err[512];
    exe = modules_load_exe(arg, err, sizeof(err));
    if (!exe) {
        fprintf(stderr, "chiron: %s\n", err);
        exit(1);
    }
    teb_set_image_base((uint32_t)exe->base);
    // The MSVC entry point (WinMainCRTStartup) never returns; it calls ExitProcess.
    ((void(WINAPI *)(void))(exe->base + exe->entry_rva))();
    return NULL;
}

int main(int argc, char **argv) {
    char game_dir[4096];
    const char *exe_name = "terranx.exe";
    int allow_unknown = 0;
    install_default_dir(game_dir, sizeof(game_dir));

    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) snprintf(game_dir, sizeof(game_dir), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe_name = argv[++i];
        else if (!strcmp(argv[i], "--trace")) modules_trace = 1;
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc &&
                 sscanf(argv[++i], "%dx%d", &display_width, &display_height) == 2) {}
        else if (!strcmp(argv[i], "--allow-unknown-exe")) allow_unknown = 1;
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else { usage(); return 2; }
    }

    path_init(game_dir);
    char exe_path[4096], hash[65];
    if (path_to_host(exe_name, exe_path, sizeof(exe_path)) || access(exe_path, R_OK)) {
        fprintf(stderr, "chiron: %s not found in %s\n"
                        "Point --game at the folder of your installed copy of the game.\n",
                exe_name, game_dir);
        return 1;
    }
    const struct known_build *build = install_identify(exe_path, hash);
    if (build) {
        fprintf(stderr, "chiron: found %s\n", build->description);
    } else if (allow_unknown) {
        fprintf(stderr, "chiron: warning: unrecognised %s (sha256 %s)\n", exe_name, hash);
    } else {
        fprintf(stderr, "chiron: %s (sha256 %s) is not a supported version of the game.\n"
                        "Chiron needs an original, purchased copy; see README.md.\n",
                exe_name, hash);
        return 1;
    }

    char cmdline[1024];
    size_t n = snprintf(cmdline, sizeof(cmdline), "%s", exe_name);
    for (; i < argc && n < sizeof(cmdline); i++) n += snprintf(cmdline + n, sizeof(cmdline) - n, " %s", argv[i]);
    kernel32_set_command_line(cmdline);

    struct sigaction sa = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    if (modules_trace) {
        atexit(thunk_trace_summary);
        signal(SIGTERM, on_terminate);
        signal(SIGINT, on_terminate);
    }

    // The game runs on its own thread so it gets a Windows-sized stack whose
    // bounds we know for the TEB.
    pthread_attr_t attr;
    pthread_t t;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GAME_STACK_SIZE);
    pthread_create(&t, &attr, game_thread, (void *)exe_name);
    pthread_join(t, NULL);
    return 0;
}
