// ===========================================================================
//  FrameConstants.cpp
// ---------------------------------------------------------------------------
//  The CPU half of the shader contract. See FrameConstants.h for why the
//  padding fields in those structs are load-bearing.
// ===========================================================================

#include "FrameConstants.h"

#include <Math/Math.h>
#include <Renderer/D3D12Context.h>

#include <cstring>

namespace Nova::Sandbox
{
void CreateFrameConstants(Renderer::Material& material)
{
    // Three slots, one per root parameter, matching D3D12Context's signature.
    // Created explicitly rather than on first use so each buffer's size comes
    // from its own struct.
    material.EnsureConstantBuffer(0, sizeof(CameraConstantData));   // b0
    material.EnsureConstantBuffer(1, sizeof(ObjectConstantData));   // b1
    material.EnsureConstantBuffer(2, sizeof(LightConstantData));    // b2
}

void UpdateFrameConstants(Renderer::Material& material,
                          const Scene::Camera& camera,
                          std::uint32_t    frameSlot,
                          float            spinAngle)
{
    // ---- b0: the camera ------------------------------------------------
    //
    // viewProjection is ONE matrix, not view and projection separately. It is
    // combined on the CPU because that is where the combine is free: the shader
    // would otherwise need two root parameters and a multiply per vertex, and
    // the result is identical because matrix multiplication is associative.
    CameraConstantData cameraConstants{};
    const Math::Mat4 viewProjection = camera.GetViewProjectionMatrix();
    std::memcpy(cameraConstants.viewProjection, viewProjection.m,
                sizeof(cameraConstants.viewProjection));
    cameraConstants.cameraPosition[0] = camera.position.x;
    cameraConstants.cameraPosition[1] = camera.position.y;
    cameraConstants.cameraPosition[2] = camera.position.z;
    cameraConstants.padding            = 0.0F;
    material.SetConstantBuffer(0, cameraConstants, frameSlot);

    // ---- b1: the object ------------------------------------------------
    //
    // A yaw-only rotation, so the triangle turns about its own vertical axis
    // and shows a different facet to the light as it goes. Driven off the frame
    // delta rather than wall-clock time so the spin rate is identical at 30 FPS
    // and at 300 - the same reasoning the FPS controller uses for movement.

    ObjectConstantData objectConstants{};
    const Math::Mat4 model = Math::Mat4::Rotate(spinAngle, Math::Vec3::Up);
    std::memcpy(objectConstants.model, model.m, sizeof(objectConstants.model));
    material.SetConstantBuffer(1, objectConstants, frameSlot);

    // ---- b2: the light -------------------------------------------------
    //
    // Direction points FROM the light, so a light above and beside the scene
    // travels down and inward. Normalised here as well as in the shader: the
    // shader normalises too, because a shader that cannot trust its inputs is a
    // shader every caller has to be careful with.
    LightConstantData lightConstants{};
    lightConstants.direction[0] =  0.35F;
    lightConstants.direction[1] = -0.90F;
    lightConstants.direction[2] =  0.25F;
    lightConstants.color[0]    =  1.00F;
    lightConstants.color[1]    =  0.97F;
    lightConstants.color[2]    =  0.90F;

    // 0.22, not 0.10. The back buffer is UNORM with no HDR range, so the
    // brightest thing this scene can produce is colour x light colour x 1.0.
    // An ambient below about 0.2 turns every surface facing away from the light
    // into a hole rather than a shadow, and the demo then reads as a broken
    // renderer instead of a dark one. Real games solve this with tone mapping
    // over an HDR target; the tone-mapping milestone is where this number stops
    // mattering.
    lightConstants.ambient     =  0.22F;
    material.SetConstantBuffer(2, lightConstants, frameSlot);
}

} // namespace Nova::Sandbox