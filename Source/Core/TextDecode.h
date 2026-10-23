#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
/** Text file bytes -> string: UTF-8 (with or without BOM) when valid, otherwise Shift-JIS (what Excel's plain "CSV" save writes). */
juce::String decodeTextBytes (const juce::MemoryBlock& bytes, bool* wasUtf8 = nullptr);

/** True if the file starts with a UTF-8 BOM or is valid UTF-8. */
bool isUtf8Text (const juce::MemoryBlock& bytes);

juce::MemoryBlock encodeShiftJis (const juce::String& text); // used by self tests
} // namespace wt
