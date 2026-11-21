#pragma once

// Helpers only the tests need; the product has no use for them.

#include <string>

#include "settings.h"

// Last n lines of the current log file.
inline std::string LastLogLines(int n) {
    std::string text;
    if (!ReadFileUtf8(LogDir() + L"\\admutify.log", text)) return {};
    size_t end = text.size();
    int lines = 0;
    size_t pos = end;
    while (pos > 0) {
        --pos;
        if (text[pos] == '\n' && pos + 1 < end) {
            if (++lines >= n) {
                ++pos;
                break;
            }
        }
    }
    return text.substr(pos);
}

// Three-way merge: a field that differs between shown and current takes the current value, every other field keeps
// the value from the file.
inline Settings MergeSettingsChange(const Settings& fromFile, const Settings& shown, const Settings& current) {
    return ApplySettingsFields(fromFile, current, SettingsDiff(shown, current));
}
