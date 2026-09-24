// Host-side stand-in for the few Arduino-core pieces the display code uses:
// String, Print, Serial, millis(). Enough to compile Hub75Display.cpp and
// Adafruit_GFX with g++ -- not an emulator. See ../run.sh.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using std::max;
using std::min;

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define radians(deg) ((deg) * DEG_TO_RAD)

typedef uint8_t byte;
typedef bool boolean;

#define PROGMEM
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif

class __FlashStringHelper; // Adafruit_GFX declares F()-string overloads
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

// The preview drives time: cycle and toast logic read millis().
extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }
inline void delay(unsigned long ms) { g_fakeMillis += ms; }
inline void yield() {}

// WString.h's include guard -- utils/LruCache.h keys its std::hash<String> on it.
#define String_class_h

class String
{
public:
    String() {}
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}
    String(double v, unsigned int decimals = 2)
    {
        char b[48];
        snprintf(b, sizeof(b), "%.*f", (int)decimals, v);
        _s = b;
    }

    unsigned int length() const { return (unsigned int)_s.size(); }
    bool isEmpty() const { return _s.empty(); }
    const char *c_str() const { return _s.c_str(); }
    char operator[](unsigned int i) const { return i < _s.size() ? _s[i] : 0; }
    char &operator[](unsigned int i) { return _s[i]; }

    String substring(unsigned int left) const { return left >= _s.size() ? String() : String(_s.substr(left)); }
    String substring(unsigned int left, unsigned int right) const
    {
        if (left > right)
            std::swap(left, right);
        if (left >= _s.size())
            return String();
        if (right > _s.size())
            right = (unsigned int)_s.size();
        return String(_s.substr(left, right - left));
    }
    int indexOf(char c, unsigned int from = 0) const
    {
        const size_t p = _s.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String &t, unsigned int from = 0) const
    {
        const size_t p = _s.find(t._s, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    bool startsWith(const String &p) const { return _s.compare(0, p._s.size(), p._s) == 0; }
    bool endsWith(const String &p) const
    {
        return _s.size() >= p._s.size() && _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0;
    }
    bool equalsIgnoreCase(const String &o) const
    {
        if (_s.size() != o._s.size())
            return false;
        for (size_t i = 0; i < _s.size(); ++i)
            if (tolower((unsigned char)_s[i]) != tolower((unsigned char)o._s[i]))
                return false;
        return true;
    }
    void remove(unsigned int index) { if (index < _s.size()) _s.erase(index); }
    void remove(unsigned int index, unsigned int count)
    {
        if (index < _s.size())
            _s.erase(index, count);
    }
    void toUpperCase()
    {
        for (auto &c : _s)
            c = (char)toupper((unsigned char)c);
    }
    void toLowerCase()
    {
        for (auto &c : _s)
            c = (char)tolower((unsigned char)c);
    }
    void trim()
    {
        const size_t a = _s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
        {
            _s.clear();
            return;
        }
        _s = _s.substr(a, _s.find_last_not_of(" \t\r\n") - a + 1);
    }

    // What ArduinoJson's String writer needs, so the shim also serves host
    // checks of the JSON parsers.
    bool concat(const char *p, unsigned int len)
    {
        _s.append(p, len);
        return true;
    }
    bool concat(const char *p)
    {
        _s.append(p ? p : "");
        return true;
    }
    bool reserve(unsigned int size)
    {
        _s.reserve(size);
        return true;
    }
    String &operator+=(const String &o)
    {
        _s += o._s;
        return *this;
    }
    String &operator+=(const char *o)
    {
        _s += o;
        return *this;
    }
    String &operator+=(char c)
    {
        _s += c;
        return *this;
    }
    friend String operator+(const String &a, const String &b) { return String(a._s + b._s); }
    friend String operator+(const String &a, const char *b) { return String(a._s + b); }
    friend String operator+(const char *a, const String &b) { return String(std::string(a) + b._s); }
    friend String operator+(const String &a, char b) { return String(a._s + b); }
    bool operator==(const String &o) const { return _s == o._s; }
    bool operator==(const char *o) const { return _s == o; }
    bool operator!=(const String &o) const { return _s != o._s; }
    bool operator!=(const char *o) const { return _s != o; }
    bool operator<(const String &o) const { return _s < o._s; }

private:
    std::string _s;
};

class Print
{
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buf, size_t n)
    {
        size_t k = 0;
        while (n--)
            k += write(*buf++);
        return k;
    }
    size_t print(const char *s) { return write((const uint8_t *)s, strlen(s)); }
    size_t print(const String &s) { return print(s.c_str()); }
    size_t print(long v) { return print(String(v)); }
    size_t println() { return print("\n"); }
    size_t println(const char *s) { return print(s) + println(); }
    size_t println(const String &s) { return print(s) + println(); }
    size_t println(long v) { return print(v) + println(); }
    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        char b[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        return print(b);
    }
};

// Serial goes to stderr, so a preview's own output stays clean.
class HardwareSerial : public Print
{
public:
    size_t write(uint8_t c) override { return fputc(c, stderr) == EOF ? 0 : 1; }
};
extern HardwareSerial Serial;
