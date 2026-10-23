#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
/** Comma separated text with "quoted, fields" and "" for a quote. Fields are trimmed; empty lines are dropped. */
std::vector<juce::StringArray> parseCsv (juce::String text);

juce::String csvEscape (const juce::String& s);

/** One row, ending in CRLF (what Excel writes). */
juce::String toCsvLine (const juce::StringArray& fields);

/** UTF-8 or Shift-JIS (what Excel's plain "CSV" save writes) file -> rows; empty if the file cannot be read. */
std::vector<juce::StringArray> readCsvRows (const juce::File& file);

/** Rows -> UTF-8 with a BOM (so Excel reads the Japanese correctly); every row as wide as the first. */
juce::MemoryBlock csvBytes (const std::vector<juce::StringArray>& rows);
} // namespace wt
