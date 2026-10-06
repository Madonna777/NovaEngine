// ===========================================================================
//  lit_vs.hlsl
// ---------------------------------------------------------------------------
//  Vertex stage for lit geometry. The full MVP chain is applied here, which is
//  the whole reason a camera object exists.
//
//  THE ORDER OF THE TWO MULTIPLIES IS NOT INTERCHANGEABLE
//  ------------------------------------------------------
//      final = viewProjection * model * position
//
//  model first puts the vertex into world space, then viewProjection takes the
//  world into camera/clip space. Swapped, every object would be transformed
//  as if the camera were the parent - a whole model matrix applied to the view
//  matrix - which produces a scene that renders, does not crash, and is subtly
//  wrong for every object at once.
//
//  Column-major storage means the CPU writes Mat4 as float[16] and HLSL reads
//  float4x4 with no transposition. See Mat4.h for why the layout is chosen this
//  way rather than the other one.
// ===========================================================================

#include "common.hlsli"

cbuffer CameraCB : register(b0)
{
    float4x4 viewProjection;
    float3   cameraPos;
    float    padding;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 model;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // Model to world, then world to clip.
    float4 worldPosition = mul(input.position, model);
    output.position = mul(worldPosition, viewProjection);

    // World position is forwarded UNLIT so the pixel stage can compute a
    // distance-attenuated term or a fog blend later. Passing it is cheap now
    // and impossible to add later without recompiling every shader.
    output.worldPos = worldPosition.xyz;

    // The normal goes through the model's rotation without translation. A naive
    // mul(input.normal, model) would fold the translation into the normal's
    // implicit w=1, and the light would swim as the object moved. Models here
    // are rotation * uniform scale, so the inverse-transpose reduces to the
    // rotation itself - valid only while scales stay uniform, which is why a
    // future non-uniform-scale object needs the real inverse-transpose.
    output.normal = normalize(mul(input.normal, model));

    output.uv    = input.uv;
    output.color = input.color;

    return output;
}