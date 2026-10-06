// ===========================================================================
//  lit_ps.hlsl
// ---------------------------------------------------------------------------
//  Pixel stage: Lambertian diffuse plus an ambient floor.
//
//  WHY LAMBERT AND NOT HALF-LAMBERT
//  ---------------------------------
//  Lambert is dot(N, L) clamped at 0, which means the terminator (where the
//  surface turns away from the light) has infinite slope and reads as a hard
//  edge on a low-poly sphere. Half-Lambert - dot(N,L) * 0.5 + 0.5 - softens it
//  and costs one multiply and one add. It is not used here because the
//  triangle needs no terminator at all; the formula is written in its textbook
//  form so the later lit mesh has one line to change.
//
//  WHY THE AMBIENT TERM EXISTS AT ALL
//  ---------------------------------
//  Pure Lambert renders the unlit side of an object as absolute black, which
//  reads as a hole in the scene rather than as shadow. An ambient floor is the
//  cheapest stand-in for bounced light and for the fact that no real lighting
//  environment is truly zero. It is a constant, so it costs one register and no
//  texture fetches.
// ===========================================================================

#include "common.hlsli"

cbuffer LightCB : register(b2)
{
    float3 direction;
    float3 color;
    float  ambient;
};

float4 main(VSOutput input) : SV_Target
{
    // LightCB::direction points FROM the light, so the surface-to-light vector
    // is its negation. Getting this backwards produces a scene lit from the
    // opposite side - a bug that looks like a plausible artistic choice until
    // the shadow and the sun disagree.
    float3 toLight = normalize(-direction);
    float3 normal  = normalize(input.normal);

    // max() guards the back side: the dot product is negative there, and a
    // negative diffuse term would subtract light.
    const float ndotl = max(dot(normal, toLight), 0.0F);

    // Base colour x light colour x lambert term. Multiplying the light colour
    // in is what makes a dim red sun read as a dim red sun rather than as a
    // dim white one.
    const float3 diffuse = input.color.rgb * color.rgb * ndotl;

    // The ambient floor is deliberately NOT scaled by ndotl: it stands in for
    // light that has bounced off surfaces the directional light cannot see, so
    // it exists precisely where the diffuse term is zero.
    //
    // The local is named ambientTerm, not ambient: `ambient` is the cbuffer
    // member, and a local of the same name shadows it - which compiles as
    // "used before initialization" under strict warnings rather than as the
    // self-reference it actually is. A silent mis-read here would light the
    // scene from garbage.
    const float3 ambientTerm = input.color.rgb * color.rgb * ambient;

    // Alpha passes through untouched. The back buffer's alpha mode is IGNORE so
    // nothing composites today; carrying it costs nothing and keeps the shader
    // correct the day the alpha mode changes.
    return float4(diffuse + ambientTerm, input.color.a);
}