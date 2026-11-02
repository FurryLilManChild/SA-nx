// SA-nx on the Nintendo Switch: Horizon around dream-recomp's launcher.
//
//   sdmc:/switch/sonicadv/sonicadv.nro        this program
//   sdmc:/switch/sonicadv/sonicadv.toml       the game's config, with SD paths
//   sdmc:/switch/sonicadv/1ST_READ.BIN        the game's main program, from the disc
//   sdmc:/switch/sonicadv/disc/               the GDI and its tracks
//   sdmc:/switch/sonicadv/save/vmu_A1.bin     the memory card (created on first run)
//   sdmc:/switch/sonicadv/sonicadv.log        stdout, sonicadv.err.log stderr (fresh each launch;
//                                             the previous launch's as sonicadv.prev.log)
//   sdmc:/switch/sonicadv/sonicadv_args.txt   optional extra launcher arguments, one per line
//   sdmc:/switch/sonicadv/crash_XXXXXXXX.txt  written by a crash
//   sdmc:/switch/sonicadv/sonicadv_profile.txt where the game thread spends its time (below)
//
// The clock and crash-report approach follows CoD-nx's src/switch_nx.c (MIT, burnout3-sw's base).
#include <switch.h>

#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <vector>

int dream_boot_main(int argc, char** argv);
extern void (*dream_port_set_cpu_mhz)(int mhz);
extern void (*dream_port_set_gpu_mhz)(int mhz);
extern void (*dream_port_set_mem_mhz)(int mhz);
extern volatile int dream_port_slow;  // 1: the last half second ran below 0.95x
extern volatile int dream_port_movie;  // 1: the last half second showed movie frames
extern volatile int dream_port_phase;  // the launcher: 0 playing, 1 menu, 2 ended
extern void (*dream_port_on_thread)(const char* name);
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
    // The previous launch's logs are kept beside the new ones (a relaunch used to wipe a run).
    // The SD card's FAT refuses a rename onto an existing file: delete, then rename.
    std::remove("sdmc:/switch/sonicadv/sonicadv.prev.log");
    std::rename("sdmc:/switch/sonicadv/sonicadv.log", "sdmc:/switch/sonicadv/sonicadv.prev.log");
    std::remove("sdmc:/switch/sonicadv/sonicadv.prev.err.log");
    std::rename("sdmc:/switch/sonicadv/sonicadv.err.log",
                "sdmc:/switch/sonicadv/sonicadv.prev.err.log");
    std::remove("sdmc:/switch/sonicadv/sonicadv_profile.prev.txt");
    std::rename("sdmc:/switch/sonicadv/sonicadv_profile.txt",
                "sdmc:/switch/sonicadv/sonicadv_profile.prev.txt");
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

// __start__ is an absolute linker symbol (0), not the load address; the profiler's find_module()
// asks the kernel instead, and crash offsets use that when it has run (test4 logged raw
// addresses because of this).
std::uint64_t g_base_for_crash = 0;
std::uint64_t off(std::uint64_t a) {
    return a - g_base_for_crash;
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
// The port menu's CPU and GPU settings. A rate is only ever raised, the old one goes back at exit,
// and when Horizon OC is running its own profile wins (fighting it froze consoles in other ports).
// The system resets clocks when the console is docked or undocked, or the game regains focus, so
// the chosen rates are put back on those events.
struct Clock {
    PcvModuleId module;
    const char* name;
    ClkrstSession session{};
    bool open = false;
    u32 old_hz = 0, set_hz = 0;
};
Clock g_cpu_clock{PcvModuleId_CpuBus, "CPU"};
Clock g_gpu_clock{PcvModuleId_GPU, "GPU"};
Clock g_mem_clock{PcvModuleId_EMC, "memory"};
bool g_clkrst = false;
bool g_hooked = false;
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
    if (appletGetFocusState() != AppletFocusState_InFocus)
        return;
    for (Clock* c : {&g_cpu_clock, &g_gpu_clock, &g_mem_clock})
        if (c->open && c->set_hz)
            clkrstSetClockRate(&c->session, c->set_hz);
}

void clk_hook(AppletHookType hook, void*) {
    if (hook == AppletHookType_OnFocusState || hook == AppletHookType_OnOperationMode ||
        hook == AppletHookType_OnPerformanceMode || hook == AppletHookType_OnResume)
        clk_reapply();
}

