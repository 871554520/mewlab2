// CI 用：把 SWF 的第 N 帧渲成 PNG
#include "swf.hpp"
#include "softrender.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: render swf frame out.png [scale]\n"); return 2; }
    swf::Movie mv; std::string err;
    if (!swf::parse(argv[1], mv, &err)) { printf("ERR %s\n", err.c_str()); return 1; }
    int frame = std::atoi(argv[2]);
    double scale = argc > 4 ? std::atof(argv[4]) : 0.5;
    double w = mv.frameSize.x1 - mv.frameSize.x0, h = mv.frameSize.y1 - mv.frameSize.y0;
    if (w <= 0 || h <= 0) { w = 550; h = 400; }
    soft::Canvas cv((int)(w * scale), (int)(h * scale));
    soft::renderFrame(mv, frame, scale, cv);
    if (!soft::writePng(argv[3], cv)) { printf("write fail\n"); return 1; }
    printf("rendered %s frame=%d size=%dx%d\n", argv[3], frame, cv.w, cv.h);
    return 0;
}
