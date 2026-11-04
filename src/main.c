// chiron: run SMAC/SMACX natively from the player's own installation.
#include <SDL.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "engine/hooks.h"
#include "engine/patches.h"
#include "install/install.h"
#include "loader/modules.h"
#include "loader/teb.h"
#include "loader/thunks.h"
#include "win32/gdi.h"
#include "win32/kernel32.h"
#include "win32/win32.h"

#define GAME_STACK_SIZE (16u << 20)

static struct pe_image *exe;
static int known_build;
static enum hook_mode reimpl_mode;

static void usage(void) {
    fprintf(stderr,
            "usage: chiron [options] [-- game arguments]\n"
            "       chiron --setup PATH\n"
            "  --setup PATH         remember where the game is: its folder, a folder above it\n"
            "                       (such as a Wine prefix), or a GOG offline installer to unpack\n"
            "  --game DIR           game folder for this run (default: the one set up, or found)\n"
            "  --user DIR           folder for saves and settings (default: ~/.local/share/chiron/user)\n"
            "  --exe NAME           executable inside the game folder (default: terranx.exe)\n"
            "  --screen WxH         screen size reported to the game (default: 1024x768)\n"
            "  --volume N           master volume, 0-100 (default: 100)\n"
            "  --original           run only the game's own code, none of Chiron's reimplementations\n"
            "  --trace              log every Win32 call the game makes\n"
            "  --allow-unknown-exe  run an executable whose checksum is not recognised\n");
}

int seh_handle_signal(int sig, siginfo_t *si, void *uctx);

static void on_fault(int sig, siginfo_t *si, void *ctx) {
    // Faults on game threads go to the game's own exception handlers.
    if (seh_handle_signal(sig, si, ctx)) return;
    ucontext_t *uc = ctx;
    char where[128];
    modules_describe_addr(uc->uc_mcontext.gregs[REG_EIP], where, sizeof(where));
    fprintf(stderr, "chiron: %s at %s (fault address %p)\n", strsignal(sig), where, si->si_addr);
    thunk_trace_recent();
    modules_scan_stack(uc->uc_mcontext.gregs[REG_ESP]);
    _exit(128 + sig);
}

static void on_terminate(int sig) {
    // Debugging aid: report what the game was calling when it was stopped.
    thunk_trace_summary();
    thunk_trace_recent();
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
    if (known_build) {
        patches_apply(exe);
        hooks_hold_time = kernel32_hold_ticks;
        hooks_apply(exe, reimpl_mode);
    }
    // The MSVC entry point (WinMainCRTStartup) never returns; it calls ExitProcess.
    ((void(WINAPI *)(void))(exe->base + exe->entry_rva))();
    return NULL;
}

// Tell the player, also when Chiron was started from a desktop launcher and
// nobody sees its output.
static void report_problem(const char *text) {
    fprintf(stderr, "chiron: %s\n", text);
    if (!isatty(2)) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chiron", text, NULL);
}

// The game folder: --game, $CHIRON_GAME_DIR, the one set up, Chiron's own
// folder, or one found where GOG installs usually are (then remembered).
static int choose_game_dir(char *out, size_t outlen) {
    const char *env = getenv("CHIRON_GAME_DIR");
    if (env && *env) {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    if (install_config_get("game", out, outlen) && *out) return 0;
    char data[3072];
    const struct known_build *build;
    char hash[65];
    install_data_dir(data, sizeof(data));
    snprintf(out, outlen, "%s/game", data);
    if (install_check_dir(out, &build, hash)) return 0;
    if (install_autodetect(out, outlen)) {
        fprintf(stderr, "chiron: found the game in %s\n", out);
        if (install_config_set("game", out) == 0) fprintf(stderr, "chiron: remembered it for next time\n");
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    char game_dir[4096] = "", user_dir[4096] = "";
    const char *exe_name = "terranx.exe", *setup = NULL;
    int allow_unknown = 0;
    reimpl_mode = hooks_mode_from_env();

    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) snprintf(game_dir, sizeof(game_dir), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--user") && i + 1 < argc) snprintf(user_dir, sizeof(user_dir), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--setup") && i + 1 < argc) setup = argv[++i];
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe_name = argv[++i];
        else if (!strcmp(argv[i], "--trace")) modules_trace = 1;
        else if (!strcmp(argv[i], "--original")) reimpl_mode = HOOKS_OFF;
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc &&
                 sscanf(argv[++i], "%dx%d", &display_width, &display_height) == 2) {}
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) audio_volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--allow-unknown-exe")) allow_unknown = 1;
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else { usage(); return 2; }
    }

    if (setup) {
        if (install_setup(setup, game_dir, sizeof(game_dir))) return 1;
        fprintf(stderr, "chiron: set up the game in %s\nRun chiron to play.\n", game_dir);
        return 0;
    }
    if (!*game_dir && choose_game_dir(game_dir, sizeof(game_dir))) {
        report_problem("Chiron could not find Alpha Centauri.\n\n"
                       "Run \"chiron --setup PATH\" with the folder of your installed copy (GOG), "
                       "or with the GOG offline installer.");
        return 1;
    }
    if (!*user_dir) {
        const char *env = getenv("CHIRON_USER_DIR");
        if (env && *env) {
            snprintf(user_dir, sizeof(user_dir), "%s", env);
        } else {
            char data[3072];
            install_data_dir(data, sizeof(data));
            snprintf(user_dir, sizeof(user_dir), "%s/user", data);
        }
    }

    if (audio_volume < 0) audio_volume = 0;
    if (audio_volume > 100) audio_volume = 100;
    path_init(game_dir, user_dir);
    char exe_path[4096], hash[65];
    if (path_to_host(exe_name, exe_path, sizeof(exe_path)) || access(exe_path, R_OK)) {
        char msg[8400];
        snprintf(msg, sizeof(msg), "%s not found in %s.\n\nRun \"chiron --setup PATH\" with the folder of "
                 "your installed copy of the game.", exe_name, game_dir);
        report_problem(msg);
        return 1;
    }
    const struct known_build *build = install_identify(exe_path, hash);
    if (build) {
        known_build = 1;
        fprintf(stderr, "chiron: found %s\n", build->description);
    } else if (allow_unknown) {
        fprintf(stderr, "chiron: warning: unrecognised %s (sha256 %s)\n", exe_name, hash);
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s (sha256 %s) is not a supported version of the game.\n"
                 "Chiron needs an original, purchased copy; see README.md.", exe_name, hash);
        report_problem(msg);
        return 1;
    }

    char cmdline[1024];
    size_t n = snprintf(cmdline, sizeof(cmdline), "%s", exe_name);
    for (; i < argc && n < sizeof(cmdline); i++) n += snprintf(cmdline + n, sizeof(cmdline) - n, " %s", argv[i]);
    kernel32_set_command_line(cmdline);

    // SA_NODEFER: a handler that resumes from one fault may hit another.
    // SA_ONSTACK: game threads handle faults on their own signal stack (see
    // teb_attach_current_thread), leaving the space below esp to seh.c.
    struct sigaction sa = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK};
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
