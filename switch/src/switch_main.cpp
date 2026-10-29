// SA-nx on the Nintendo Switch: Horizon around dream-recomp's launcher.
//
//   sdmc:/switch/sonicadv/sonicadv.nro        this program
//   sdmc:/switch/sonicadv/sonicadv.toml       the game's config, with SD paths
//   sdmc:/switch/sonicadv/1ST_READ.BIN        the game's main program, from the disc
//   sdmc:/switch/sonicadv/disc/               the GDI and its tracks
//   sdmc:/switch/sonicadv/save/vmu_A1.bin     the memory card (created on first run)
//   sdmc:/switch/sonicadv/sonicadv.log        stdout, sonicadv.err.log stderr (fresh each launch)
//   sdmc:/switch/sonicadv/sonicadv_args.txt   optional extra launcher arguments, one per line
//   sdmc:/switch/sonicadv/crash_XXXXXXXX.txt  written by a crash
//
// The clock and crash-report approach follows CoD-nx's src/switch_nx.c (MIT, burnout3-sw's base).
#include <switch.h>

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

int dream_boot_main(int argc, char** argv);
extern void (*dream_port_set_cpu_mhz)(int mhz);
extern "C" char __start__;  // libnx's crt0: the start of this module, for crash offsets

namespace {

constexpr const char* kDir = "sdmc:/switch/sonicadv";

// --- logging --------------------------------------------------------------------------------
// One file per stream (Horizon refuses a second write handle on an open file), fully buffered,
// flushed by a thread every two seconds so the game thread never waits on the SD card.
Thread g_flusher;
volatile bool g_flusher_stop = false;

void flusher(void*) {
    while (!g_flusher_stop) {
        svcSleepThread(2'000'000'000ll);
        std::fflush(stdout);
        std::fflush(stderr);
    }
}

void open_logs() {
    mkdir(kDir, 0777);
    std::freopen("sdmc:/switch/sonicadv/sonicadv.log", "w", stdout);
    std::freopen("sdmc:/switch/sonicadv/sonicadv.err.log", "w", stderr);
    static char out_buf[64 * 1024], err_buf[16 * 1024];
    std::setvbuf(stdout, out_buf, _IOFBF, sizeof out_buf);
    std::setvbuf(stderr, err_buf, _IOFBF, sizeof err_buf);
    if (R_SUCCEEDED(threadCreate(&g_flusher, flusher, nullptr, nullptr, 0x4000, 0x30, 0)))
        threadStart(&g_flusher);
}

// --- crash reports --------------------------------------------------------------------------
// Written through the file-system service directly: no malloc and no stdio, so a crash inside
// either still gets a report. Offsets are from the module start; Claude resolves them with
// aarch64-none-elf-addr2line against the ELF of the same build.
void crash_write(const char* text) {
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs)
        return;
    char path[96];
    std::snprintf(path, sizeof path, "/switch/sonicadv/crash_%08X.txt",
                  static_cast<unsigned>(armGetSystemTick() & 0xFFFFFFFFu));
    const std::size_t len = std::strlen(text);
    fsFsCreateFile(fs, path, len, 0);
    FsFile f;
    if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Write, &f)))
        return;
    fsFileWrite(&f, 0, text, len, FsWriteOption_Flush);
    fsFileClose(&f);
}

std::uint64_t off(std::uint64_t a) {
    return a - reinterpret_cast<std::uint64_t>(&__start__);
}

[[noreturn]] void on_terminate() {
    char msg[512] = "uncaught exception";
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            std::snprintf(msg, sizeof msg, "uncaught exception: %s", ex.what());
        } catch (...) {
            std::snprintf(msg, sizeof msg, "uncaught exception of an unknown type");
        }
    }
    std::fprintf(stderr, "[fatal] %s\n", msg);
    std::fflush(stdout);
    std::fflush(stderr);
    char text[640];
    std::snprintf(text, sizeof text, "SA-nx terminate\n%s\n", msg);
    crash_write(text);
    svcExitProcess();
    __builtin_unreachable();
}

