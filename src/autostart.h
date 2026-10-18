#pragma once

// Start with Windows through the per-user Run key.
bool IsAutostartEnabled();
bool SetAutostart(bool enable);
// Task Manager keeps its on/off choice for a Run entry as a binary value: an odd first byte means disabled.
bool StartupApprovedMeansDisabled(const unsigned char* data, unsigned long size);
// Test seam: use another key path below HKCU instead of the real Run key.
void SetAutostartKeyOverride(const wchar_t* subKey);
// Test seam: 1 or 0 forces portable or installed naming, -1 follows the real exe.
void SetAutostartPortableOverride(int portable);
