// ===========================================================================
//  D3D12Helpers.cpp
// ===========================================================================

#include <Renderer/D3D12Helpers.h>

#include <Core/Platform.h>

#include <string>

namespace Nova::Renderer
{
void ThrowIfFailed(HRESULT result, const char* file, int line)
{
    const std::uint32_t code = static_cast<std::uint32_t>(result);

    // snprintf rather than fmt: this is a header helper that may be reached
    // from anywhere in the module, and pulling a formatting library into it for
    // one call would tax every translation unit that includes it. Two
    // formatting sites are not worth a dependency.
    //
    // 11 characters is exactly "0x" + 8 hex digits + the terminator. HRESULT is
    // 32 bits, so eight digits is the complete value - not a truncation of it.
    char codeText[11] = {};
    std::snprintf(codeText, sizeof(codeText), "0x%08X", code);

    // The system message plus the code is deliberately redundant. The code is
    // what has to be greppable in the SDK headers; the text is what tells a
    // reader which of a dozen device-creation failures they are looking at.
    // Platform::GetLastErrorMessage documents that it accepts a HRESULT cast to
    // its parameter type, and returns the code itself rather than an empty
    // string when the system has no message for it - so this never degrades to
    // a blank.
    throw std::runtime_error(std::string("D3D12 call failed: ") + codeText + " (" +
                             Platform::GetLastErrorMessage(code) + ") at " +
                             SourceBasename(file) + ":" + std::to_string(line));
}

} // namespace Nova::Renderer