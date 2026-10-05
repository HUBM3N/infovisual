#pragma once
// Animated GIF frame compositor: palette lookup, transparency, disposal, clipping and size caps.
// Pure CPU so tests/gif_test.cpp can run it without a device; LoadGifFrames (drawutils.cpp)
// uploads what it produces. Three loaders used to carry their own copy of this loop, two of them
// indexing the palette with no bounds check.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../giflib-6.1.3/gif_lib.h"

constexpr int    kGifMaxDim   = 16384;
constexpr size_t kGifMaxBytes = 512u * 1024u * 1024u;   // a long 1080p GIF is one texture per frame

// Calls onFrame(const uint32_t* argb, int w, int h, float delaySeconds) once per composited frame
// of an already-slurped GIF; onFrame returns false to stop. Stops by itself before the frames it
// has emitted would pass maxBytes (w*h*4 each). Returns the number of frames emitted, 0 when the
// canvas is rejected (empty, or larger than kGifMaxDim on a side).
template <class OnFrame>
inline int GifCompositeFrames(GifFileType* gif, size_t maxBytes, OnFrame&& onFrame)
{
    if (!gif) return 0;
    const int W = gif->SWidth, H = gif->SHeight;
    if (W <= 0 || H <= 0 || W > kGifMaxDim || H > kGifMaxDim || gif->ImageCount <= 0) return 0;
    const size_t frameBytes = (size_t)W * (size_t)H * 4u;

    std::vector<uint32_t> canvas((size_t)W * H, 0u);
    std::vector<uint32_t> saved;
    int emitted = 0;

    for (int fi = 0; fi < gif->ImageCount; ++fi) {
        if ((size_t)(emitted + 1) * frameBytes > maxBytes) break;

        const SavedImage&     si   = gif->SavedImages[fi];
        const GifImageDesc&   id   = si.ImageDesc;
        const ColorMapObject* cmap = id.ColorMap ? id.ColorMap : gif->SColorMap;
        if (!cmap || !si.RasterBits || id.Width <= 0 || id.Height <= 0) continue;

        GraphicsControlBlock gcb{};
        gcb.TransparentColor = NO_TRANSPARENT_COLOR;
        DGifSavedExtensionToGCB(gif, fi, &gcb);   // leaves the defaults when there is no GCB

        // The frame rect clipped to the canvas.
        const int x0 = std::max(id.Left, 0), y0 = std::max(id.Top, 0);
        const int x1 = std::min(id.Left + id.Width, W), y1 = std::min(id.Top + id.Height, H);

        if (gcb.DisposalMode == DISPOSE_PREVIOUS) saved = canvas;

        for (int cy = y0; cy < y1; ++cy)
            for (int cx = x0; cx < x1; ++cx) {
                const int idx = si.RasterBits[(size_t)(cy - id.Top) * id.Width + (cx - id.Left)];
                if (idx == gcb.TransparentColor || idx >= cmap->ColorCount) continue;
                const GifColorType& c = cmap->Colors[idx];
                canvas[(size_t)cy * W + cx] = 0xFF000000u | ((uint32_t)c.Red << 16)
                                            | ((uint32_t)c.Green << 8) | c.Blue;
            }

        ++emitted;
        if (!onFrame(canvas.data(), W, H, std::max((float)gcb.DelayTime / 100.f, 0.02f)))   // centiseconds
            break;

        if (gcb.DisposalMode == DISPOSE_BACKGROUND) {
            // The spec says background color; transparent keeps overlays from flashing a fill.
            for (int cy = y0; cy < y1; ++cy)
                std::fill(canvas.begin() + (size_t)cy * W + x0, canvas.begin() + (size_t)cy * W + x1, 0u);
        } else if (gcb.DisposalMode == DISPOSE_PREVIOUS) {
            canvas.swap(saved);
        }
    }
    return emitted;
}
