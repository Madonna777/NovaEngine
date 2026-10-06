// ===========================================================================
//  FrameConstants.h
// ---------------------------------------------------------------------------
//  The CPU-side mirror of Shaders/common.hlsli, plus the per-frame write of it
//  into the renderer's constant buffers.
//
//  WHY THIS IS NOT INSIDE main.cpp
//  ------------------------------
//  Two reasons, and the second is the important one. The obvious one is file
//  size. The real one is that this is the only place in the engine where a
//  human being has to keep a byte-for-byte agreement with HLSL, and burying it
//  in the application's startup code is how that agreement gets broken by
//  someone reorganising main() six months from now. Giving it a file whose name
//  is the subject means "the CPU struct that must match the shader" is what a
//  reader sees when they grep for it.
// ===========================================================================
#pragma once

#include <Math/Mat4.h>
#include <Renderer/Material.h>
#include <Scene/Camera.h>

#include <cstdint>

namespace Nova::Sandbox
{
// ===========================================================================
//  CPU mirrors of the HLSL constant buffers.
//
//  These are why "use the same struct on both sides" does not work. HLSL lays a
//  constant buffer out on 16-byte registers - a float3 is padded to 16 even
//  when another float3 follows it - while the CPU lays it out with whatever the
//  compiler feels like. The two agree only when the padding is written down.
//  Delete a pad field and the shader reads the NEXT buffer's first four bytes,
//  silently, forever.
//
//  The static_asserts are the mechanism rather than decoration. HLSL's layout is
//  a property of the shader compiler, not of this file, so the only way to know
//  the two agree is to check the sizes at compile time rather than to inspect a
//  screenshot three milestones from now.
// ===========================================================================

/// Matches CameraCB: a 64-byte matrix, then a 16-byte position and pad.
/// The float3 lands at offset 64, which is already register-aligned, so the
/// trailing float is not padding by accident - it is what puts `cameraPos` in
/// the low three lanes of one register instead of straddling two.
struct CameraConstantData
{
    float viewProjection[16];
    float cameraPosition[3];
    float padding;
};

/// Matches ObjectCB: a single 64-byte matrix. One per drawable object; every
/// vertex the object draws is transformed by the same matrix, so it belongs
/// outside the vertex buffer and inside a per-draw constant.
struct ObjectConstantData
{
    float model[16];
};

/// Matches LightCB: 32 bytes, and every byte of the padding is load-bearing.
///
/// THE 4-BYTE GAP AFTER `direction` IS NOT OPTIONAL. HLSL aligns every float3
/// to a 16-byte boundary even when another float3 follows, so the shader reads
/// `color` from offset 16. A C++ mirror that packs the two float3s back to back
/// puts `color` at offset 12, and the shader then reads four bytes of the
/// alignment gap as its red channel - which is padding, therefore zero,
/// therefore an unlit black object rather than an error. Nothing reports it: the
/// slot is a valid 256-byte constant buffer, the PSO is valid, the draw
/// executes, and only the image is wrong.
///
/// `ambient` stays a plain float because after the padded `color` a scalar
/// lands at offset 28, which is exactly where the shader expects it.
struct LightConstantData
{
    float direction[3];
    float padding0;  ///< 16-byte alignment gap before `color`, never written
    float color[3];
    float ambient;
};

static_assert(sizeof(CameraConstantData) == 80, "CameraCB layout must match common.hlsli");
static_assert(sizeof(ObjectConstantData) == 64, "ObjectCB layout must match common.hlsli");
static_assert(sizeof(LightConstantData) == 32, "LightCB layout must match common.hlsli");

/// Creates the three constant buffers on @p material, sized from the structs
/// above.
///
/// Called once at startup. The sizes come from the structs rather than from
/// whatever is written first, so a buffer cannot end up sized by accident.
///
/// @param material Material to own the buffers. Must already have SetDevice.
void CreateFrameConstants(Renderer::Material& material);

/// Writes the camera constants into b0, and the light into b2.
///
/// @param material  Target material, already populated by CreateFrameConstants.
/// @param camera    Camera supplying b0 for this frame.
/// @param frameSlot Slot being written, 0 to frames-in-flight minus one.
///
/// @note b1 is NOT written here. The object transform is per-draw state because
///       one slot holds one matrix, so it is written by WriteObjectConstants
///       immediately before each draw instead.
void UpdateFrameConstants(Renderer::Material& material,
                          const Scene::Camera& camera,
                          std::uint32_t    frameSlot);

/// Writes one object's matrix into b1.
///
/// Separate from UpdateFrameConstants because b1 holds a SINGLE transform.
/// Two objects in one frame therefore need two calls, each immediately before
/// the draw that consumes it - which is why this is its own function rather
/// than a parameter of the per-frame write.
///
/// @param material  Material owning the b1 buffer.
/// @param model     Object-to-world matrix for the object about to be drawn.
/// @param frameSlot Constant buffer slot for this frame.
void WriteObjectConstants(Renderer::Material& material, const Math::Mat4& model,
                          std::uint32_t frameSlot);

/// Writes the directional light into b2.
///
/// Split out for the same reason as WriteObjectConstants: the light is not
/// frame-dependent, and naming it separately makes it visible that it could be
/// written once at startup instead of once per frame.
void WriteLightConstants(Renderer::Material& material, std::uint32_t frameSlot);

} // namespace Nova::Sandbox