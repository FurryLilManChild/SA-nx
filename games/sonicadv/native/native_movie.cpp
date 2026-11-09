// SA-nx Test 2.3: the movie player's IDCT (MOVIE.BIN, 0x0CEB1504) run natively (movie_idct.cpp),
// checked against the translation on real calls.
//
// During a movie this one function took a third of the game thread (switch profile: 33.1% of the
// samples, movies at 0.82x). The native version does the same arithmetic in the same order, so the
// two must agree bit for bit: registers, FPU registers, cycle count and every byte stored.
//
// That is checked rather than assumed. The first 20,000 calls (a few seconds of movie), and one in
// 64 after that, run both: the translation first, with every store journalled, then its stores are
// undone and the copy runs from the same state. The registers and every byte either one stored
// must be equal. The first difference switches the copy off for the rest of the run, puts the
// translation's result back, and goes to the log with what differed.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dream/runtime/mem/dc_memory.h"
#include "dream/runtime/sh4/abi.h"
#include "dream/runtime/sh4/ops.h"

namespace dream::gen::sonicadv {
void fn_0ceb1504_8ceb0000__resume(dream::sh4::Ctx& c, dream::Memory& m, std::uint32_t resume_pc);
}  // namespace dream::gen::sonicadv
namespace sanx {
bool movie_idct_native(dream::sh4::Ctx& c, dream::Memory& m);
}