// The Switch's GPU table has 460.8 and 614.4 MHz; the menu shows them rounded.
u32 to_hz(const Clock& c, int mhz) {
    if (c.module == PcvModuleId_GPU && mhz == 460)
        return 460800000u;
    if (c.module == PcvModuleId_GPU && mhz == 614)
        return 614400000u;
    if (c.module == PcvModuleId_EMC && mhz == 1331)
        return 1331200000u;
    return static_cast<u32>(mhz) * 1000000u;
}

void set_clock(Clock& c, int mhz) {
    if (!c.open) {
        if (mhz == 0)
            return;
        if (service_running("hoc:clk")) {
            std::printf("[clock] Horizon OC is running: its profile sets the %s clock\n", c.name);
            return;
        }
        if (!g_clkrst) {
            if (!hosversionAtLeast(8, 0, 0) || R_FAILED(clkrstInitialize())) {
                std::printf("[clock] clkrst unavailable: %s clock unchanged\n", c.name);
                return;
            }
            g_clkrst = true;
        }
        if (R_FAILED(clkrstOpenSession(&c.session, c.module, 3))) {
            std::printf("[clock] no clkrst session for the %s: unchanged\n", c.name);
            return;
        }
        c.open = true;
        clkrstGetClockRate(&c.session, &c.old_hz);
        if (!g_hooked) {
            appletHook(&g_clk_hook, clk_hook, nullptr);
            g_hooked = true;
        }
    }
    if (mhz == 0) {
        if (c.set_hz && c.old_hz)
            clkrstSetClockRate(&c.session, c.old_hz);
        c.set_hz = 0;
        std::printf("[clock] %s back to %u MHz\n", c.name, c.old_hz / 1000000u);
        return;
    }
    const u32 want = to_hz(c, mhz);
    if (c.old_hz >= want && !c.set_hz) {
        std::printf("[clock] %s already at %u MHz: left alone\n", c.name, c.old_hz / 1000000u);
        return;
    }
    clkrstSetClockRate(&c.session, want);
    u32 now = 0;
    clkrstGetClockRate(&c.session, &now);
    c.set_hz = now ? now : want;
    std::printf("[clock] %s %u -> %u MHz\n", c.name, c.old_hz / 1000000u, now / 1000000u);
}

void set_cpu_mhz(int mhz) {
    set_clock(g_cpu_clock, mhz);
}

void set_gpu_mhz(int mhz) {
    set_clock(g_gpu_clock, mhz);
}

void set_mem_mhz(int mhz) {
    set_clock(g_mem_clock, mhz);
}

void clk_restore() {
    if (g_hooked)
        appletUnhook(&g_clk_hook);
    for (Clock* c : {&g_cpu_clock, &g_gpu_clock, &g_mem_clock}) {
        if (!c->open)
            continue;
        if (c->set_hz && c->old_hz)
            clkrstSetClockRate(&c->session, c->old_hz);
        clkrstCloseSession(&c->session);
        c->open = false;
    }
    if (g_clkrst)
        clkrstExit();
    g_clkrst = false;
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

// --- exception unwinding: a cache in front of libgcc's FDE search -----------------------------
// The game's longjmp (guest 0x0C110820) reaches the translated setjmp through a C++ exception
// (dream::sh4::nonlocal_return), several hundred times a second in play. For every frame it
// unwinds, libgcc looks the return address up in this module's frame tables, a search over
// hundreds of thousands of entries: 56% of the unwinding time, about 1.8% of the game thread
// below full speed in switch-test26's profile. The same few call sites repeat, and the tables
// never change in a static executable, so each answer is kept per thread. The struct is libgcc's
// dwarf_eh_bases (unwind-dw2-fde.h).
struct DwarfEhBases {
    void* tbase;
    void* dbase;
    void* func;
};
extern "C" const void* __real__Unwind_Find_FDE(void* pc, DwarfEhBases* bases);
extern "C" const void* __wrap__Unwind_Find_FDE(void* pc, DwarfEhBases* bases) {
    struct Entry {
        void* pc;
        const void* fde;
        DwarfEhBases bases;
    };
    constexpr std::size_t kEntries = 4096;  // a power of two
    static thread_local Entry* table = nullptr;
    if (!table)
        table = new (std::nothrow) Entry[kEntries]();
    if (!table)
        return __real__Unwind_Find_FDE(pc, bases);
    const std::uint64_t key = reinterpret_cast<std::uintptr_t>(pc);
    Entry& e = table[((key >> 2) * 0x9E3779B97F4A7C15ull) >> 52];
    if (e.pc == pc && e.fde) {
        *bases = e.bases;
        return e.fde;
    }
    const void* fde = __real__Unwind_Find_FDE(pc, bases);
    if (fde) {
        e.pc = pc;
        e.fde = fde;
        e.bases = *bases;
    }
    return fde;
}

// --- sampling profiler --------------------------------------------------------------------
// About 500 times a second a thread on core 0 pauses the game thread, reads its registers and the
// chain of frame records on its stack (each caller's return address), and lets it go again. Only
// samples taken while the game is playing count: menu time and the end-of-run report and teardown
// are counted separately (dream_port_phase, set by the launcher). Every 20 seconds the counts are
// written to sonicadv_profile.tmp and then moved over sonicadv_profile.txt, so a run that ends
// mid-write keeps the last whole file. Addresses are offsets into this module (its load address
// taken from the kernel's memory map, not from a linker symbol); Claude resolves them with the
// build's ELF. The pauses cost the game thread a few percent.
Handle g_prof_target = INVALID_HANDLE;
std::uint64_t g_stack_lo = 0, g_stack_hi = 0;  // the game thread's stack, for walking it safely
Thread g_prof;
volatile bool g_prof_stop = false;
std::uint64_t g_module_base = 0, g_module_end = 0;

void find_module() {
    MemoryInfo mi{};
    u32 pi = 0;
    // The code segment this function is in starts at the module's load address (crt0 is first).
    if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, reinterpret_cast<u64>(&find_module)))) {
        g_module_base = mi.addr;
        g_base_for_crash = mi.addr;
        // Code, then read-only data, then data: walk forward while the mappings stay contiguous.
        u64 at = mi.addr + mi.size;
        g_module_end = at;
        for (int i = 0; i < 8; ++i) {
            MemoryInfo next{};
            if (R_FAILED(svcQueryMemory(&next, &pi, at)) || next.addr != at ||
                (next.type & 0xFF) == MemType_Unmapped)
                break;
            at = next.addr + next.size;
            g_module_end = at;
        }
    }
}

