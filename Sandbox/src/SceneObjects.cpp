// ===========================================================================
//  SceneObjects.cpp
// ===========================================================================

#include "SceneObjects.h"

#include "FrameConstants.h"

#include <Core/Log.h>
#include <Renderer/Primitives.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace Nova::Sandbox
{
namespace
{
/// Where a model file is looked for.
///
/// The Sandbox has no content directory and no asset registry yet, so the path
/// is relative to the working directory - the executable's folder under F5, the
/// repository root from a command line. Naming it here rather than inlining it
/// at the call site means the asset milestone replaces this one line with a
/// manifest lookup.
constexpr const char* kDefaultModelPath = "Assets/cube.obj";
} // namespace

bool DemoScene::Initialize(ID3D12Device* device, const std::string& modelPath)
{
    if (device == nullptr)
    {
        // Unlike a missing asset, this is a programmer error and throws. There
        // is no sensible fallback for "no device", and the alternative is a
        // scene that silently never draws.
        throw std::invalid_argument("DemoScene::Initialize requires a device");
    }

    // ---- the ground plane ------------------------------------------------
    //
    // 40 units across so it fills the lower half of the frame from the camera's
    // starting position and reads as a floor rather than as a floating tile.
    // One subdivision because the plane is flat and unlit: more would add
    // vertices that change nothing visible, which is the cheapest possible way
    // to make a later lighting pass slower.
    if (!ground_.CreateFromData(device, Renderer::GeneratePlane(40.0F, 40.0F, 1)))
    {
        NOVA_ERROR("DemoScene: the ground plane could not be created");
        return false;
    }

    // ---- the model, or a cube --------------------------------------------
    //
    // The file is tried first and the cube is the fallback, not the other way
    // around. A pipeline that starts hard-coded and treats loading as an
    // optimisation tends never to get loading tested, because the hard-coded
    // path is the one that always runs.
    if (!model_.LoadFromFile(device, modelPath))
    {
        NOVA_CLIENT_INFO("DemoScene: no model at '{}', using a procedural cube", modelPath);

        if (!model_.CreateFromData(device, Renderer::GenerateCube(1.0F)))
        {
            NOVA_ERROR("DemoScene: the fallback cube could not be created either");
            return false;
        }
    }

    // Resting on the plane rather than at the origin, so the first frame shows
    // contact instead of a cube bisected by the floor.
    model_.GetTransform().position = Math::Vec3(0.0F, 1.0F, 0.0F);

    NOVA_CLIENT_INFO("DemoScene ready: model has {} mesh(es)", model_.GetMeshCount());
    return true;
}

void DemoScene::Update(Renderer::Material& material, std::uint32_t frameSlot,
                       float elapsedSeconds)
{
    // Accumulate from the elapsed time the caller passes rather than from a frame
    // delta computed here: the render loop already has an authoritative clock,
    // and two clocks disagreeing is how an animation ends up stepping by one
    // value while the camera moves by another.
    spinAngle_ = elapsedSeconds * 0.6F;

    model_.GetTransform().rotation = Math::Vec3(0.0F, spinAngle_, 0.0F);
}

void DemoScene::Draw(ID3D12GraphicsCommandList* commandList, Renderer::Material& material,
                     std::uint32_t frameSlot) noexcept
{
    if (commandList == nullptr)
    {
        return;
    }

    // b1 holds ONE transform, so every draw with a different matrix needs its
    // own upload immediately before it. That is the whole reason the object
    // constant is per-draw state rather than per-frame state, and it is the
    // limitation the scene graph will remove by giving each draw an index into
    // a ring of transforms.

    // The model.
    WriteObjectConstants(material, model_.GetModelMatrix(), frameSlot);
    model_.Draw(commandList);

    // The ground, identity transform.
    WriteObjectConstants(material, ground_.GetModelMatrix(), frameSlot);
    ground_.Draw(commandList);
}

} // namespace Nova::Sandbox