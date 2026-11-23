// 平台无关软光栅（L2 的 Linux 版本，用于 CI 里把 SWF 渲成 PNG）
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "swf.hpp"

namespace soft {

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // w*h*4
    Canvas(int w_, int h_) : w(w_), h(h_), rgba((size_t)w_ * h_ * 4, 0) {}
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void blend(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

void renderFrame(const swf::Movie& mv, int frameIndex, double scale, Canvas& out);
bool writePng(const std::string& path, const Canvas& c);

} // namespace soft