struct Profile {
    static constexpr int kDepth = 8;
    std::unordered_map<std::uint64_t, std::uint32_t> pcs;        // where the thread was
    std::unordered_map<std::string, std::uint32_t> stacks;      // pc, lr, then callers
    std::uint64_t samples = 0, failed = 0, in_menu = 0, after_run = 0;
    // The same samples again, split: [0] full speed, [1] below 0.95x (dream_port_slow), [2] a
    // movie playing (dream_port_movie, whatever the speed).
    std::unordered_map<std::uint64_t, std::uint32_t> pcs_by[3];
    std::unordered_map<std::string, std::uint32_t> stacks_by[3];
    std::uint64_t samples_by[3] = {0, 0, 0};
};

std::uint64_t mod(std::uint64_t a) {
    return (a >= g_module_base && a < g_module_end) ? a - g_module_base : 0xFFFFFFFFull;
}

void prof_write(const Profile& p) {
    FILE* f = std::fopen("sdmc:/switch/sonicadv/sonicadv_profile.tmp", "w");
    if (!f)
        return;
    std::fprintf(f,
                 "SA-nx profile 2: %llu playing samples, %llu unreadable, %llu in a menu, %llu "
                 "after the run; module 0x%llx..0x%llx\n",
                 (unsigned long long)p.samples, (unsigned long long)p.failed,
                 (unsigned long long)p.in_menu, (unsigned long long)p.after_run,
                 (unsigned long long)g_module_base, (unsigned long long)g_module_end);
    auto pcs = [f](const char* name, const std::unordered_map<std::uint64_t, std::uint32_t>& m) {
        std::vector<std::pair<std::uint32_t, std::uint64_t>> v;
        for (const auto& [a, n] : m) v.push_back({n, a});
        std::sort(v.rbegin(), v.rend());
        std::fprintf(f, "== %s %zu\n", name, v.size());
        for (std::size_t i = 0; i < v.size() && i < 2500; ++i)
            std::fprintf(f, "%llx %u\n", (unsigned long long)v[i].second, v[i].first);
    };
    auto stacks = [f](const char* name, const std::unordered_map<std::string, std::uint32_t>& m) {
        std::vector<std::pair<std::uint32_t, const std::string*>> v;
        for (const auto& [k, n] : m) v.push_back({n, &k});
        std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        std::fprintf(f, "== %s %zu\n", name, v.size());
        for (std::size_t i = 0; i < v.size() && i < 2500; ++i)
            std::fprintf(f, "%u %s\n", v[i].first, v[i].second->c_str());
    };
    std::fprintf(f, "split: %llu samples at full speed, %llu below 0.95x, %llu in a movie\n",
                 (unsigned long long)p.samples_by[0], (unsigned long long)p.samples_by[1],
                 (unsigned long long)p.samples_by[2]);
    pcs("pc", p.pcs);
    stacks("stacks", p.stacks);
    pcs("pc_fast", p.pcs_by[0]);
    pcs("pc_slow", p.pcs_by[1]);
    stacks("stacks_fast", p.stacks_by[0]);
    stacks("stacks_slow", p.stacks_by[1]);
    pcs("pc_movie", p.pcs_by[2]);
    stacks("stacks_movie", p.stacks_by[2]);
    std::fprintf(f, "== end\n");
    std::fclose(f);
    // The SD card's FAT refuses a rename onto an existing file: delete, then rename.
    std::remove("sdmc:/switch/sonicadv/sonicadv_profile.txt");
    std::rename("sdmc:/switch/sonicadv/sonicadv_profile.tmp",
                "sdmc:/switch/sonicadv/sonicadv_profile.txt");
}

