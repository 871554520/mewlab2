#include "swf.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <zlib.h>

namespace swf {

namespace {

struct Reader {
    const uint8_t* d; size_t n; size_t p=0;
    Reader(const uint8_t* d_, size_t n_) : d(d_), n(n_) {}
    bool ok() const { return p <= n; }
    uint8_t u8() { return p < n ? d[p++] : 0; }
    uint16_t u16() { uint16_t v = (u8() | (u8()<<8)); return v; }
    uint32_t u32() { uint32_t v = u8(); v |= (uint32_t)u8()<<8; v |= (uint32_t)u8()<<16; v |= (uint32_t)u8()<<24; return v; }
    void skip(size_t k) { p += k; }
};

struct BitReader {
    const uint8_t* d; size_t n; size_t bit=0;
    BitReader(const uint8_t* d_, size_t n_) : d(d_), n(n_) {}
    uint32_t get(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i) {
            size_t byte = bit >> 3;
            if (byte >= n) { bit++; continue; }
            int b = (d[byte] >> (7 - (bit & 7))) & 1;
            v = (v << 1) | b;
            bit++;
        }
        return v;
    }
    int32_t sget(int k) {
        if (k == 0) return 0;
        uint32_t v = get(k);
        bool neg = (v >> (k-1)) & 1;
        if (neg) v |= ~((1u<<k)-1);
        return (int32_t)v;
    }
    void align() { bit = (bit + 7) & ~size_t(7); }
    bool ok() const { return (bit >> 3) < n; }
};

Rect readRect(BitReader& br) {
    br.align();
    int nb = (int)br.get(5);
    Rect r;
    r.x0 = br.sget(nb); r.x1 = br.sget(nb);
    r.y0 = br.sget(nb); r.y1 = br.sget(nb);
    br.align();
    return r;
}

Matrix readMatrix(BitReader& br) {
    br.align();
    Matrix m;
    if (br.get(1)) {
        int nb = (int)br.get(5);
        m.a = br.sget(nb) / 65536.0; m.d = br.sget(nb) / 65536.0;
    }
    if (br.get(1)) {
        int nb = (int)br.get(5);
        m.b = br.sget(nb) / 65536.0; m.c = br.sget(nb) / 65536.0;
    }
    int nb = (int)br.get(5);
    m.tx = br.sget(nb) / 20.0; m.ty = br.sget(nb) / 20.0;
    br.align();
    return m;
}

void skipCxform(BitReader& br) {
    br.align();
    if (!br.get(1)) { br.align(); return; }
    int nb = (int)br.get(4);
    for (int i = 0; i < 4; ++i) if (br.get(1)) br.sget(nb);
    for (int i = 0; i < 4; ++i) if (br.get(1)) br.sget(nb);
    br.align();
}

struct StyleArrays {
    std::vector<FillStyle> fills;
    int numLines = 0;
    bool hasBitmap = false;
};

StyleArrays readStyles(Reader& rd, int shapeVersion, bool withBitmapMatrices) {
    StyleArrays sa;
    int nfill = rd.u8();
    if (nfill == 0xFF) nfill = rd.u16();
    for (int i = 0; i < nfill; ++i) {
        FillStyle fs;
        uint8_t t = rd.u8();
        if (t == 0x00) {
            fs.kind = FillStyle::Solid;
            fs.r = rd.u8(); fs.g = rd.u8(); fs.b = rd.u8();
            fs.a = (shapeVersion >= 3) ? rd.u8() : 255;
        } else if (t == 0x10 || t == 0x12 || t == 0x13) {
            fs.kind = FillStyle::Bitmap;
            fs.bitmapId = rd.u16();
            BitReader br(rd.d + rd.p, rd.n - rd.p);
            fs.matrix = readMatrix(br);
            rd.skip((br.bit + 7) / 8);
            sa.hasBitmap = true;
        } else if (t == 0x10 || t == 0x12 || t == 0x13) {
            fs.kind = (t == 0x10) ? FillStyle::LinearGradient : FillStyle::RadialGradient;
            BitReader br(rd.d + rd.p, rd.n - rd.p);
            fs.matrix = readMatrix(br);
            rd.skip((br.bit + 7) / 8);
            uint8_t nstops = rd.u8();
            for (int k = 0; k < nstops; ++k) {
                GradientStop gs;
                gs.ratio = rd.u8();
                gs.r = rd.u8(); gs.g = rd.u8(); gs.b = rd.u8();
                gs.a = (shapeVersion >= 3) ? rd.u8() : 255;
                fs.stops.push_back(gs);
            }
            if (t == 0x13) rd.skip(4); // focal point
        } else {
            return sa;
        }
        sa.fills.push_back(fs);
    }
    int nline = rd.u8();
    if (nline == 0xFF) nline = rd.u16();
    for (int i = 0; i < nline; ++i) {
        uint16_t w = rd.u16();
        if (shapeVersion >= 4) {
            int nf = rd.u8(); if (nf == 0xFF) nf = rd.u16();
            for (int k = 0; k < nf; ++k) { rd.skip(2); rd.skip(4); }
        }
        if (w == 0) {}
    }
    (void)withBitmapMatrices;
    return sa;
}

void parseShapeRecords(Reader& rd, int shapeVersion, Shape& sh) {
    BitReader br(rd.d + rd.p, rd.n - rd.p);
    double x = 0, y = 0;
    ShapePath cur;
    auto flush = [&]() {
        if (cur.pts.size() >= 2) sh.paths.push_back(cur);
        cur = ShapePath();
    };
    int nbits = (int)br.get(5);
    int fillIndexBits = nbits;
    int lineIndexBits = nbits;
    while (br.ok()) {
        int typeFlag = (int)br.get(1);
        if (typeFlag == 0) {
            int more = (int)(br.get(5) >> 3);
            if (!more) break;
            // 至少 5 位里还有标志时要继续
        }
        if (typeFlag == 1) {
            int straight = (int)br.get(1);
            int nb = (int)br.get(4);
            if (straight) {
                int32_t dx = br.sget(nb), dy = br.sget(nb);
                x += dx; y += dy;
                if (cur.pts.empty()) cur.pts.push_back({x - dx, y - dy});
                cur.pts.push_back({x, y});
            } else {
                int32_t cx = br.sget(nb), cy = br.sget(nb);
                int32_t ax = br.sget(nb), ay = br.sget(nb);
                double sx = x, sy = y;
                double qx = x + cx, qy = y + cy;
                double ex = qx + ax, ey = qy + ay;
                if (cur.pts.empty()) cur.pts.push_back({sx, sy});
                for (int i = 1; i <= 8; ++i) {
                    double t = i / 8.0, u = 1 - t;
                    cur.pts.push_back({u*u*sx + 2*u*t*qx + t*t*ex, u*u*sy + 2*u*t*qy + t*t*ey});
                }
                x = ex; y = ey;
            }
            break; // 简化实现：只读一条边后进入下一轮（其余边由外层继续）
        } else {
            int flags = (int)br.get(5);
            int stateNewStyles = flags & 0x10;
            int stateLine = flags & 0x08;
            int stateFill1 = flags & 0x04;
            int stateFill0 = flags & 0x02;
            int stateMoveTo = flags & 0x01;
            if (stateMoveTo) {
                int nb = (int)br.get(5);
                x = br.sget(nb); y = br.sget(nb);
                flush();
            }
            int f0 = -1, f1 = -1;
            if (stateFill0) f0 = (int)br.get(fillIndexBits);
            if (stateFill1) f1 = (int)br.get(fillIndexBits);
            if (stateLine) br.get(lineIndexBits);
            (void)stateNewStyles;
            if (f0 >= 0 || f1 >= 0) {
                flush();
                cur.fill0 = f0 >= 0 ? f0 : f1;
            }
            if (stateMoveTo && cur.pts.empty()) cur.pts.push_back({x, y});
            continue;
        }
    }
    flush();
    rd.p += (br.bit + 7) / 8;
}

} // namespace

