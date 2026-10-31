// SA-nx resolve probe: the list walked and blended in list order, no sort and no array. Not a
// correct picture; it measures the cost of walking the lists alone.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "oit_common.glsl"

layout(location = 0) out vec4 o_colour;

void main() {
    const ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 dst = texelFetch(opaque_colour, p, 0);
    uint i = heads[uint(p.y) * push.width + uint(p.x)];
    for (uint g = 0u; i != kEnd && g < 4096u; ++g) {
        const vec4 src = unpackUnorm4x8(nodes[i].colour);
        dst = clamp(src * src.a + dst * (1.0 - src.a), 0.0, 1.0);
        i = nodes[i].next;
    }
    o_colour = dst;
}
