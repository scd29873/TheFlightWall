#pragma once
// Host stand-in for ESP32-HUB75-MatrixPanel-DMA: the same configuration struct
// and class surface Hub75Display uses, drawing into a plain RGB565 buffer the
// preview reads back. Field names, enum values, the rotation transform and the
// refresh-rate arithmetic mirror the library (mrcodetastic, 2026-08 master) so
// that a mistake against the real API fails to compile here too.
#include <vector>
#include "Arduino.h"
#include <Adafruit_GFX.h>

#ifndef PIXEL_COLOR_DEPTH_BITS
#define PIXEL_COLOR_DEPTH_BITS 8
#endif

struct HUB75_I2S_CFG
{
    enum shift_driver { SHIFTREG = 0, FM6124, FM6126A, ICN2038S, MBI5124, DP3246 };
    enum line_driver { TYPE138 = 0, TYPE595, TYPE_DIRECT, SM5266P, SM5368 = TYPE595 };
    enum clk_speed { HZ_8M = 8000000, HZ_10M = 8000000, HZ_15M = 16000000, HZ_16M = 16000000, HZ_20M = 20000000 };

    uint16_t mx_width;
    uint16_t mx_height;
    uint16_t chain_length;
    struct i2s_pins
    {
        int8_t r1, g1, b1, r2, g2, b2, a, b, c, d, e, lat, oe, clk;
    } gpio;
    shift_driver driver = SHIFTREG;
    line_driver line_decoder = TYPE138;
    bool double_buff = false;
    clk_speed i2sspeed = HZ_8M;
    uint8_t latch_blanking = 2;
    bool clkphase = true;
    uint8_t min_refresh_rate = 60;
    uint8_t pixel_color_depth_bits = PIXEL_COLOR_DEPTH_BITS;

    HUB75_I2S_CFG(uint16_t w, uint16_t h, uint16_t chain, i2s_pins pins)
        : mx_width(w), mx_height(h), chain_length(chain), gpio(pins) {}
};

class MatrixPanel_I2S_DMA;
// The panel Hub75Display constructed most recently, for the preview to read.
extern MatrixPanel_I2S_DMA *g_previewPanel;

class MatrixPanel_I2S_DMA : public Adafruit_GFX
{
public:
    explicit MatrixPanel_I2S_DMA(const HUB75_I2S_CFG &cfg)
        : Adafruit_GFX(cfg.mx_width * cfg.chain_length, cfg.mx_height), m_cfg(cfg),
          _fb((size_t)cfg.mx_width * cfg.chain_length * cfg.mx_height, 0)
    {
        g_previewPanel = this;
    }
    virtual ~MatrixPanel_I2S_DMA() {}

    bool begin()
    {
        // The library's Step 1, verbatim in arithmetic: raise lsbMsbTransitionBit
        // until the refresh rate meets min_refresh_rate.
        const int depth = m_cfg.pixel_color_depth_bits;
        const int pixelsPerRow = m_cfg.mx_width * m_cfg.chain_length;
        const int rowsPerFrame = m_cfg.mx_height / 2;
        for (lsbMsbTransitionBit = 0;; ++lsbMsbTransitionBit)
        {
            const long long psPerClock = 1000000000000LL / m_cfg.i2sspeed;
            const long long nsPerLatch = (pixelsPerRow * psPerClock) / 1000;
            long long nsPerRow = depth * nsPerLatch;
            for (int i = lsbMsbTransitionBit + 1; i < depth; i++)
                nsPerRow += (1LL << (i - lsbMsbTransitionBit - 1)) * nsPerLatch;
            calculated_refresh_rate = (int)(1000000000LL / (nsPerRow * rowsPerFrame));
            if (calculated_refresh_rate >= m_cfg.min_refresh_rate || lsbMsbTransitionBit >= depth - 1)
                break;
        }
        return true;
    }
    void setBrightness8(uint8_t b) { brightness = b; }
    void clearScreen() { std::fill(_fb.begin(), _fb.end(), 0); }
    void stopDMAoutput() { clearScreen(); }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        // The library's transform() for rotation 2; the others are not used.
        if (rotation == 2)
        {
            x = _width - 1 - x;
            y = _height - 1 - y;
        }
        if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT)
            return;
        _fb[(size_t)y * WIDTH + x] = color;
    }

    const std::vector<uint16_t> &frame() const { return _fb; }
    int lsbMsbTransitionBit = 0;
    int calculated_refresh_rate = 0;
    uint8_t brightness = 255;

protected:
    struct
    {
        void dma_transfer_start() {}
        void dma_transfer_stop() {}
    } dma_bus;
    HUB75_I2S_CFG m_cfg;

private:
    std::vector<uint16_t> _fb;
};
