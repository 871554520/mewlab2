// SWF 解析（L0）：文件头、tag 流、位图、形状路径、帧显示列表
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace swf {

struct Rect { double x0=0,y0=0,x1=0,y1=0; };
struct Matrix { double a=1,b=0,c=0,d=1,tx=0,ty=0; };

struct Bitmap {
    int id=0; int width=0, height=0;
    bool isJpeg=false;
    std::vector<uint8_t> jpeg;        // DefineBitsJPEG2/3 原始 JPEG
    std::vector<uint8_t> rgba;        // DefineBitsLossless 解出的 RGBA
    std::vector<uint8_t> alpha;       // DefineBitsJPEG3 的 alpha 通道
};

struct GradientStop { uint8_t r=0,g=0,b=0,a=255; uint8_t ratio=0; };

struct FillStyle {
    enum Kind { Solid=0, Bitmap=1, LinearGradient=2, RadialGradient=3 } kind=Solid;
    uint8_t r=0,g=0,b=0,a=255;
    int bitmapId=0;
    Matrix matrix;
    std::vector<GradientStop> stops;
};

struct ShapePath {
    int fill0 = -1, fill1 = -1;       // 左右两种填充（仅取 fill0）
    std::vector<std::pair<double,double>> pts;  // 已展平的折线
};

struct Shape {
    int id=0;
    Rect bounds;
    std::vector<FillStyle> fills;
    std::vector<ShapePath> paths;
};

struct Placement {
    int depth=0; int characterId=0; Matrix matrix; int ratio=0;
    int clipDepth=0; bool hasClip=false; bool remove=false;
};

struct Frame {
    std::vector<Placement> ops;       // 按出现顺序的显示列表操作
    std::string label;
    std::vector<uint8_t> actions;     // DoAction 字节码（AVM1）
};

struct Sprite;

struct Sprite { int id=0; std::vector<Frame> frames; };

struct ButtonRecord {
    int characterId=0; int depth=0; uint8_t stateFlags=0; Matrix matrix;
};

struct ButtonAction {
    uint16_t conditions=0;            // 低 8 位：1=over 2=up 3=down 4=hit
    std::vector<uint8_t> code;
};

struct Button {
    int id=0;
    std::vector<ButtonRecord> records;
    std::vector<ButtonAction> actions;
};

struct Movie {
    std::string name;
    Rect frameSize;
    double frameRate=24;
    int frameCount=0;
    uint32_t backgroundColor=0xFFFFFF;
    std::map<int,Bitmap> bitmaps;
    std::map<int,Shape> shapes;
    std::map<int,Sprite> sprites;
    std::map<int,Button> buttons;
    std::vector<Frame> frames;
    std::vector<std::vector<uint8_t>> initActions;   // DoInitAction（按定义顺序）
    std::map<std::string,int> tagHistogram;
    std::string error;
    bool loaded=false;
};

bool parse(const std::string& path, Movie& out, std::string* err=nullptr);

// 命中测试：点是否在按钮的可见记录范围内（应用 placement 与记录矩阵）
bool hitTestButton(const Movie& mv, const Button& btn, double px, double py,
                   double placeA=1, double placeB=0, double placeC=0, double placeD=1,
                   double placeTx=0, double placeTy=0);
bool parseMemory(const std::vector<uint8_t>& raw, Movie& out, std::string* err=nullptr);

} // namespace swf
