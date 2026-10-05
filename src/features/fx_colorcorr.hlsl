// Color correction, ps_3_0. Built into fx_colorcorr.h:
//   fxc /nologo /T ps_3_0 /E main /Vn k_fxColorCorrBytes /Fh fx_colorcorr.h fx_colorcorr.hlsl
// Constants (set in render.cpp ColorCorr_Apply):
//   c0 rgb gain (xyz), contrast (w)       c1 saturation, temperature, exposure mult (2^EV)
//   c2..c4 hue rotation matrix rows       c5 levels in black, in white, 1/gamma
//   c6 levels out black, out range, highlights, shadows
// s1: 256x1 curves LUT, rgb = channel curve(master curve(x)), baked by CcCurve_BakeLut
sampler2D s0 : register(s0);
sampler2D s1 : register(s1);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 c = tex2D(s0, uv).rgb * c0.rgb * c1.z;          // gain + exposure
    c.r = saturate(c.r + c1.y * 0.15);                      // temperature
    c.g = saturate(c.g + c1.y * 0.05);
    c.b = saturate(c.b - c1.y * 0.15);
    float3 li = saturate((c - c5.x) / (c5.y - c5.x));       // levels in + gamma
    c = pow(li, c5.z);
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));    // saturation
    c = luma + (c - luma) * c1.x;
    float3 h;                                               // hue rotation
    h.r = dot(c, c2.xyz);
    h.g = dot(c, c3.xyz);
    h.b = dot(c, c4.xyz);
    c = saturate(h);
    // curves: sample texel centers so 8-bit value k reads LUT texel k exactly
    float3 u = c * (255.0 / 256.0) + (0.5 / 256.0);
    c.r = tex2D(s1, float2(u.r, 0.5)).r;
    c.g = tex2D(s1, float2(u.g, 0.5)).g;
    c.b = tex2D(s1, float2(u.b, 0.5)).b;
    c = saturate(c6.x + c * c6.y);                          // levels out
    luma = dot(c, float3(0.2126, 0.7152, 0.0722));          // highlights / shadows
    float hw = smoothstep(0.5, 1.0, luma);
    float sw = 1.0 - smoothstep(0.0, 0.5, luma);
    c = saturate(c + c6.z * hw + c6.w * sw);
    c = saturate((c - 0.5) * c0.w + 0.5);                   // contrast
    return float4(c, 1);
}
