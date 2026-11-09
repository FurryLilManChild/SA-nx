// SA-nx Test 2.3: a native version of the movie player's inverse DCT (MOVIE.BIN, 0x0CEB1504).
//
// The function runs once per macroblock of every movie frame and took a third of the game thread
// during movies (switch profile, movies at 0.82x). It is hand-written SH-4: four passes over the
// six 8x8 blocks of a macroblock, each a 4x4 matrix (XMTRX, loaded with FMOV pairs) applied to
// rows of four floats with FTRV, with the second and fourth passes adding and subtracting halves
// (a butterfly) and the fourth one rounding to 16-bit samples for the output pointers in its
// argument block.
//
// This does the same arithmetic in the same order: FTRV as dream-recomp evaluates it (products
// in double, summed left to right, rounded to float), single-precision adds, FTRC as ops.h has it.
// What it leaves out is the cost of emulating each instruction: guest memory is read and written
// through one host pointer, the matrix is converted to double once per pass rather than at every
// FTRV, and the FPU registers are host values. The guest registers, the FPU registers, FPUL, T
// and the cycle count it leaves behind are the translation's, so the caller cannot tell the two
// apart; native_movie.cpp checks that on real calls.
//
// The argument block (r4 + 32 on entry, called A here), as the code uses it:
//   A+0   float added to every output sample before rounding (and the word after it)
//   A+4   bytes from one block to the next in passes 1 and 3
//   A+8   coded-block mask: bit 31 first, one bit per block
//   A+12  coefficient buffer (passes 1 and 2 read it)
//   A+20  pointer into a list of six output pointers (advanced as the blocks are done)
//   A+24  integer subtracted from every rounded sample
//   A+32  first matrix, A+36 second matrix, A+40 the intermediate buffer
//   A+44  block counter, A+48 scratch (both written by pass 4)
#include <cstdint>
#include <cstring>

#include "dream/runtime/memory.h"
#include "dream/runtime/sh4/abi.h"
#include "dream/runtime/sh4/ops.h"

#if defined(_MSC_VER)
#define SANX_INLINE __forceinline
#else
#define SANX_INLINE inline __attribute__((always_inline))
#endif