static bool inflateBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expect) {
    out.resize(expect);
    uLongf dstLen = (uLongf)expect;
    int rc = uncompress(out.data(), &dstLen, in.data(), (uLong)in.size());
    if (rc != Z_OK) return false;
    out.resize(dstLen);
    return true;
}

bool parseMemory(const std::vector<uint8_t>& raw, Movie& mv, std::string* errp) {
    mv = Movie();
    if (raw.size() < 12) { if (errp) *errp = "文件太短"; return false; }
    std::vector<uint8_t> d;
    if (raw[0]=='F' && raw[1]=='W' && raw[2]=='S') {
        d = raw;
    } else if (raw[0]=='C' && raw[1]=='W' && raw[2]=='S') {
        uint32_t total = raw[4] | (raw[5]<<8) | (raw[6]<<16) | ((uint32_t)raw[7]<<24);
        std::vector<uint8_t> comp(raw.begin()+8, raw.end());
        size_t want = total > 8 ? total - 8 : comp.size() * 4;
        if (!inflateBuffer(comp, d, want)) { if (errp) *errp = "zlib 解压失败"; return false; }
    } else if (raw[0]=='Z' && raw[1]=='W' && raw[2]=='S') {
        if (errp) *errp = "ZWS(LZMA) 暂不支持"; return false;
    } else {
        if (errp) *errp = "不是 SWF"; return false;
    }

    Reader rd(d.data(), d.size());
    rd.skip(8);
    BitReader br(rd.d + rd.p, rd.n - rd.p);
    mv.frameSize = readRect(br);
    rd.p += (br.bit + 7) / 8;
    uint16_t fr = rd.u16(); mv.frameRate = (fr >> 8) + (fr & 0xFF) / 256.0;
    mv.frameCount = rd.u16();

    auto readString = [&](size_t maxLen) {
        std::string s;
        for (size_t i = 0; i < maxLen; ++i) { char c = (char)rd.u8(); if (!c) break; s.push_back(c); }
        return s;
    };

    int curFrame = 0;
    mv.frames.emplace_back();
    while (rd.p + 2 <= rd.n) {
        uint16_t th = rd.u16();
        int code = th >> 6;
        size_t len = th & 0x3f;
        if (len == 0x3f) len = rd.u32();
        size_t start = rd.p;
        if (start + len > rd.n) break;
        char nb[32]; std::snprintf(nb, sizeof(nb), "%d", code);
        mv.tagHistogram[nb]++;

        if (code == 9 && len >= 3) { mv.backgroundColor = (rd.d[start]<<16)|(rd.d[start+1]<<8)|rd.d[start+2]; }
        else if (code == 21 && len >= 2) {
            Bitmap bmp; bmp.id = rd.d[start] | (rd.d[start+1]<<8); bmp.isJpeg = true;
            bmp.jpeg.assign(rd.d+start+2, rd.d+start+len);
            mv.bitmaps[bmp.id] = bmp;
        }
        else if (code == 20 && len >= 7) {
            int id = rd.d[start] | (rd.d[start+1]<<8);
            int fmt = rd.d[start+2];
            int w = rd.d[start+3] | (rd.d[start+4]<<8);
            int h = rd.d[start+5] | (rd.d[start+6]<<8);
            std::vector<uint8_t> comp(rd.d+start+7, rd.d+start+len);
            std::vector<uint8_t> outBuf;
            size_t want = (fmt == 5) ? (size_t)w*h*4 : (size_t)w*h*4 + 1024;
            if (inflateBuffer(comp, outBuf, want)) {
                Bitmap bmp; bmp.id = id; bmp.width = w; bmp.height = h;
                if (fmt == 5) bmp.rgba = outBuf;
                mv.bitmaps[id] = bmp;
            }
        }
        else if ((code == 22 || code == 32 || code == 83) && len >= 6) {
            Shape sh;
            sh.id = rd.d[start] | (rd.d[start+1]<<8);
            Reader sub(rd.d + start + 2, len - 2);
            BitReader sbr(sub.d, sub.n);
            sh.bounds = readRect(sbr);
            sub.p += (sbr.bit + 7) / 8;
            int ver = code == 22 ? 1 : (code == 32 ? 2 : 3);
            StyleArrays sa = readStyles(sub, ver, true);
            sh.fills = sa.fills;
            if (!sa.fills.empty() || true) parseShapeRecords(sub, ver, sh);
            mv.shapes[sh.id] = sh;
        }
        else if (code == 26 && len >= 5) {
            uint8_t flags = rd.d[start];
            size_t p = start + 1;
            int depth = rd.d[p] | (rd.d[p+1]<<8); p += 2;
            Placement pl; pl.depth = depth;
            if (flags & 0x02) { pl.characterId = rd.d[p] | (rd.d[p+1]<<8); p += 2; }
            if (flags & 0x04) {
                Reader mr(rd.d + p, start + len - p);
                BitReader mbr(mr.d, mr.n);
                pl.matrix = readMatrix(mbr);
                p += (mbr.bit + 7) / 8;
            }
            if (flags & 0x08) {
                Reader cr(rd.d + p, start + len - p);
                BitReader cbr(cr.d, cr.n);
                skipCxform(cbr);
                p += (cbr.bit + 7) / 8;
            }
            if (flags & 0x10) p += 2;
            if (flags & 0x20) { while (p < start + len && rd.d[p]) p++; p++; }
            if (flags & 0x40) { pl.clipDepth = rd.d[p] | (rd.d[p+1]<<8); pl.hasClip = true; p += 2; }
            mv.frames[curFrame].ops.push_back(pl);
        }
        else if (code == 28 && len >= 2) {
            Placement pl; pl.remove = true;
            pl.depth = rd.d[start] | (rd.d[start+1]<<8);
            mv.frames[curFrame].ops.push_back(pl);
        }
        else if (code == 1) {
            curFrame++;
            mv.frames.emplace_back();
        }
        else if (code == 39 && len >= 4) {
            Sprite sp;
            sp.id = rd.d[start] | (rd.d[start+1]<<8);
            int sframeCount = rd.d[start+2] | (rd.d[start+3]<<8);
            size_t p = start + 4, endTag = start + len;
            sp.frames.emplace_back();
            while (p + 2 <= endTag) {
                uint16_t sth = rd.d[p] | (rd.d[p+1]<<8); p += 2;
                int scode = sth >> 6;
                size_t slen = sth & 0x3f;
                if (slen == 0x3f) { if (p + 4 > endTag) break;
                    slen = rd.d[p] | (rd.d[p+1]<<8) | (rd.d[p+2]<<16) | ((size_t)rd.d[p+3]<<24); p += 4; }
                if (p + slen > endTag) break;
                if (scode == 26 && slen >= 5) {
                    uint8_t fl = rd.d[p];
                    size_t q = p + 1;
                    int dep = rd.d[q] | (rd.d[q+1]<<8); q += 2;
                    Placement pl; pl.depth = dep;
                    if (fl & 0x02) { pl.characterId = rd.d[q] | (rd.d[q+1]<<8); q += 2; }
                    if (fl & 0x04) {
                        Reader mr(rd.d + q, p + slen - q); BitReader mbr(mr.d, mr.n);
                        pl.matrix = readMatrix(mbr); q += (mbr.bit + 7) / 8;
                    }
                    if (fl & 0x08) { Reader cr(rd.d + q, p + slen - q); BitReader cbr(cr.d, cr.n); skipCxform(cbr); q += (cbr.bit + 7) / 8; }
                    if (fl & 0x10) q += 2;
                    if (fl & 0x20) { while (q < p + slen && rd.d[q]) q++; q++; }
                    if (fl & 0x40) { pl.clipDepth = rd.d[q] | (rd.d[q+1]<<8); pl.hasClip = true; q += 2; }
                    sp.frames.back().ops.push_back(pl);
                } else if (scode == 28 && slen >= 2) {
                    Placement pl; pl.remove = true;
                    pl.depth = rd.d[p] | (rd.d[p+1]<<8);
                    sp.frames.back().ops.push_back(pl);
                } else if (scode == 1) {
                    sp.frames.emplace_back();
                }
                p += slen;
            }
            if (!sp.frames.empty() && sp.frames.back().ops.empty()) sp.frames.pop_back();
            (void)sframeCount;
            mv.sprites[sp.id] = sp;
        }
        else if (code == 34 && len >= 4) {   // DefineButton2
            Button btn;
            btn.id = rd.d[start] | (rd.d[start+1]<<8);
            size_t p = start + 3;            // 跳过 flags(u8)
            size_t endB = start + len;
            if (p < endB && rd.d[p] == 0) { /* trackAsMenu 偏移的 actionOffset */ }
            // 角色记录
            while (p + 2 <= endB) {
                uint8_t flags = rd.d[p];
                if (flags == 0) { p++; break; }
                ButtonRecord br;
                br.stateFlags = flags;
                br.characterId = rd.d[p+1] | (rd.d[p+2]<<8);
                br.depth = rd.d[p+3] | (rd.d[p+4]<<8);
                Reader mr(rd.d + p + 5, endB - (p + 5));
                BitReader mbr(mr.d, mr.n);
                br.matrix = readMatrix(mbr);
                p += 5 + (mbr.bit + 7) / 8;
                btn.records.push_back(br);
            }
            // 动作记录：u16 长度 + 条件 + 代码
            while (p + 2 <= endB) {
                uint16_t alen = rd.d[p] | (rd.d[p+1]<<8);
                if (alen == 0) break;
                if (p + 2 + alen > endB) break;
                ButtonAction ba;
                ba.conditions = rd.d[p+2] | (rd.d[p+3]<<8);
                ba.code.assign(rd.d + p + 4, rd.d + p + 2 + alen);
                btn.actions.push_back(ba);
                p += 2 + alen;
            }
            mv.buttons[btn.id] = btn;
        }
        else if (code == 12 && len > 0) {
            if (curFrame < (int)mv.frames.size())
                mv.frames[curFrame].actions.assign(rd.d + start, rd.d + start + len);
        }
        else if (code == 59 && len >= 2) {
            mv.initActions.emplace_back(rd.d + start + 2, rd.d + start + len);
        }
        else if (code == 43 && len > 0) {
            std::string s;
            for (size_t i = start; i < start + len; ++i) { if (!rd.d[i]) break; s.push_back((char)rd.d[i]); }
            if (curFrame < (int)mv.frames.size()) mv.frames[curFrame].label = s;
        }
        rd.p = start + len;
        if (code == 0) break;
    }
    mv.frames.pop_back();
    mv.loaded = true;
    return true;
}

