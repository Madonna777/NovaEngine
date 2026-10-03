// ===========================================================================
//  Editor - entry point (placeholder)
// ---------------------------------------------------------------------------
//  Deliberately does almost nothing yet.
//
//  WHY NOT STUB IMGUI IN NOW: ImGui's core is backend-agnostic, but its D3D12
//  backend needs a device, a swap chain and a command queue to submit into.
//  Writing ImGui code before the Renderer exists produces code that cannot be
//  compiled or run - the worst of both worlds. The order is: window and device
//  first, then ImGui initialised against them, then panels.
// ===========================================================================

#include <Core/Assert.h>
#include <Core/Log.h>

#include <cstdio>

int main()
{
    Nova::Platform::EnableUtf8Console();

    // The Editor will log to its own file next to the executable, but until
    // there is anything to diagnose, console output only is less clutter.
    Nova::Log::Initialize(Nova::LogLevel::Info,
                          /*logFile=*/{},
                          /*consoleOutput=*/true,
                          /*flushOnError=*/true);

    NOVA_CLIENT_INFO("NovaEngine Editor - not yet implemented");
    NOVA_CLIENT_INFO("Next: Renderer module (device, swap chain, GLFW window), then ImGui.");

    Nova::Log::Shutdown();
    return 0;
}