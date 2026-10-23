#pragma once

#include <juce_core/juce_core.h>

namespace wt::xlsx
{
struct Sheet
{
    juce::String name;
    std::vector<juce::StringArray> rows;
};

/** Reads every worksheet (shared strings, inline strings and plain values). Files written by Excel, openpyxl and this module all work. */
bool read (const juce::File& file, std::vector<Sheet>& sheets, juce::String& error);

/** Writes a fresh workbook (bold, frozen header row; inline strings). Replaces the file atomically; fails if Excel has it open. */
bool write (const juce::File& file, const std::vector<Sheet>& sheets, juce::String& error);

/**
    Adds one row to the end of a sheet by editing that sheet's XML inside the package; every other part
    (other sheets, styles, column widths ...) is copied through unchanged.
*/
bool appendRow (const juce::File& file, const juce::String& sheetName, const juce::StringArray& fields, juce::String& error);
} // namespace wt::xlsx
