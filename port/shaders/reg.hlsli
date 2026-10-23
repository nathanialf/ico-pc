// reg.hlsli: the register declaration every ICO shader uses.
//
// ICO_REG(r, s) is register(r, space s): the register space is the RHI bind
// group and the register number the RHI slot (common.hlsli's binding
// scheme).  The Direct3D 11 backend compiles the same sources with FXC
// (shader model 5.0), which has no register spaces: under ICO_D3D11 the
// macro drops the space.  The register numbers are distinct across the
// groups (b0; b1, b2, b3, t0, t4; t1, t2, s1; u0), so a flat namespace has
// no collision.
#ifndef ICO_REG_HLSLI
#define ICO_REG_HLSLI

#ifdef ICO_D3D11
#define ICO_REG(r, s) register(r)
#else
#define ICO_REG_SPACE(s) space##s
#define ICO_REG(r, s) register(r, ICO_REG_SPACE(s))
#endif

#endif