// --- clocks ---------------------------------------------------------------------------------
// The port menu's CPU setting. A rate is only ever raised, the old one goes back at exit, and
// when Horizon OC is running its own profile wins (fighting it froze consoles in other ports).
ClkrstSession g_cpu;
bool g_clk_open = false;
u32 g_cpu_old = 0, g_cpu_set = 0;
AppletHookCookie g_clk_hook;

bool service_running(const char* name) {
    Handle h;
    const Result rc = smRegisterService(&h, smEncodeName(name), false, 1);
    if (R_SUCCEEDED(rc)) {
        smUnregisterService(smEncodeName(name));
        svcCloseHandle(h);
        return false;
    }
    return rc == MAKERESULT(21, 4);  // sm: already registered
}

void clk_reapply() {
    if (g_clk_open && g_cpu_set && appletGetFocusState() == AppletFocusState_InFocus)
        clkrstSetClockRate(&g_cpu, g_cpu_set);
}

void clk_hook(AppletHookType hook, void*) {
    if (hook == AppletHookType_OnFocusState || hook == AppletHookType_OnOperationMode ||
        hook == AppletHookType_OnPerformanceMode || hook == AppletHookType_OnResume)
        clk_reapply();
}

void set_cpu_mhz(int mhz) {
    if (!g_clk_open) {
        if (mhz == 0)
            return;
        if (service_running("hoc:clk")) {
            std::printf("[clock] Horizon OC is running: its profile sets the CPU clock\n");
            return;
        }
        if (!hosversionAtLeast(8, 0, 0) || R_FAILED(clkrstInitialize()) ||
            R_FAILED(clkrstOpenSession(&g_cpu, PcvModuleId_CpuBus, 3))) {
            std::printf("[clock] clkrst unavailable: CPU clock unchanged\n");
            return;
        }
        g_clk_open = true;
        clkrstGetClockRate(&g_cpu, &g_cpu_old);
        appletHook(&g_clk_hook, clk_hook, nullptr);
    }
    if (mhz == 0) {
        if (g_cpu_set && g_cpu_old)
            clkrstSetClockRate(&g_cpu, g_cpu_old);
        g_cpu_set = 0;
        std::printf("[clock] CPU back to %u MHz\n", g_cpu_old / 1000000u);
        return;
    }
    const u32 want = static_cast<u32>(mhz) * 1000000u;
    if (g_cpu_old >= want && !g_cpu_set) {
        std::printf("[clock] CPU already at %u MHz: left alone\n", g_cpu_old / 1000000u);
        return;
    }
    clkrstSetClockRate(&g_cpu, want);
    u32 now = 0;
    clkrstGetClockRate(&g_cpu, &now);
    g_cpu_set = now ? now : want;
    std::printf("[clock] CPU %u -> %u MHz\n", g_cpu_old / 1000000u, now / 1000000u);
}

void clk_restore() {
    if (!g_clk_open)
        return;
    appletUnhook(&g_clk_hook);
    if (g_cpu_set && g_cpu_old)
        clkrstSetClockRate(&g_cpu, g_cpu_old);
    clkrstCloseSession(&g_cpu);
    clkrstExit();
    g_clk_open = false;
}

// --- the game ------------------------------------------------------------------------------
std::vector<std::string> g_args;
int g_rc = 0;

void game_thread(void*) {
    std::vector<char*> argv;
    for (std::string& a : g_args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    g_rc = dream_boot_main(static_cast<int>(argv.size() - 1), argv.data());
}

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Shown on the console's own text screen when the game cannot start, until Plus is pressed.
void message_screen(const char* fmt, ...) {
    consoleInit(nullptr);
    std::va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n\nPress + to exit.\n");
    consoleUpdate(nullptr);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
        svcSleepThread(16'000'000ll);
    }
    consoleExit(nullptr);
}

}  // namespace

