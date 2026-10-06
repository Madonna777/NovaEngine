// ===========================================================================
//  triangle_vs.hlsl
// ---------------------------------------------------------------------------
//  Minimal vertex shader. Passes the vertex through in clip space and forwards
//  the colour untouched so the pixel shader can interpolate it across the
//  triangle.
//
//  HLSL SEMANTICS, WHY THEY ARE NAMED THIS WAY
//  -------------------------------------------
//  "POSITION" is a USER-DEFINED semantic - the engine attaches that name to a
//  slot in its vertex buffer layout, and matching input/output names is how the
//  two ends of the fixed-function stage agree on what is what. "SV_Position" is
//  SYSTEM-DEFINED: the rasteriser reads the value there to decide which pixels
//  the triangle covers. The two live in different namespaces; conflating them is
//  the most common cause of "triangle invisible" with a perfect-looking shader.
// ===========================================================================

// One attribute row the D3D12 fixed-function input assembler feeds to main().
// The layout here matches Vertex in Engine/Renderer/Mesh.h - order and
// offsets must agree, because this shader reads raw attribute streams, not
// C++ structs.
struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

// What the vertex shader hands to the rasteriser, and what it forwards to the
// pixel shader. SV_Position MUST be float4: clip space is (x, y, z, w), and
// the GPU divides by w so nothing reaches the framebuffer (or the depth test)
// that has not been through perspective division.
struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

PSInput main(VSInput input)
{
    PSInput output;

    // No MVP transform yet - the geometry is authored directly in clip space.
    // D3D12's clip convention is left-handed, so +X is right, +Y is up, and
    // z runs 0 (near) to 1 (far). w=1 disables perspective division, which is
    // what an un-transformed vertex needs.
    output.position = float4(input.position, 1.0f);

    // Colour is interpolation input, not output - the rasteriser blends it
    // across the triangle's pixels using the same barycentric weights as the
    // position, which is why the gradient looks smooth: the hardware is doing
    // the interpolation, the shader is just the endpoint.
    output.color = input.color;

    return output;
}
