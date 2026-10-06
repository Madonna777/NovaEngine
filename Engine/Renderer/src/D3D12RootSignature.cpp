// ===========================================================================
//  D3D12RootSignature.cpp
// ---------------------------------------------------------------------------
//  The root signature: the contract between a command list and what a shader is
//  allowed to read.
//
//  WHAT A ROOT SIGNATURE ACTUALLY IS
//  --------------------------------
//  A shader sees variables at abstract addresses: b0, b1, t0, space 0, space 1.
//  A root signature is the translation from those names to something the
//  hardware can fetch. It is a table of root parameters, each saying:
//
//      "register b1 in space 0 of the VERTEX stage is a CONSTANT BUFFER, and
//       its address is set with SetGraphicsRootConstantBuffer"
//
//  Three shapes of root parameter exist, and the choice is the whole design:
//
//      ROOT_DESCRIPTOR       a raw address. One line of code to bind. No heap.
//      DESCRIPTOR_TABLE      an array of descriptors living in a heap. For
//                            bindless rendering, or for arrays indexed by a
//                            shader. Needs a heap and an allocation strategy.
//      ROOT_CONSTANTS        inline 32-bit values, for a handful of scalars.
//
//  Three fixed buffers want root descriptors. A table would mean three heaps,
//  three allocations, and three extra calls per frame, to do what a 64-bit
//  address already does.
//
//  WHY THIS AND NOT ONE SIGNATURE PER SHADER
//  ------------------------------------------
//  The original triangle pipeline built its own single-CBV-table signature,
//  because that is what the first D3D12 samples do and there was exactly one
//  buffer to bind. Now that the renderer owns the register layout, a pipeline
//  built against a DIFFERENT signature fails at draw time with a "root
//  signature mismatch" that names neither of the two candidates. The pipeline
//  takes this signature instead, and a shader swap becomes a bytecode swap.
//
//  WHY ROOT_DESCRIPTOR AND NOT A DESCRIPTOR TABLE
//  ----------------------------------------------
//  A table would mean three descriptor heaps, three allocations, and three
//  SetGraphicsRootDescriptorTable calls per frame, to do what one 64-bit
//  address already does. Tables are the right answer for bindless rendering
//  where a shader indexes an array of resources - that arrives with the texture
//  milestone and will live in the SRV slot, not here.
//
//  THE FLAG THAT DECIDES WHETHER THE INPUT LAYOUT IS LEGAL
//  -------------------------------------------------------
//  ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT is what lets the vertex buffer's
//  attribute descriptions be used at all. Without it the input layout is
//  accepted by the PSO and then ignored, and the vertex shader reads an
//  undefined attribute stream - usually all zeroes.
// ===========================================================================

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

namespace Nova::Renderer
{
void D3D12Context::CreateRootSignature()
{
    // One root parameter per constant buffer, in register order. The array is
    // indexed by register index below, so the order here IS the binding.
    D3D12_ROOT_PARAMETER parameters[3]{};

    for (UINT index = 0; index < 3; ++index)
    {
        // ROOT_DESCRIPTOR rather than DESCRIPTOR_TABLE: the GPU address is
        // supplied directly, so no descriptor heap participates at all.
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

        parameters[index].Descriptor.ShaderRegister = index;   // b0, b1, b2
        parameters[index].Descriptor.RegisterSpace  = 0;

        // ALL stages rather than per-stage visibility. A narrower setting
        // (vertex-only, say) is slightly cheaper on the hardware that implements
        // it, and silently breaks the moment a compute pass reads the same
        // camera buffer. One visibility for a shared signature is the honest
        // default when more than one stage can read it.
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters     = 3;
    description.pParameters       = parameters;
    description.NumStaticSamplers = 0;
    description.pStaticSamplers   = nullptr;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialise to a blob, then create. The blob is the portable form: a root
    // signature is a compiled artefact, and CreateRootSignature takes its bytes
    // rather than the struct, so the same signature can come from a file or from
    // the struct with identical behaviour.
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    NOVA_THROW_IF_FAILED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
                                                     serialized.GetAddressOf(),
                                                     errors.GetAddressOf()));

    // Node index 0: a single adapter, so one node. Multi-node is for linked
    // adapters, which are one board - not two GPUs on one machine.
    NOVA_THROW_IF_FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(),
                                                     serialized->GetBufferSize(),
                                                     IID_PPV_ARGS(&rootSignature_)));

    NOVA_INFO("Root signature: b0=camera b1=object b2=light (root descriptors, no heap)");
}
} // namespace Nova::Renderer