void profiler(void*) {
    Profile p;
    p.pcs.reserve(1 << 15);
    p.stacks.reserve(1 << 15);
    std::uint64_t last_write = armGetSystemTick();
    const std::uint64_t write_every = armGetSystemTickFreq() * 20;
    char key[Profile::kDepth * 10 + 32];
    while (!g_prof_stop) {
        svcSleepThread(2'000'000ll);  // 2 ms: about 500 samples a second
        const int phase = dream_port_phase;
        if (R_FAILED(svcSetThreadActivity(g_prof_target, ThreadActivity_Paused))) {
            ++p.failed;
            continue;
        }
        ThreadContext ctx;
        const Result rc = svcGetThreadContext3(&ctx, g_prof_target);
        std::uint64_t chain[Profile::kDepth];
        int depth = 0;
        if (R_SUCCEEDED(rc) && phase == 0) {
            // Frame records: [fp] is the caller's fp, [fp + 8] the return address. Read while the
            // thread is paused, and only inside its own stack, so a bad fp cannot fault here.
            std::uint64_t fp = ctx.fp;
            while (depth < Profile::kDepth && fp >= g_stack_lo && fp + 16 <= g_stack_hi &&
                   (fp & 7) == 0) {
                const std::uint64_t* rec = reinterpret_cast<const std::uint64_t*>(fp);
                chain[depth++] = rec[1];
                if (rec[0] <= fp)
                    break;  // frames grow toward higher addresses; anything else is not a chain
                fp = rec[0];
            }
        }
        svcSetThreadActivity(g_prof_target, ThreadActivity_Runnable);
        if (R_FAILED(rc)) {
            ++p.failed;
            continue;
        }
        if (phase == 1) {
            ++p.in_menu;
            continue;
        }
        if (phase == 2) {
            ++p.after_run;
            continue;
        }
        const int slow = dream_port_movie ? 2 : dream_port_slow ? 1 : 0;
        ++p.samples;
        ++p.samples_by[slow];
        ++p.pcs[mod(ctx.pc.x)];
        ++p.pcs_by[slow][mod(ctx.pc.x)];
        int n = std::snprintf(key, sizeof key, "%llx %llx", (unsigned long long)mod(ctx.pc.x),
                              (unsigned long long)mod(ctx.lr));
        for (int i = 0; i < depth && n > 0 && n < static_cast<int>(sizeof key) - 12; ++i)
            n += std::snprintf(key + n, sizeof key - n, " %llx", (unsigned long long)mod(chain[i]));
        ++p.stacks[std::string(key)];
        ++p.stacks_by[slow][std::string(key)];
        if (armGetSystemTick() - last_write >= write_every) {
            last_write = armGetSystemTick();
            prof_write(p);
        }
    }
    prof_write(p);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::set_terminate(on_terminate);
    open_logs();
    std::printf("SA-nx (Switch) starting\n");
    find_module();  // the load address, for crash reports and the profiler

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
    // The disc dump is what a player must supply: a GDI (with its tracks) or a CHD in disc/.
    // 1ST_READ.BIN is taken from it on the first start (release 0.5.0; the first release build
    // still demanded it here and stopped before the launcher could extract it).
    bool have_disc = false;
    if (DIR* d = opendir("sdmc:/switch/sonicadv/disc")) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".gdi") == 0 ||
                                 n.compare(n.size() - 4, 4, ".chd") == 0))
                have_disc = true;
        }
        closedir(d);
    }
    if (!exists("sdmc:/switch/sonicadv/sonicadv.toml") || !have_disc) {
        std::printf("[switch] missing game files: sonicadv.toml %s, disc dump %s\n",
                    exists("sdmc:/switch/sonicadv/sonicadv.toml") ? "found" : "missing",
                    have_disc ? "found" : "missing");
        std::fflush(stdout);
        message_screen("Missing game files.\n\nPut your Sonic Adventure disc dump (the .gdi file and\n"
                       "its three track files) in sdmc:/switch/sonicadv/disc/,\n"
                       "next to sonicadv.toml. See the README.");
        return 0;
    }

    dream_port_set_cpu_mhz = set_cpu_mhz;
    dream_port_set_gpu_mhz = set_gpu_mhz;
    dream_port_set_mem_mhz = set_mem_mhz;
    // Helper threads the launcher starts. Core 1 is the game's; the sound processor gets core 2;
    // the render thread core 0, with the main thread (asleep while the game runs), SDL's audio
    // thread and the profiler.
    dream_port_on_thread = [](const char* name) {
        int core = -1;
        if (!std::strcmp(name, "aica"))
            core = 2;
        else if (!std::strcmp(name, "render"))
            core = 0;
        if (core < 0)
            return;
        const Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
        std::printf("[switch] thread %s on core %d%s\n", name, core, R_FAILED(rc) ? " (refused)" : "");
    };
