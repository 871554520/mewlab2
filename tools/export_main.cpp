// CI 用：把 SWF 里所有导出的 Shape 角色逐张渲成 PNG
#include "swf.hpp"
#include "softrender.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: export swf outdir scale [max]\n"); return 2; }
    swf::Movie mv; std::string err;
    if (!swf::parse(argv[1], mv, &err)) { printf("ERR %s\n", err.c_str()); return 1; }
    std::string dir = argv[2];
    double scale = std::atof(argv[3]);
    int maxn = argc > 4 ? std::atoi(argv[4]) : 40;
    int n = 0;
    for (auto& kv : mv.exports) {
        if (n >= maxn) break;
        auto shIt = mv.shapes.find(kv.first);
        if (shIt == mv.shapes.end()) continue;
        std::string safe;
        for (char c : kv.second) safe.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/%03d_%s.png", dir.c_str(), kv.first, safe.c_str());
        soft::Canvas cv(1, 1);
        if (!soft::renderCharacter(mv, kv.first, scale, cv)) continue;
        if (cv.w > 2000 || cv.h > 2000) continue;
        if (!soft::writePng(path, cv)) continue;
        printf("%s\t%s\t%dx%d\n", path, kv.second.c_str(), cv.w, cv.h);
        n++;
    }
    printf("exported=%d\n", n);
    return 0;
}
