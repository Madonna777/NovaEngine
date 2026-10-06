// ===========================================================================
//  common.hlsli
// ---------------------------------------------------------------------------
//  Structures shared between the vertex and pixel shaders. A .hlsli file is
//  included by several shaders and is NOT compiled on its own - it has no
//  entry point.
//
//  WHY THE CPU MIRRORS THESE EXACTLY
//  ---------------------------------
//  A constant buffer is a raw byte block. HLSL lays out a struct with std430-like
//  rules (float3 padded to 16 bytes, struct size rounded up to 16), and the CPU
//  must produce the identical byte stream or every field is silently read from
//  the wrong offset. Three rules keep that agreement mechanical:
//
//    1. float4-sized scalars only at a float3's tail. A `float3` followed by a
//       `float` costs 16 bytes on the GPU and 4 on the CPU, so the padding is
//       spelled out explicitly below rather than left to the compiler.
//    2. No bool on either side. HLSL packs bools into 4-byte registers; a C++
//       `bool` is 1 byte. There is no portable fix, so there is no bool.
//    3. Every buffer starts at a 256-byte boundary - see ConstantBuffer.h.
//
//  REGISTER ASSIGNMENT IS PART OF THE CONTRACT
//  ------------------------------------------
//  The register/binding in the shader must match the root parameter in
//  D3D12Context::CreateRootSignature exactly:
//
//      CameraCB  ->  b0  (set every frame, camera-wide)
//      ObjectCB  ->  b1  (set per draw call, object-wide)
//      LightCB   ->  b2  (set when the lighting changes)
//
//  A mismatch is not a crash and usually not an error either: it reads another
//  buffer's bytes and the scene renders with nonsense numbers. The debug layer
//  cannot see it. That is why the numbers are written down in two files.
// ===========================================================================

#ifndef COMMON_HLSLI
#define COMMON_HLSLI

// ---------------------------------------------------------------------------
//  Per-vertex attributes. POSITION/NORMAL/TEXCOORD/COLOR are user-defined
//  semantics consumed by the input assembler through D3D12_INPUT_ELEMENT_DESC;
//  the CPU-side struct with the matching order lives in VertexBuffer.h.
// ---------------------------------------------------------------------------
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    float4 color    : COLOR;
};

// ---------------------------------------------------------------------------
//  Per-pixel inputs. SV_Position is system-defined: the rasteriser fills it in
//  with screen coordinates after perspective divide, so the pixel shader can
//  read the pixel it is shading but must not write to it.
// ---------------------------------------------------------------------------
struct VSOutput
{
    float4 position  : SV_Position;
    float3 worldPos  : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float4 color     : COLOR;
};

// ---------------------------------------------------------------------------
//  CameraCB - 96 bytes of payload, padded to 256 by the buffer layout.
// ---------------------------------------------------------------------------
struct CameraCB
{
    float4x4 viewProjection;
    float3   cameraPos;
    float    padding;
};

// ---------------------------------------------------------------------------
//  ObjectCB - 64 bytes of payload. One per drawable object; every vertex the
//  object draws is transformed by the same matrix, so it belongs outside the
//  vertex buffer and inside a per-draw constant.
// ---------------------------------------------------------------------------
struct ObjectCB
{
    float4x4 model;
};

// ---------------------------------------------------------------------------
//  LightCB - one directional light. Direction is in world space and points
//  FROM the light TOWARD the scene, so a light at the sun travels (0,-1,0).
// ---------------------------------------------------------------------------
struct LightCB
{
    float3 direction;
    float3 color;
    float  ambient;
};

#endif // COMMON_HLSLI