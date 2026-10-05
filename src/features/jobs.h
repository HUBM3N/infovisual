#pragma once
#include "../globals.h"
#include <functional>

// Background worker pool for CPU-only work that would otherwise stall the
// render thread -- SVG rasterization, blur kernels, and anything else that
// touches no engine or D3D state.
//
// Nothing submitted here may call into the engine, into D3D9, or into ImGui.
// Those are all render-thread-only. The pattern is: worker produces a plain
// pixel buffer, render thread turns it into a texture.
//
// Workers run below normal priority and leave two cores for the game, so a
// burst of jobs can't take frame time away from TF2's own threads.
namespace Jobs
{
    void Start();
    void Stop();                              // drains, joins, safe to call twice
    void Submit(std::function<void()> fn);
    int  WorkerCount();
    int  Pending();                           // queued, not yet started
}
