// ===========================================================================
//  triangle_ps.hlsl
// ---------------------------------------------------------------------------
//  Minimal pixel (fragment) shader: returns the interpolated colour unchanged.
// ===========================================================================

struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

// SV_Target names the render target slot this shader writes into. RTVFormats[0]
// of the PSO - the back buffer, R8G8B8A8_UNORM - is what it writes through.
// No blending is enabled in this pipeline, so the value here OVERWRITES the
// destination pixel: that is why this triangle has hard edges and will not
// anti-alias at the rasteriser stage.
float4 main(PSInput input) : SV_Target
{
    return input.color;
}
