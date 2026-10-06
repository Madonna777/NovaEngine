// ===========================================================================
//  SceneObjects.h
// ---------------------------------------------------------------------------
//  The Sandbox's demo scene: a spinning cube and a ground plane.
//
//  Split out of main.cpp because this is a scene description, not application
//  behaviour, and because it is the part most likely to be replaced wholesale
//  when the next milestone lands. main.cpp keeps the frame lifecycle; this file
//  keeps what is in it.
// ===========================================================================
#pragma once

#include <Math/Transform.h>
#include <Renderer/Material.h>
#include <Renderer/Model.h>

#include <cstdint>
#include <string>

namespace Nova::Sandbox
{
/// The demo scene's contents and the per-frame update that moves them.
///
/// @note Both objects are Model rather than Mesh because a Model is what a
///       loaded file produces and what the generators feed. Using Mesh for the
///       cube and Model for a real asset would give the debug cube and the
///       character different draw paths, so a bug affecting only loaded models
///       would only ever be seen with a loaded model.
class DemoScene final
{
public:
    DemoScene()  = default;
    ~DemoScene() = default;

    DemoScene(const DemoScene&)            = delete;
    DemoScene& operator=(const DemoScene&) = delete;
    DemoScene(DemoScene&&) noexcept        = default;
    DemoScene& operator=(DemoScene&&) noexcept = default;

    /// Builds the scene, preferring a model file and falling back to a cube.
    ///
    /// @param device    Device to create buffers on.
    /// @param modelPath Path to try loading. A miss is expected and produces the
    ///                  procedural cube rather than an error.
    ///
    /// @return False only if BOTH the file load and the fallback failed, which
    ///         means a broken generator rather than a missing asset.
    bool Initialize(ID3D12Device* device, const std::string& modelPath);

    /// Positions both objects and uploads the camera constants.
    ///
    /// @param material       Material whose b0/b1 receive the camera and the
    ///                       model's transform.
    /// @param frameSlot      Constant buffer slot for this frame.
    /// @param elapsedSeconds Authoritative elapsed time from the render loop.
    void Update(Renderer::Material& material, std::uint32_t frameSlot, float elapsedSeconds);

    /// Draws the model, then the ground plane.
    ///
    /// @param material Material whose b1 currently holds each object's matrix.
    ///                 Passed in because the transform is per-draw state: b1
    ///                 holds ONE matrix, so the upload that sets it has to be
    ///                 paired with the draw that consumes it.
    ///
    /// @note Model before ground. With opaque geometry the order is free; it is
    ///       fixed now so the first blended material does not have to revisit
    ///       it, and so the two orders are not mixed by accident.
    void Draw(ID3D12GraphicsCommandList* commandList, Renderer::Material& material,
              std::uint32_t frameSlot) noexcept;

    [[nodiscard]] bool IsEmpty() const noexcept { return model_.IsEmpty(); }

private:
    /// The object being demonstrated: a loaded model, or a cube.
    Renderer::Model model_;

    /// Large quad at y = 0 so the scene has a horizon to read depth against.
    /// Without it a floating cube reads as a flat sprite, because nothing in
    /// frame conveys distance.
    Renderer::Model ground_;

    /// Yaw accumulated over time, in radians.
    float spinAngle_ = 0.0F;
};

} // namespace Nova::Sandbox