// libnx calls this for any CPU exception in the application.
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    char text[1024];
    int n = std::snprintf(text, sizeof text,
                          "SA-nx crash\nexception %u\npc  0x%lx (module +0x%lx)\nlr  0x%lx "
                          "(module +0x%lx)\nfar 0x%lx\nsp  0x%lx\n",
                          ctx->error_desc, (unsigned long)ctx->pc.x,
                          (unsigned long)off(ctx->pc.x), (unsigned long)ctx->lr.x,
                          (unsigned long)off(ctx->lr.x), (unsigned long)ctx->far.x,
                          (unsigned long)ctx->sp.x);
    for (int i = 0; i < 29 && n > 0 && n < static_cast<int>(sizeof text) - 40; ++i)
        n += std::snprintf(text + n, sizeof text - n, "x%d 0x%lx\n", i,
                           (unsigned long)ctx->cpu_gprs[i].x);
    crash_write(text);
    svcExitProcess();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::set_terminate(on_terminate);
    open_logs();
    std::printf("SA-nx (Switch) starting\n");

    const AppletType at = appletGetAppletType();
    if (at != AppletType_Application && at != AppletType_SystemApplication) {
        std::printf("[switch] applet mode (%d): not enough memory\n", static_cast<int>(at));
        std::fflush(stdout);
        message_screen("Sonic Adventure needs the memory of a full game.\n\n"
                       "Launch it through title takeover: hold R while starting any game,\n"
                       "then open Sonic Adventure from the homebrew menu.\n"
                       "Do not launch it from the Album.");
        return 0;
    }
    {
        u64 total = 0, used = 0;
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        std::printf("[switch] memory %lu MB total, %lu MB used\n",
                    static_cast<unsigned long>(total >> 20), static_cast<unsigned long>(used >> 20));
    }

    chdir(kDir);
    mkdir("sdmc:/switch/sonicadv/save", 0777);
    if (!exists("sdmc:/switch/sonicadv/sonicadv.toml") ||
        !exists("sdmc:/switch/sonicadv/1ST_READ.BIN")) {
        std::fflush(stdout);
        message_screen("Missing game files.\n\nsdmc:/switch/sonicadv/ needs sonicadv.toml,\n"
                       "1ST_READ.BIN and the disc/ folder (the GDI and its tracks).\n"
                       "See the README.");
        return 0;
    }

    dream_port_set_cpu_mhz = set_cpu_mhz;
    // The first run starts with the CPU at 1785 MHz: the stock 1020 MHz is far too slow for the
    // recompiled game (test1 ran there). Once the file exists, the port menu's choice stands.
    if (!exists("sdmc:/switch/sonicadv/sonicadv_options.txt")) {
        if (FILE* f = std::fopen("sdmc:/switch/sonicadv/sonicadv_options.txt", "wb")) {
            std::fputs("CPU_MHZ=1785\n", f);
            std::fclose(f);
            std::printf("[switch] first run: options start with the CPU at 1785 MHz\n");
        }
    }

    g_args = {"sonicadv", "--config", "sonicadv.toml", "--window", "--boot-sr", "0x600000F1",
              "--vmu", "save/vmu_A1.bin", "--report", "sonicadv_report.txt", "--options",
              "sonicadv_options.txt"};
    // Extra arguments, one per line, for testing without a new build.
    if (std::ifstream extra("sonicadv_args.txt"); extra) {
        std::string line;
        while (std::getline(extra, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && line[0] != '#')
                g_args.push_back(line);
        }
    }
    std::printf("[switch] launcher arguments:");
    for (const std::string& a : g_args)
        std::printf(" %s", a.c_str());
    std::printf("\n");
    std::fflush(stdout);

    // Every guest call is a host call, so the translated code needs a deep stack: the PC build
    // needed 256 MB (Emerald Coast's mid-stage load). Core 1, leaving core 0 to the system and
    // SDL's audio thread.
    Thread game;
    constexpr std::size_t kStack = 256u << 20;
    Result rc = threadCreate(&game, game_thread, nullptr, nullptr, kStack, 0x2C, 1);
    if (R_FAILED(rc)) {
        std::fprintf(stderr, "[switch] cannot create the game thread (0x%X)\n", rc);
        std::fflush(stderr);
        message_screen("Could not start the game thread (0x%X).", rc);
        return 1;
    }
    threadStart(&game);
    threadWaitForExit(&game);
    threadClose(&game);

    std::printf("[switch] the launcher returned %d\n", g_rc);
    clk_restore();
    g_flusher_stop = true;
    threadWaitForExit(&g_flusher);
    threadClose(&g_flusher);
    std::fflush(stdout);
    std::fflush(stderr);
    return g_rc;
}
