// Gate test for the GIF frame compositor (src/features/gifdecode.h).
// Build + run:  cmake --build build --config Release --target gif_test && build\Release\gif_test.exe
// The GIF is built in memory, so no file and no device are involved.
#include "../src/features/gifdecode.h"
#include <cstdio>
#include <cstring>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static constexpr uint32_t C0 = 0xFF102030u, C1 = 0xFF405060u, C2 = 0xFF708090u;

struct Frame { std::vector<uint32_t> px; float delay; };

// 4-byte graphics control extension, as giflib stores it in SavedImage::ExtensionBlocks.
struct Gce {
    GifByteType bytes[4];
    ExtensionBlock block;
    Gce(int disposal, int delayCs, int transparent)
    {
        bytes[0] = (GifByteType)((disposal << 2) | (transparent >= 0 ? 1 : 0));
        bytes[1] = (GifByteType)(delayCs & 0xFF);
        bytes[2] = (GifByteType)((delayCs >> 8) & 0xFF);
        bytes[3] = (GifByteType)(transparent >= 0 ? transparent : 0);
        block.ByteCount = 4;
        block.Bytes     = bytes;
        block.Function  = GRAPHICS_EXT_FUNC_CODE;
    }
};

static void SetImage(SavedImage& si, int left, int top, int w, int h, GifByteType* raster, Gce* gce)
{
    std::memset(&si, 0, sizeof(si));
    si.ImageDesc.Left = left; si.ImageDesc.Top = top;
    si.ImageDesc.Width = w;   si.ImageDesc.Height = h;
    si.RasterBits = raster;
    if (gce) { si.ExtensionBlockCount = 1; si.ExtensionBlocks = &gce->block; }
}

int main()
{
    GifColorType colors[3] = { {0x10,0x20,0x30}, {0x40,0x50,0x60}, {0x70,0x80,0x90} };
    ColorMapObject cmap{};
    cmap.ColorCount = 3; cmap.BitsPerPixel = 2; cmap.Colors = colors;

    // f0: whole canvas color 1, 10 cs.
    GifByteType r0[16]; std::memset(r0, 1, sizeof(r0));
    Gce g0(DISPOSE_DO_NOT, 10, -1);
    // f1: 4x4 rect at (2,2), clipped to its top-left 2x2. Index 2 except a transparent 3 at
    // (0,0) and an out-of-palette 9 at (1,0). Restores the previous canvas afterwards.
    GifByteType r1[16]; std::memset(r1, 2, sizeof(r1)); r1[0] = 3; r1[1] = 9;
    Gce g1(DISPOSE_PREVIOUS, 0, 3);
    // f2: 2x1 of color 0 at (0,0), cleared to transparent afterwards.
    GifByteType r2[2] = { 0, 0 };
    Gce g2(DISPOSE_BACKGROUND, 5, -1);
    // f3: one pixel of color 2 at (3,3), no control block at all.
    GifByteType r3[1] = { 2 };

    SavedImage images[4];
    SetImage(images[0], 0, 0, 4, 4, r0, &g0);
    SetImage(images[1], 2, 2, 4, 4, r1, &g1);
    SetImage(images[2], 0, 0, 2, 1, r2, &g2);
    SetImage(images[3], 3, 3, 1, 1, r3, nullptr);

    GifFileType gif{};
    gif.SWidth = 4; gif.SHeight = 4;
    gif.SColorMap = &cmap;
    gif.ImageCount = 4;
    gif.SavedImages = images;

    std::vector<Frame> out;
    auto collect = [&](const uint32_t* px, int w, int h, float delay) {
        out.push_back({ std::vector<uint32_t>(px, px + (size_t)w * h), delay });
        return true;
    };
    auto at = [&](int f, int x, int y) { return out[f].px[(size_t)y * 4 + x]; };

    CHECK(GifCompositeFrames(&gif, kGifMaxBytes, collect) == 4);
    CHECK(out.size() == 4);
    if (out.size() == 4) {
        // f0
        for (uint32_t p : out[0].px) CHECK(p == C1);
        CHECK(out[0].delay == 0.1f);
        // f1: transparent and out-of-palette pixels keep what was under them, clip holds
        CHECK(at(1, 2, 2) == C1);
        CHECK(at(1, 3, 2) == C1);
        CHECK(at(1, 2, 3) == C2);
        CHECK(at(1, 3, 3) == C2);
        CHECK(at(1, 1, 1) == C1);
        CHECK(out[1].delay == 0.02f);   // 0 cs clamps to 20 ms
        // f2: drawn over the restored canvas (f1's pixels gone)
        CHECK(at(2, 0, 0) == C0);
        CHECK(at(2, 1, 0) == C0);
        CHECK(at(2, 3, 3) == C1);
        CHECK(at(2, 2, 3) == C1);
        // f3: background disposal cleared f2's rect, no GCB draws opaque
        CHECK(at(3, 0, 0) == 0u);
        CHECK(at(3, 1, 0) == 0u);
        CHECK(at(3, 2, 0) == C1);
        CHECK(at(3, 3, 3) == C2);
    }

    // byte cap: room for exactly two 4x4 frames
    out.clear();
    CHECK(GifCompositeFrames(&gif, 2 * 16 * 4, collect) == 2);
    CHECK(out.size() == 2);

    // the callback can stop the decode
    out.clear();
    int calls = 0;
    CHECK(GifCompositeFrames(&gif, kGifMaxBytes, [&](const uint32_t*, int, int, float) { return ++calls < 3; }) == 3);

    // rejected canvases
    gif.SWidth = 0;
    CHECK(GifCompositeFrames(&gif, kGifMaxBytes, collect) == 0);
    gif.SWidth = kGifMaxDim + 1;
    CHECK(GifCompositeFrames(&gif, kGifMaxBytes, collect) == 0);
    CHECK(GifCompositeFrames(nullptr, kGifMaxBytes, collect) == 0);

    // a frame with no raster is skipped, not dereferenced
    gif.SWidth = 4;
    images[0].RasterBits = nullptr;
    out.clear();
    CHECK(GifCompositeFrames(&gif, kGifMaxBytes, collect) == 3);

    if (s_failed) { std::printf("%d check(s) failed\n", s_failed); return 1; }
    std::printf("gif_test: all checks passed\n");
    return 0;
}
