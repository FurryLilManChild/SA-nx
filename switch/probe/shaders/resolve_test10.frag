// SA-nx resolve probe: the resolve shader exactly as switch-test10 shipped it (from git).
// Translucent resolve pass (SA-nx): sort each pixel's fragments by depth and blend them over the
// opaque image, back to front, with the hardware's blend instructions.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "oit_common.glsl"

layout(location = 0) out vec4 o_colour;

// Fragments kept per pixel. Past this many the farthest are dropped, which costs the least.
// SA-nx: a specialisation constant, so the renderer can also build a lean resolve that keeps 8
// (the port menu's "accurate (lean)"): the arrays below live in per-thread memory, and on the
// Switch's GPU 32 of each made a screen with many stacked layers take hundreds of milliseconds.
layout(constant_id = 0) const int kMax = 32;

// The hardware's eight blend factors. 2 and 3 are "the other colour": the destination for the
// source factor, the source for the destination factor.
vec4 factor(uint code, vec4 src, vec4 dst, bool is_src) {
    const vec4 other = is_src ? dst : src;
    switch (code) {
        case 0u: return vec4(0.0);
        case 1u: return vec4(1.0);
        case 2u: return other;
        case 3u: return vec4(1.0) - other;
        case 4u: return vec4(src.a);
        case 5u: return vec4(1.0 - src.a);
        case 6u: return vec4(dst.a);
        default: return vec4(1.0 - dst.a);
    }
}

// Drawn first is the farther (smaller 1/w); at equal depth the earlier submitted.
bool before(float da, uint oa, float db, uint ob) {
    return da < db || (da == db && oa < ob);
}

void main() {
    const ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 dst = texelFetch(opaque_colour, p, 0);

    uint idx[kMax];
    float dep[kMax];
    uint ord[kMax];
    int n = 0;
    uint i = heads[uint(p.y) * push.width + uint(p.x)];
    // Bounded, so a damaged list cannot hang the GPU.
    int guard = 0;
    for (; i != kEnd && guard < 4096; ++guard) {
        const float d = nodes[i].depth;
        const uint o = nodes[i].info >> 6;
        if (n < kMax) {
            idx[n] = i;
            dep[n] = d;
            ord[n] = o;
            ++n;
        } else {
            // Full: replace the farthest kept fragment if this one is nearer.
            int far = 0;
            for (int k = 1; k < kMax; ++k)
                if (before(dep[k], ord[k], dep[far], ord[far]))
                    far = k;
            if (before(dep[far], ord[far], d, o)) {
                idx[far] = i;
                dep[far] = d;
                ord[far] = o;
            }
        }
        i = nodes[i].next;
    }
    if (guard > 64)
        atomicAdd(counter.long_lists, 1u);
    if (i != kEnd)
        atomicAdd(counter.unterminated, 1u);
    // Insertion sort, farthest first.
    for (int a = 1; a < n; ++a) {
        const uint ti = idx[a];
        const float td = dep[a];
        const uint to = ord[a];
        int b = a - 1;
        while (b >= 0 && before(td, to, dep[b], ord[b])) {
            idx[b + 1] = idx[b];
            dep[b + 1] = dep[b];
            ord[b + 1] = ord[b];
            --b;
        }
        idx[b + 1] = ti;
        dep[b + 1] = td;
        ord[b + 1] = to;
    }
    for (int k = 0; k < n; ++k) {
        const vec4 src = unpackUnorm4x8(nodes[idx[k]].colour);
        const uint info = nodes[idx[k]].info;
        dst = clamp(src * factor(info & 7u, src, dst, true) +
                        dst * factor((info >> 3) & 7u, src, dst, false),
                    0.0, 1.0);
    }
    o_colour = dst;
}