namespace {
using dream::Memory;
using dream::sh4::Ctx;

constexpr std::uint64_t kCheckAll = 20000;  // calls checked one by one
constexpr std::uint64_t kCheckEvery = 64;   // then one call in this many

std::uint64_t g_calls = 0, g_checked = 0, g_skipped = 0;
bool g_off = false;

void original(Ctx& c, Memory& m) { dream::gen::sonicadv::fn_0ceb1504_8ceb0000__resume(c, m, 0); }

std::uint64_t read_sized(Memory& m, std::uint32_t addr, unsigned size) {
    switch (size) {
        case 1: return m.read8(addr);
        case 2: return m.read16(addr);
        case 8: return (static_cast<std::uint64_t>(m.read32(addr + 4)) << 32) | m.read32(addr);
        default: return m.read32(addr);
    }
}

// Which part of the context differs, for the log.
std::string ctx_difference(const Ctx& a, const Ctx& b) {
    char buf[160];
    for (int i = 0; i < 16; ++i)
        if (a.r[i] != b.r[i]) {
            std::snprintf(buf, sizeof buf, "r%d %08x vs %08x", i, a.r[i], b.r[i]);
            return buf;
        }
    for (int i = 0; i < 16; ++i)
        if (std::memcmp(&a.fr[i], &b.fr[i], 4)) {
            std::snprintf(buf, sizeof buf, "fr%d %08x vs %08x", i, dream::sh4::f2u(a.fr[i]),
                          dream::sh4::f2u(b.fr[i]));
            return buf;
        }
    for (int i = 0; i < 16; ++i)
        if (std::memcmp(&a.xf[i], &b.xf[i], 4)) {
            std::snprintf(buf, sizeof buf, "xf%d %08x vs %08x", i, dream::sh4::f2u(a.xf[i]),
                          dream::sh4::f2u(b.xf[i]));
            return buf;
        }
    std::snprintf(buf, sizeof buf,
                  "fpul %08x/%08x t %u/%u fpscr %08x/%08x cycles %llu/%llu pc %08x/%08x",
                  a.fpul, b.fpul, a.t, b.t, a.fpscr, b.fpscr,
                  static_cast<unsigned long long>(a.cycles),
                  static_cast<unsigned long long>(b.cycles), a.pc, b.pc);
    return buf;
}

void checked_call(Ctx& c, Memory& m, dream::mem::DcMemory& dc) {
    using dream::sh4::g_tail_entry_pr;
    using dream::sh4::g_tail_entry_set;
    using dream::sh4::g_tail_entry_target;
    const bool tail_set = g_tail_entry_set;
    const std::uint32_t tail_pr = g_tail_entry_pr, tail_target = g_tail_entry_target;
    const Ctx before = c;
    const std::uint64_t next_event = c.next_event;

    // The translation, journalled, with interrupts held off so nothing else runs in between.
    c.next_event = ~0ull;
    dc.journal.clear();
    dc.journal_saw_device = false;
    m.journaling = true;
    original(c, m);
    m.journaling = false;
    const Ctx want = c;
    const std::vector<dream::mem::DcMemory::WriteRecord> stores = dc.journal;
    std::vector<std::uint64_t> want_values;
    want_values.reserve(stores.size());
    for (const auto& w : stores) want_values.push_back(read_sized(m, w.addr, w.size));
    dc.undo_journal();
    if (dc.journal_saw_device) {  // not a pure function of memory this time: keep the translation
        ++g_skipped;
        c = before;
        g_tail_entry_set = tail_set;
        g_tail_entry_pr = tail_pr;
        g_tail_entry_target = tail_target;
        original(c, m);
        return;
    }

    // The copy, from the same state.
    c = before;
    c.next_event = ~0ull;
    g_tail_entry_set = tail_set;
    g_tail_entry_pr = tail_pr;
    g_tail_entry_target = tail_target;
    m.journaling = true;
    const bool handled = sanx::movie_idct_native(c, m);
    m.journaling = false;
    if (!handled) {
        ++g_skipped;
        c = before;
        c.next_event = next_event;
        g_tail_entry_set = tail_set;
        g_tail_entry_pr = tail_pr;
        g_tail_entry_target = tail_target;
        original(c, m);
        return;
    }
    const std::vector<dream::mem::DcMemory::WriteRecord> mine = dc.journal;
    dc.journal.clear();

    std::string diff;
    if (std::memcmp(&c, &want, sizeof c))
        diff = "registers: " + ctx_difference(want, c);
    for (std::size_t i = 0; diff.empty() && i < stores.size(); ++i)
        if (read_sized(m, stores[i].addr, stores[i].size) != want_values[i]) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "memory at %08x: %llx, translation %llx",
                          stores[i].addr,
                          static_cast<unsigned long long>(read_sized(m, stores[i].addr,
                                                                     stores[i].size)),
                          static_cast<unsigned long long>(want_values[i]));
            diff = buf;
        }
    // A store only the copy made must have left the value the translation left there: the old one.
    for (std::size_t i = 0; diff.empty() && i < mine.size(); ++i) {
        bool theirs = false;
        for (const auto& w : stores) theirs = theirs || w.addr == mine[i].addr;
        if (!theirs && read_sized(m, mine[i].addr, mine[i].size) != mine[i].old_value) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "a store only the copy made, at %08x", mine[i].addr);
            diff = buf;
        }
    }
    c.next_event = next_event;
    ++g_checked;
    if (g_checked == kCheckAll || (g_checked % 2000 == 0 && g_checked > kCheckAll)) {
        std::printf("[idct] native copy: %llu calls, %llu checked against the translation, all "
                    "equal\n",
                    static_cast<unsigned long long>(g_calls),
                    static_cast<unsigned long long>(g_checked));
        std::fflush(stdout);
    }
    if (diff.empty())
        return;

    // Different: put the translation's result back and stop using the copy.
    g_off = true;
    std::printf("[idct] native copy DIFFERS from the translation on call %llu (%s); using the "
                "translation from now on\n",
                static_cast<unsigned long long>(g_calls), diff.c_str());
    std::fflush(stdout);
    dc.journal = mine;
    dc.undo_journal();
    c = before;
    g_tail_entry_set = tail_set;
    g_tail_entry_pr = tail_pr;
    g_tail_entry_target = tail_target;
    original(c, m);
}

void movie_idct(Ctx& c, Memory& m) {
    if (g_off || (c.fpscr & (dream::sh4::FPSCR_SZ | dream::sh4::FPSCR_PR)) != 0) {
        original(c, m);
        return;
    }
    ++g_calls;
    auto* dc = dynamic_cast<dream::mem::DcMemory*>(&m);
    const bool check = g_calls <= kCheckAll || g_calls % kCheckEvery == 0;
    if (check && dc && !m.journaling) {
        checked_call(c, m, *dc);
        return;
    }
    if (!sanx::movie_idct_native(c, m))
        original(c, m);
}

[[maybe_unused]] const bool g_registered = (dream::sh4::register_native_override(0x0CEB1504u, &movie_idct), true);
}  // namespace