#ifndef SANX_RELEASE
    // Development builds: the first run starts with the CPU at 1785 MHz. A release starts with
    // everything at the system's own clocks and the per-triangle translucency (PortOptions'
    // defaults), which hold full speed in play (switch-test27/28).
    if (!exists("sdmc:/switch/sonicadv/sonicadv_options.txt")) {
        if (FILE* f = std::fopen("sdmc:/switch/sonicadv/sonicadv_options.txt", "wb")) {
            std::fputs("CPU_MHZ=1785\n", f);
            std::fclose(f);
            std::printf("[switch] first run: options start with the CPU at 1785 MHz\n");
        }
    }
#endif

    g_args = {"sonicadv", "--config", "sonicadv.toml", "--window", "--boot-sr", "0x600000F1",
              "--vmu", "save/vmu_A1.bin", "--options", "sonicadv_options.txt",
              // The game's wait loop: 0x8C641F98 and 0x8C641FD8 call the empty function at
              // 0x8C64162C until an interrupt changes a flag (switch-test12 profile: 10.8% of the
              // game thread at full speed). The clock jumps to the next event instead.
              "--idle-skip", "0x0C64162C:0x0C641FA0,0x0C641FE0",
              // The first word of every gameplay module (stages, fields, bosses, Chao garden,
              // minigames), so the in-game resolution applies only in play (switch-test18).
              "--play-modules",
              "0x2FD62FE6,0x432BD307,0x432BD357,0x4F222FE6,0x55484F22,0x6032D30F,0x60505548,"
              "0x63505548,0x6432D317,0x6E432FE6,0x7FFC4F22,0xD35F7FE8,0xD507D308"};
#ifndef SANX_RELEASE
    // Development builds write the end-of-run report (untranslated call targets, device
    // counters). A release leaves it out: building it took about a minute after quitting, even
    // when it was not written to a file, so a release also skips building it (--quick-exit).
    g_args.insert(g_args.begin() + 7, {"--report", "sonicadv_report.txt"});
#else
    g_args.push_back("--quick-exit");
#endif
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
    g_prof_target = game.handle;
    // The thread runs on the mirror libnx maps its stack to; the original allocation is no longer
    // accessible once mapped.
    g_stack_lo = reinterpret_cast<std::uint64_t>(game.stack_mirror);
    g_stack_hi = g_stack_lo + game.stack_sz;
#ifdef SANX_RELEASE
    const bool profiling = false;  // a release does not sample the game thread
#else
    const bool profiling =
        R_SUCCEEDED(threadCreate(&g_prof, profiler, nullptr, nullptr, 0x10000, 0x2B, 0));
#endif
    if (profiling) {
        threadStart(&g_prof);
        std::printf("[profile] sampling the game thread, written to sonicadv_profile.txt\n");
    }
    threadWaitForExit(&game);
    if (profiling) {
        g_prof_stop = true;
        threadWaitForExit(&g_prof);
        threadClose(&g_prof);
    }
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
