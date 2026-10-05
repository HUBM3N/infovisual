#pragma once
#include "../globals.h"

// Recolor TF2 net_graph text (needs net_graph 1+). Colors compiled into client.dll,
// only way in = IMatSystemSurface::DrawColoredText slot 162.
void NetGraph_Install();   // creates the detour, leaves it disarmed
void NetGraph_Tick();      // arms/disarms it with the toggle
void NetGraph_Shutdown();  // drops the detour on eject
