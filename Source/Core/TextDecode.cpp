#include "TextDecode.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace wt
{
namespace
{
bool hasBom (const juce::MemoryBlock& b)
{
    const auto* p = static_cast<const juce::uint8*> (b.getData());
    return b.getSize() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF;
}
} // namespace

bool isUtf8Text (const juce::MemoryBlock& bytes)
{
    if (bytes.getSize() == 0 || hasBom (bytes))
        return true;
    return juce::CharPointer_UTF8::isValidString (static_cast<const char*> (bytes.getData()), (int) bytes.getSize());
}

juce::String decodeTextBytes (const juce::MemoryBlock& bytes, bool* wasUtf8)
{
    const auto utf8 = isUtf8Text (bytes);
    if (wasUtf8 != nullptr)
        *wasUtf8 = utf8;

    if (bytes.getSize() == 0)
        return {};

    const auto* data = static_cast<const char*> (bytes.getData());
    const auto size = (int) bytes.getSize();

    if (utf8)
    {
        const auto skip = hasBom (bytes) ? 3 : 0;
        return juce::String::fromUTF8 (data + skip, size - skip);
    }

#if JUCE_WINDOWS
    const auto wide = MultiByteToWideChar (932, 0, data, size, nullptr, 0);
    if (wide > 0)
    {
        std::vector<wchar_t> buffer ((size_t) wide);
        MultiByteToWideChar (932, 0, data, size, buffer.data(), wide);
        return juce::String (buffer.data(), (size_t) wide);
    }
#endif

    juce::String latin1;
    for (int i = 0; i < size; ++i)
        latin1 += (juce::juce_wchar) (juce::uint8) data[i];
    return latin1;
}

juce::MemoryBlock encodeShiftJis (const juce::String& text)
{
    juce::MemoryBlock out;
#if JUCE_WINDOWS
    const auto wide = text.toWideCharPointer();
    const auto len = (int) text.length();
    const auto n = WideCharToMultiByte (932, 0, wide, len, nullptr, 0, nullptr, nullptr);
    if (n > 0)
    {
        out.setSize ((size_t) n);
        WideCharToMultiByte (932, 0, wide, len, static_cast<char*> (out.getData()), n, nullptr, nullptr);
    }
#else
    out.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
#endif
    return out;
}
} // namespace wt
