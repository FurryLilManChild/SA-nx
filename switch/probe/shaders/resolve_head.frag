// SA-nx resolve probe: only each pixel's first fragment. The floor: one list read per pixel.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "oit_common.glsl"

layout(location = 0) out vec4 o_colour;

void main() {
    const ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 dst = texelFetch(opaque_colour, p, 0);
    const uint i = heads[uint(p.y) * push.width + uint(p.x)];
    if (i != kEnd) {
        const vec4 src = unpackUnorm4x8(nodes[i].colour);
        dst = clamp(src * src.a + dst * (1.0 - src.a), 0.0, 1.0);
    }
    o_colour = dst;
}
