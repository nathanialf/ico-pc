// vu_velocity.hlsl: the velocity pass's shaders (docs/port/RENDER.md,
// "Velocity"; rd_replay_vu.c vuVelPass, rd_velocity.h).
//   vu_velocity_main_vs  a mesh or grid draw of list 0 again, from the record:
//                        vu_main_vs's position (the same triangle, the same
//                        depth: the pass tests SCENE's depth EQUAL), and the
//                        corner's motion in NDC from its two records, the
//                        homogeneous position h of this picture (record
//                        plane 0 at vu_rec.x + v) and of the picture before
//                        (the second record, vu_mark.w texels on: the pre-pass
//                        drew the vertex again through the draw's VuPrevCB)
//   velocity_ps          the motion in pixels of the target, "current to
//                        previous": where the pixel's surface was in the
//                        picture before, less where it is, +x right, +y down
// Both positions go through vu_homogeneous_position with this draw's FrameCB,
// unsnapped (h is the position before the divide and the GS's 1/16 grid),
// and the frame group's offsets (the exact viewport's half texel, a
// jitter's sub-pixel offset) are the same in both: they cancel in the
// difference, which is the unjittered motion an upscaler takes.  The
// motion is interpolated linearly in screen space (noperspective), as the
// GS interpolates the surface's attributes: a pixel's motion is that of
// the same screen-space blend of the corners.  A corner behind the eye in
// either picture gives the triangle no motion (z 0: the pixel writes 0).
#include "vu_tri.hlsl"

struct VuVelOut {
    float4 pos : SV_Position;
    // xy: the corner's NDC position in the picture before less in this one;
    // z: 1 when both are in front of the eye, else 0; w: the same for NDC z
    // (unused by velocity_ps)
    VK_LOC(0) noperspective float4 vel : TEXCOORD0;
};

VuVelOut vu_velocity_main_vs(uint vid : SV_VertexID)
{
    VuVSOut m = vu_main_vs(vid);
    uint v = (vid >> 2) - 2u + (vid & 3u);
    uint t = vu_rec.x + v;
    float4 c = vu_homogeneous_position(asfloat(vu_rec_buf[t]));
    float4 p = vu_homogeneous_position(asfloat(vu_rec_buf[t + vu_mark.w]));
    float ok = c.w > 0.0 && p.w > 0.0 ? 1.0 : 0.0;
    float cw = c.w > 0.0 ? c.w : 1.0;
    float pw = p.w > 0.0 ? p.w : 1.0;
    VuVelOut o;
    o.pos = m.pos;
    o.vel.x = ok * (p.x / pw - c.x / cw);
    o.vel.y = ok * (p.y / pw - c.y / cw);
    o.vel.z = ok;
    o.vel.w = ok * (p.z / pw - c.z / cw); // the depth's change (every lane used: no OpUndef)
    return o;
}

// NDC to pixels of the target: the exact viewport's 2 / its texels (g_vp),
// else the target's half texels (its GS size times its texels per GS
// pixel); NDC y is up, the pixels' y down
float4 velocity_ps(VuVelOut i) : SV_Target0
{
    float sx = g_vp.x != 0.0 ? 1.0 / g_vp.x : 0.5 * g_target.x * g_z.y;
    float sy = g_vp.y != 0.0 ? 1.0 / g_vp.y : 0.5 * g_target.y * g_z.z;
    float k = i.vel.z >= 0.999 ? 1.0 : 0.0;
    return float4(k * i.vel.x * sx, -k * i.vel.y * sy, 0.0, 1.0);
}
