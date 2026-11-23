#include "softrender.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <zlib.h>

namespace soft {

void Canvas::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (size_t i = 0; i < rgba.size(); i += 4) { rgba[i]=r; rgba[i+1]=g; rgba[i+2]=b; rgba[i+3]=a; }
}

void Canvas::blend(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a == 0) return;
    size_t o = ((size_t)y * w + x) * 4;
    float sa = a / 255.0f, da = rgba[o+3] / 255.0f;
    float oa = sa + da * (1 - sa);
    if (oa <= 0) return;
    auto mix = [&](uint8_t src, uint8_t dst) {
        float v = (src * sa + dst * da * (1 - sa)) / oa;
        return (uint8_t)std::min(255.0f, std::max(0.0f, v));
    };
    rgba[o] = mix(r, rgba[o]);
    rgba[o+1] = mix(g, rgba[o+1]);
    rgba[o+2] = mix(b, rgba[o+2]);
    rgba[o+3] = (uint8_t)(oa * 255);
}

namespace {

struct P2 { double x, y; };

struct Xform {
    double a=1,b=0,c=0,d=1,tx=0,ty=0;
    P2 apply(P2 p) const { return {a*p.x + c*p.y + tx, b*p.x + d*p.y + ty}; }
};

void fillPolygon(Canvas& cv, const std::vector<P2>& pts, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (pts.size() < 3) return;
    double minY = 1e18, maxY = -1e18;
    for (auto& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    int y0 = std::max(0, (int)std::floor(minY)), y1 = std::min(cv.h - 1, (int)std::ceil(maxY));
    std::vector<double> xs;
    for (int y = y0; y <= y1; ++y) {
        double sy = y + 0.5;
        xs.clear();
        for (size_t i = 0; i < pts.size(); ++i) {
            const P2& p1 = pts[i];
            const P2& p2 = pts[(i + 1) % pts.size()];
            if ((p1.y <= sy && p2.y > sy) || (p2.y <= sy && p1.y > sy)) {
                double t = (sy - p1.y) / (p2.y - p1.y);
                xs.push_back(p1.x + t * (p2.x - p1.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            int xa = std::max(0, (int)std::ceil(xs[i] - 0.5));
            int xb = std::min(cv.w - 1, (int)std::floor(xs[i+1] - 0.5));
            for (int x = xa; x <= xb; ++x) cv.blend(x, y, r, g, b, a);
        }
    }
}

const swf::FillStyle* pickFill(const swf::Shape& sh, const swf::ShapePath& p) {
    if (p.fill0 >= 0 && p.fill0 < (int)sh.fills.size()) return &sh.fills[p.fill0];
    if (p.fill1 >= 0 && p.fill1 < (int)sh.fills.size()) return &sh.fills[p.fill1];
    return nullptr;
}

void fillColor(const swf::FillStyle* fs, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if (!fs) { r = 140; g = 140; b = 140; a = 255; return; }
    if (fs->kind == swf::FillStyle::Solid) { r = fs->r; g = fs->g; b = fs->b; a = fs->a; return; }
    if ((fs->kind == swf::FillStyle::LinearGradient || fs->kind == swf::FillStyle::RadialGradient)
        && !fs->stops.empty()) {
        // 取中间色近似（软光栅不做渐变插值）
        const auto& s = fs->stops[fs->stops.size() / 2];
        r = s.r; g = s.g; b = s.b; a = s.a; return;
    }
    // 位图填充：洋红占位，便于肉眼辨认
    r = 200; g = 60; b = 200; a = 160;
}

struct Item { int depth; int characterId; swf::Matrix m; bool hasClip; int clipDepth; };

std::vector<Item> listFrom(const std::vector<swf::Frame>& frames, int frameIndex) {
    std::vector<Item> out;
    std::map<int, Item> map;
    int last = std::min(frameIndex, (int)frames.size() - 1);
    for (int f = 0; f <= last; ++f)
        for (const auto& op : frames[f].ops) {
            if (op.remove) { map.erase(op.depth); continue; }
            map[op.depth] = Item{op.depth, op.characterId, op.matrix, op.hasClip, op.clipDepth};
        }
    for (auto& kv : map) out.push_back(kv.second);
    return out;
}

void drawCharacter(Canvas& cv, const swf::Movie& mv, int id, int frame, const Xform& xf, int depthLimit);

void drawList(Canvas& cv, const swf::Movie& mv, const std::vector<swf::Frame>& frames,
              int frameIndex, Xform base, int depthLimit) {
    for (const auto& it : listFrom(frames, frameIndex)) {
        Xform xf{it.m.a * base.a + it.m.c * base.b,
                 it.m.b * base.a + it.m.d * base.b,
                 it.m.a * base.c + it.m.c * base.d,
                 it.m.b * base.c + it.m.d * base.d,
                 it.m.a * base.tx + it.m.c * base.ty + it.m.tx,
                 it.m.b * base.tx + it.m.d * base.ty + it.m.ty};
        drawCharacter(cv, mv, it.characterId, frameIndex, xf, depthLimit - 1);
    }
}

void drawCharacter(Canvas& cv, const swf::Movie& mv, int id, int frame, const Xform& xf, int depthLimit) {
    if (auto it = mv.shapes.find(id); it != mv.shapes.end()) {
        const swf::Shape& sh = it->second;
        for (const auto& path : sh.paths) {
            if (path.pts.size() < 3) continue;
            std::vector<P2> poly;
            poly.reserve(path.pts.size());
            for (auto& q : path.pts) poly.push_back(xf.apply({q.first, q.second}));
            uint8_t r, g, b, a;
            fillColor(pickFill(sh, path), r, g, b, a);
            fillPolygon(cv, poly, r, g, b, a);
        }
        return;
    }
    if (auto it = mv.sprites.find(id); it != mv.sprites.end()) {
        if (depthLimit <= 0) return;
        drawList(cv, mv, it->second.frames, frame, xf, depthLimit);
    }
    // 位图角色在软光栅里跳过（JPEG 解码不在 L0 里做）
}

} // namespace

void renderFrame(const swf::Movie& mv, int frameIndex, double scale, Canvas& cv) {
    uint8_t br = (mv.backgroundColor >> 16) & 0xFF;
    uint8_t bg = (mv.backgroundColor >> 8) & 0xFF;
    uint8_t bb = mv.backgroundColor & 0xFF;
    cv.fill(br, bg, bb, 255);
    Xform base{scale, 0, 0, scale, -mv.frameSize.x0 * scale, -mv.frameSize.y0 * scale};
    drawList(cv, mv, mv.frames, frameIndex, base, 8);
}

static void put32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x>>24); v.push_back((x>>16)&255); v.push_back((x>>8)&255); v.push_back(x&255); }

bool writePng(const std::string& path, const Canvas& c) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)c.h * (c.w * 4 + 1));
    for (int y = 0; y < c.h; ++y) {
        raw.push_back(0);
        const uint8_t* row = &c.rgba[(size_t)y * c.w * 4];
        raw.insert(raw.end(), row, row + (size_t)c.w * 4);
    }
    uLongf clen = compressBound(raw.size());
    std::vector<uint8_t> comp(clen);
    if (compress2(comp.data(), &clen, raw.data(), raw.size(), 6) != Z_OK) return false;
    comp.resize(clen);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    std::fwrite(sig, 1, 8, f);
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> len; put32(len, (uint32_t)data.size());
        std::fwrite(len.data(), 1, 4, f);
        std::fwrite(type, 1, 4, f);
        if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
        std::vector<uint8_t> crc;
        std::vector<uint8_t> buf(type, type + 4);
        buf.insert(buf.end(), data.begin(), data.end());
        put32(crc, (uint32_t)crc32(0, buf.data(), (uInt)buf.size()));
        std::fwrite(crc.data(), 1, 4, f);
    };
    std::vector<uint8_t> ihdr;
    put32(ihdr, (uint32_t)c.w); put32(ihdr, (uint32_t)c.h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk("IHDR", ihdr);
    chunk("IDAT", comp);
    chunk("IEND", {});
    std::fclose(f);
    return true;
}

} // namespace soft
