// Depth of field, pass one: the frame plus its signed circle of confusion.
// SOURCE OF RECORD for fx_dof_prepare.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/features/fx_dof_prepare.h /Vn k_fxDofPrepareBytes src/features/fx_dof_prepare.hlsl
//
// One fetch of the frame, one of the depth; out comes the frame again with the SIGNED
// circle of confusion in alpha: 0.5 sharp, below that in front of the focal plane, above it
// behind. Signed because the gather still has to know which taps are nearer than the pixel
// they are reaching, and the sign of the coc is monotonic in depth wherever it is not
// saturated. It costs two fetches per pixel once and saves one fetch per TAP in the gather.
//
//   c0 = zNear, zFar, focal distance, 1/blur ramp
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c2 = viewmodel cutoff (RAW depth, 0.1), near blur on, -, -
//   c3 = -, -, -, 1/near ramp

sampler2D sFrame : register(s0);
sampler2D sDepth : register(s1);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 c = tex2Dlod(sFrame, float4(uv, 0, 0)).rgb;

    // The RAW sample is kept as well as the unprojected one: the viewmodel is told apart by
    // the depth range it was drawn into, and that is a fact about the raw value which
    // unprojecting destroys.
    float raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float z   = linear_z(raw);

    float f = saturate((z - c0.z - c1.x) * c0.w);

    // The near side gets its OWN ramp (c3.w), because it only has focus minus half the sharp
    // band of room to use: share the far ramp and a focus of 400 units can never blur
    // anything in front of it by more than a third, which reads as the near blur being
    // broken whenever the focus is close.
    float n = saturate((c0.z - c1.x - z) * c3.w) * c2.y;

    // THE VIEWMODEL, AND ONLY THE VIEWMODEL. It is drawn through DepthRange(0, 0.1)
    // (viewrender.cpp:1109), so every pixel of it is a raw depth below 0.1 and no world
    // pixel past 7.8 units ever is -- a test in world units throws the near 24 of them away,
    // wall, railing and crate with it.
    float k = (raw < c2.x) ? 0.0 : max(f, n);
    float s = (n > f) ? -1.0 : 1.0;

    return float4(c, k * s * 0.5 + 0.5);
}