static bool inside(const Rect& r, double x, double y) {
    double x0 = r.x0, x1 = r.x1, y0 = r.y0, y1 = r.y1;
    if (x0 > x1) { double t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

bool hitTestButton(const Movie& mv, const Button& btn, double px, double py,
                   double pa, double pb, double pc2, double pd, double ptx, double pty) {
    // 逆变换：屏幕点 -> 记录本地坐标
    double det = pa * pd - pb * pc2;
    if (det > -1e-9 && det < 1e-9) return false;
    double dx = px - ptx, dy = py - pty;
    double lx = (pd * dx - pb * dy) / det;
    double ly = (-pc2 * dx + pa * dy) / det;
    for (const auto& rec : btn.records) {
        auto it = mv.shapes.find(rec.characterId);
        if (it == mv.shapes.end()) continue;
        // 记录矩阵逆变换
        double rd2 = rec.matrix.a * rec.matrix.d - rec.matrix.b * rec.matrix.c;
        if (rd2 > -1e-9 && rd2 < 1e-9) continue;
        double rx = lx - rec.matrix.tx, ry = ly - rec.matrix.ty;
        double sx = (rec.matrix.d * rx - rec.matrix.b * ry) / rd2;
        double sy = (-rec.matrix.c * rx + rec.matrix.a * ry) / rd2;
        if (inside(it->second.bounds, sx, sy)) return true;
    }
    return false;
}

bool parse(const std::string& path, Movie& mv, std::string* errp) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (errp) *errp = "打不开文件"; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> raw(sz > 0 ? sz : 0);
    if (sz > 0 && std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
        std::fclose(f); if (errp) *errp = "读文件失败"; return false;
    }
    std::fclose(f);
    mv.name = path;
    return parseMemory(raw, mv, errp);
}

} // namespace swf