namespace sanx {

using dream::Memory;
using dream::sh4::Ctx;
using dream::sh4::f2u;
using dream::sh4::u2f;

namespace {

template <bool kSlow>
struct Ram {
    std::uint8_t* base;
    std::uint32_t mask;
    // Stores go through the memory's own path while anything watches them (the journal the
    // check in native_movie.cpp uses, the write hash, a watch); reads are plain RAM either way.
    Memory* slow;
    SANX_INLINE std::uint8_t* at(std::uint32_t a) const { return base + (a & mask); }
    SANX_INLINE std::uint32_t l32(std::uint32_t a) const {
        std::uint32_t v;
        std::memcpy(&v, at(a), 4);
        return v;
    }
    SANX_INLINE float lf(std::uint32_t a) const { return u2f(l32(a)); }
    SANX_INLINE void s32(std::uint32_t a, std::uint32_t v) const {
        if constexpr (kSlow)
            slow->write32(a, v);
        else
            std::memcpy(at(a), &v, 4);
    }
    SANX_INLINE void sf(std::uint32_t a, float f) const { s32(a, f2u(f)); }
    SANX_INLINE void s16(std::uint32_t a, std::uint16_t v) const {
        if constexpr (kSlow)
            slow->write16(a, v);
        else
            std::memcpy(at(a), &v, 2);
    }
    // Four floats at a, as FMOV pair loads put them in FRn..FRn+3.
    SANX_INLINE void l4(std::uint32_t a, float* out) const { std::memcpy(out, at(a), 16); }
    SANX_INLINE void s4(std::uint32_t a, const float* in) const {
        if constexpr (kSlow)
            for (int i = 0; i < 4; ++i) slow->write32(a + 4 * i, f2u(in[i]));
        else
            std::memcpy(at(a), in, 16);
    }
};

bool in_ram(std::uint32_t a) { return dream::sh4::fast_area(a); }

// XMTRX as doubles: column k of the matrix is XF[4k..4k+3].
struct Matrix {
#if defined(__GNUC__)
    // Two lanes of double: rows 0 and 1, rows 2 and 3. Each lane does exactly the scalar
    // arithmetic (a product, then the running sum, in order; -ffp-contract=off keeps the
    // multiplies and adds separate), so the result is the same bit for bit.
    typedef double v2d __attribute__((vector_size(16)));
    v2d lo[4], hi[4];
    SANX_INLINE void load(const float* xf) {
        for (int k = 0; k < 4; ++k) {
            lo[k] = v2d{static_cast<double>(xf[4 * k]), static_cast<double>(xf[4 * k + 1])};
            hi[k] = v2d{static_cast<double>(xf[4 * k + 2]), static_cast<double>(xf[4 * k + 3])};
        }
    }
    SANX_INLINE void apply(float* v) const {
        const double x = v[0], y = v[1], z = v[2], w = v[3];
        const v2d a = ((lo[0] * x + lo[1] * y) + lo[2] * z) + lo[3] * w;
        const v2d b = ((hi[0] * x + hi[1] * y) + hi[2] * z) + hi[3] * w;
        v[0] = static_cast<float>(a[0]);
        v[1] = static_cast<float>(a[1]);
        v[2] = static_cast<float>(b[0]);
        v[3] = static_cast<float>(b[1]);
    }
#else
    double m[16];
    SANX_INLINE void load(const float* xf) {
        for (int i = 0; i < 16; ++i) m[i] = static_cast<double>(xf[i]);
    }
    // FTRV on v (four floats), in place: out[i] = xf[i]*v0 + xf[i+4]*v1 + xf[i+8]*v2 + xf[i+12]*v3,
    // each product exact in double, summed left to right, rounded once to float.
    SANX_INLINE void apply(float* v) const {
        const double x = v[0], y = v[1], z = v[2], w = v[3];
        double r[4];
        for (int i = 0; i < 4; ++i) r[i] = m[i] * x + m[i + 4] * y + m[i + 8] * z + m[i + 12] * w;
        for (int i = 0; i < 4; ++i) v[i] = static_cast<float>(r[i]);
    }
#endif
};

// The XF bank from 64 bytes at a (eight FMOV pair loads into XD0..XD14).
template <bool kSlow>
SANX_INLINE void load_xf(const Ram<kSlow>& ram, std::uint32_t a, float* xf) {
    std::memcpy(xf, ram.at(a), 64);
}

template <bool kSlow>
bool run(Ctx& c, Memory& m, const Ram<kSlow>& ram);

}  // namespace

// Returns false (having changed nothing) when the call is not one it handles; the caller then
// runs the translation.
bool movie_idct_native(Ctx& c, Memory& m) {
    using dream::sh4::FPSCR_PR;
    using dream::sh4::FPSCR_SZ;
    if ((c.fpscr & (FPSCR_SZ | FPSCR_PR)) != 0 || c.cycles >= c.next_event)
        return false;
    const auto view = m.fast_view();
    if (view.ram == nullptr)
        return false;
    if (view.store_ok)
        return run(c, m, Ram<false>{view.ram, view.mask, nullptr});
    return run(c, m, Ram<true>{view.ram, view.mask, &m});
}

namespace {

template <bool kSlow>
bool run(Ctx& c, Memory& m, const Ram<kSlow>& ram) {

    std::uint32_t r[16];
    std::memcpy(r, c.r, sizeof r);
    const std::uint32_t A = r[4] + 32;
    // Everything the function touches has to be plain RAM, or the translation does it.
    if (!in_ram(A) || !in_ram(r[15] - 32) || !in_ram(r[15]))
        return false;
    const std::uint32_t buf1 = ram.l32(A + 12), list = ram.l32(A + 20), m1 = ram.l32(A + 32),
                        m2 = ram.l32(A + 36), buf2 = ram.l32(A + 40);
    if (!in_ram(buf1) || !in_ram(list) || !in_ram(m1) || !in_ram(m2) || !in_ram(buf2))
        return false;
    for (std::uint32_t i = 0; i < 6; ++i)
        if (!in_ram(ram.l32(list + 4 * i)))
            return false;

    const std::uint32_t entry_pr = dream::sh4::take_entry_pr(c, 0x0ceb1504u);
    std::uint64_t cyc = c.cycles;
    float fr[16], xf[16];
    std::memcpy(fr, c.fr, sizeof fr);
    std::memcpy(xf, c.xf, sizeof xf);
    std::uint32_t fpul = c.fpul, t = c.t;
    Matrix mx;

    // Prologue: save r14..r8 and the argument block pointer.
    std::uint32_t sp = r[15];
    sp -= 4; ram.s32(sp, r[14]);
    r[4] = A;
    for (int k = 13; k >= 8; --k) { sp -= 4; ram.s32(sp, r[k]); }
    sp -= 4; ram.s32(sp, r[4]);

    // Passes 1 and 3: the matrix at A+32 applied to the 32 floats at the start of each coded block.
    // Registers as the code leaves them: r0 = A+36's pointer, r1..r4 the four row pointers.
    const auto rows_pass = [&](std::uint32_t buf) {
        load_xf(ram, m1, xf);
        mx.load(xf);
        r[8] = 16; r[9] = 32; r[10] = 48; r[11] = 64; r[12] = 80; r[13] = 96; r[14] = 112;
        r[1] = buf;
        r[7] = 6;
        r[6] = ram.l32(A + 8);
        r[2] = r[1] + 16;
        r[5] = ram.l32(A + 4);
        r[3] = r[1] + 32;
        r[0] = m2;
        r[4] = r[1] + 48;
        for (;;) {
            t = r[6] >> 31; r[6] <<= 1;
            cyc += 2;
            if (!t) {
                cyc += 1;
            } else {
                // Eight rows of four, each replaced by XMTRX times itself. The stores are
                // FMOV pair stores with pre-decrement (FR0 to @-r1, then FR2 to @-r1), so each row
                // goes back with its halves swapped: v2 v3 v0 v1. The registers end holding the
                // last four rows (4..7) in order.
                const std::uint32_t b = r[1];
                for (std::uint32_t row = 0; row < 8; ++row) {
                    float v[4];
                    ram.l4(b + 16 * row, v);
                    mx.apply(v);
                    const float swapped[4] = {v[2], v[3], v[0], v[1]};
                    ram.s4(b + 16 * row, swapped);
                    if (row >= 4)
                        std::memcpy(&fr[4 * (row - 4)], v, 16);
                }
                cyc += 48;
            }
            --r[7]; t = r[7] == 0;
            r[1] += r[5]; r[2] += r[5]; r[3] += r[5];
            cyc += 6;
            r[4] += r[5];
            if (t)
                break;
        }
    };

    // Passes 2 and 4: rows from the first half of a block (r3) and, through the second matrix, from
    // the second half (r1) make a sum and a difference (a butterfly).
    // A first-half row: FMOV @r3+,FR2 then @r3+,FR0 (pair loads).
    const auto load_lo = [&](std::uint32_t a, float* f) {
        f[2] = ram.lf(a); f[3] = ram.lf(a + 4);
        f[0] = ram.lf(a + 8); f[1] = ram.lf(a + 12);
    };

    // Pass 1 (0x0ceb1516..0x0ceb15bc), with the entry block's 37 cycles.
    cyc += 37;
    rows_pass(buf1);

    // Pass 2 (0x0ceb15be): each coded block of buf1 through the second matrix into buf2.
    r[4] = ram.l32(sp);
    load_xf(ram, r[0], xf);  // r0 = the second matrix (left by pass 1)
    mx.load(xf);
    r[0] += 64;
    r[5] = 64; t = r[5] >> 31; r[5] <<= 1;
    r[6] = ram.l32(r[4] + 8);
    r[7] = 6;
    r[3] = ram.l32(r[4] + 12);
    r[1] = r[5] + r[3];
    r[0] = ram.l32(r[4] + 40);
    cyc += 17;
    for (;;) {
        t = r[6] >> 31; r[6] <<= 1;
        cyc += 2;
        if (!t) {
            cyc += 1;
        } else {
            r[2] = 4;
            ram.l4(r[1], &fr[8]);
            r[1] += 16;
            cyc += 3;
            // Two loops of four rows (0x0ceb15ea, then 0x0ceb1626 after r0 += 112): each row of
            // the butterfly writes eight floats a row apart and reads the next second-half row.
            for (int half = 0; half < 2; ++half) {
                do {
                    mx.apply(&fr[8]);
                    load_lo(r[3], fr);
                    r[3] += 16;
                    fr[6] = fr[2]; fr[7] = fr[3];
                    fr[4] = fr[0]; fr[5] = fr[1];
                    fr[0] += fr[8]; fr[1] += fr[9]; fr[2] += fr[10]; fr[3] += fr[11];
                    fr[4] -= fr[8]; fr[5] -= fr[9]; fr[6] -= fr[10]; fr[7] -= fr[11];
                    ram.sf(r[0], fr[0]);
                    ram.sf(r[0] + 32, fr[1]);
                    ram.sf(r[0] + 64, fr[2]);
                    ram.sf(r[0] + 96, fr[3]);
                    ram.sf(r[0] + 112, fr[4]);
                    ram.sf(r[0] + 80, fr[5]);
                    ram.sf(r[0] + 48, fr[6]);
                    ram.sf(r[0] + 16, fr[7]);
                    --r[2]; t = r[2] == 0;
                    ram.l4(r[1], &fr[8]);
                    r[1] += 16;
                    r[0] += 4;
                    cyc += 28;
                } while (!t);
                if (half == 0) {
                    r[0] += 112;
                    r[2] = 4;
                    cyc += 2;
                }
            }
            r[1] -= 16; r[1] -= r[5];
            r[0] -= 16; r[0] -= r[5];
            r[3] -= r[5];
            cyc += 5;
        }
        t = r[5] >> 31; r[5] <<= 1;
        --r[7]; t = r[7] == 0;
        r[0] += r[5]; r[3] += r[5]; r[1] += r[5];
        cyc += 7;
        const bool again = !t;
        t = r[5] & 1u; r[5] >>= 1;
        if (!again)
            break;
    }

    // Pass 3 (0x0ceb1676): the first matrix over buf2.
    cyc += 20;
    rows_pass(ram.l32(r[4] + 40));

    // Pass 4 (0x0ceb170e): the butterfly through the second matrix, plus the bias, rounded to
    // 16-bit samples at each block's output pointer.
    r[7] = ram.l32(sp);
    load_xf(ram, r[0], xf);
    mx.load(xf);
    r[0] += 64;
    r[3] = 6;
    ram.s32(r[7] + 44, r[3]);
    r[5] = 64; t = r[5] >> 31; r[5] <<= 1;
    r[3] = ram.l32(r[7] + 40);
    r[1] = r[3];
    r[6] = ram.l32(r[7] + 8);
    r[1] += r[5];
    t = r[5] >> 31; r[5] <<= 1;
    cyc += 18;
    for (;;) {
        r[2] = ram.l32(r[7] + 20);
        t = r[6] >> 31; r[6] <<= 1;
        r[0] = ram.l32(r[2]);
        r[2] += 4;
        cyc += 5;
        ram.s32(r[7] + 20, r[2]);
        if (t) {
            r[2] = 8;
            ram.l4(r[1], &fr[8]);
            r[1] += 16;
            cyc += 3;
            do {
                mx.apply(&fr[8]);
                load_lo(r[3], fr);
                r[3] += 16;
                fr[6] = fr[2]; fr[7] = fr[3];
                fr[4] = fr[0]; fr[5] = fr[1];
                fr[0] += fr[8]; fr[1] += fr[9]; fr[2] += fr[10]; fr[3] += fr[11];
                fr[4] -= fr[8]; fr[5] -= fr[9];
                fr[12] = ram.lf(r[7]); fr[13] = ram.lf(r[7] + 4);
                fr[6] -= fr[10];
                fr[8] = ram.lf(r[1]); fr[9] = ram.lf(r[1] + 4);
                r[1] += 8;
                fr[7] -= fr[11];
                fr[10] = ram.lf(r[1]); fr[11] = ram.lf(r[1] + 4);
                r[1] += 8;
                fr[0] += fr[12];
                ram.s32(r[7] + 48, r[3]);
                fr[1] += fr[12];
                r[3] = ram.l32(r[7] + 24);
                for (int k = 2; k < 8; ++k) fr[k] += fr[12];
                using dream::sh4::ftrc;
                fpul = ftrc(fr[0]); r[4] = fpul;
                fpul = ftrc(fr[1]); r[5] = fpul;
                r[4] -= r[3]; ram.s16(r[0], static_cast<std::uint16_t>(r[4]));
                fpul = ftrc(fr[2]); r[4] = fpul;
                r[5] -= r[3]; ram.s16(r[0] + 32, static_cast<std::uint16_t>(r[5]));
                fpul = ftrc(fr[3]); r[5] = fpul;
                r[4] -= r[3]; ram.s16(r[0] + 64, static_cast<std::uint16_t>(r[4]));
                fpul = ftrc(fr[4]); r[4] = fpul;
                r[5] -= r[3]; ram.s16(r[0] + 96, static_cast<std::uint16_t>(r[5]));
                fpul = ftrc(fr[5]); r[5] = fpul;
                r[4] -= r[3]; ram.s16(r[0] + 112, static_cast<std::uint16_t>(r[4]));
                fpul = ftrc(fr[6]); r[4] = fpul;
                r[5] -= r[3]; ram.s16(r[0] + 80, static_cast<std::uint16_t>(r[5]));
                fpul = ftrc(fr[7]); r[5] = fpul;
                r[4] -= r[3]; ram.s16(r[0] + 48, static_cast<std::uint16_t>(r[4]));
                --r[2]; t = r[2] == 0;
                r[5] -= r[3]; ram.s16(r[0] + 16, static_cast<std::uint16_t>(r[5]));
                r[0] += 2;
                cyc += 62;
                r[3] = ram.l32(r[7] + 48);
            } while (!t);
            r[1] -= 16;
            r[5] = ram.l32(r[7] + 4);
            r[3] -= 128;
            r[1] -= 128;
            cyc += 4;
        }
        r[4] = ram.l32(r[7] + 44);
        r[3] += r[5];
        --r[4]; t = r[4] == 0;
        ram.s32(r[7] + 44, r[4]);
        cyc += 6;
        r[1] += r[5];
        if (t)
            break;
    }

    // Epilogue (0x0ceb17d2): restore r8..r14 and return.
    sp += 4;
    for (int k = 8; k <= 13; ++k) { r[k] = ram.l32(sp); sp += 4; }
    cyc += 10;
    const std::uint32_t rts_target = c.pr;
    r[14] = ram.l32(sp); sp += 4;
    r[15] = sp;

    std::memcpy(c.r, r, sizeof r);
    std::memcpy(c.fr, fr, sizeof fr);
    std::memcpy(c.xf, xf, sizeof xf);
    c.fpul = fpul;
    c.t = t;
    c.cycles = cyc;
    if (rts_target != entry_pr)
        dream::sh4::nonlocal_return(c, m, rts_target);
    return true;
}

}  // namespace
}  // namespace sanx
