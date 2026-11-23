#include "swf.hpp"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    swf::Movie mv; std::string err;
    if (!swf::parse(argv[1], mv, &err)) { printf("%s\tERR\t%s\n", argv[1], err.c_str()); return 1; }
    size_t paths=0, fills=0, grads=0, spriteFrames=0, clips=0, actions=0, btnActs=0;
    for (auto& kv : mv.shapes) {
        paths += kv.second.paths.size();
        fills += kv.second.fills.size();
        for (auto& f : kv.second.fills)
            if (f.kind==swf::FillStyle::LinearGradient||f.kind==swf::FillStyle::RadialGradient) grads++;
    }
    for (auto& kv : mv.sprites) spriteFrames += kv.second.frames.size();
    for (auto& f : mv.frames) { if (!f.actions.empty()) actions++; for (auto& op : f.ops) if (op.hasClip) clips++; }
    for (auto& kv : mv.buttons) btnActs += kv.second.actions.size();
    printf("%s\tOK\tframes=%zu\tshapes=%zu\tbitmaps=%zu\tsprites=%zu\tbuttons=%zu\tgradients=%zu\tclips=%zu\tframeActions=%zu\tbtnActions=%zu\n",
           argv[1], mv.frames.size(), mv.shapes.size(), mv.bitmaps.size(), mv.sprites.size(),
           mv.buttons.size(), grads, clips, actions, btnActs);
    return 0;
}
