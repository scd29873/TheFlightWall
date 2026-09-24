#pragma once
// Host stand-in for LittleFS: paths resolve under PREVIEW_DATA_DIR (the
// firmware's data/ folder), so the preview draws the real logo tiles.
#include <cstdio>
#include <string>
#include "Arduino.h"

class File
{
public:
    File() {}
    explicit File(FILE *f) : _f(f) {}
    explicit operator bool() const { return _f != nullptr; }
    size_t read(uint8_t *buf, size_t n) { return _f ? fread(buf, 1, n, _f) : 0; }
    void close()
    {
        if (_f)
            fclose(_f);
        _f = nullptr;
    }

private:
    FILE *_f = nullptr;
};

class LittleFSFS
{
public:
    File open(const String &path, const char *mode)
    {
        const std::string full = std::string(PREVIEW_DATA_DIR) + path.c_str();
        return File(fopen(full.c_str(), mode[0] == 'r' ? "rb" : "wb"));
    }
};
extern LittleFSFS LittleFS;
