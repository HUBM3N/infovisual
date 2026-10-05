// Gate test for minesweeper rules (src/features/minesweeper.h).
// Build + run:  cmake --build build --config Release --target minesweeper_test && build\Release\minesweeper_test.exe
#include "../src/features/minesweeper.h"
#include <cstdio>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

using namespace MsGame;

static int CountMines(const Board& b) {
    int n = 0;
    for (int r = 0; r < b.h; r++) for (int c = 0; c < b.w; c++) n += b.c[r][c].mine;
    return n;
}

// Board with mines at the given cells, placed as if the first click was elsewhere.
static void Rig(Board& b, int diff, std::initializer_list<std::pair<int,int>> mines) {
    b.Init(diff);
    for (auto& m : mines) b.c[m.first][m.second].mine = true;
    for (int r = 0; r < b.h; r++) for (int c = 0; c < b.w; c++) {
        int k = 0;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++)
            if (b.In(r + dr, c + dc) && b.c[r + dr][c + dc].mine) k++;
        b.c[r][c].adj = (uint8_t)k;
    }
    b.mines = (int)mines.size();
    b.state = PLAYING;
}

int main()
{
    // Every difficulty, many seeds, corner + center first clicks: exact mine count,
    // 3x3 around the click stays clear so the first click always opens an area.
    for (int d = 0; d < 3; d++)
        for (uint32_t seed = 1; seed < 200; seed++) {
            Board b; b.Init(d);
            int sr = (int)(seed % b.h), sc = (int)((seed * 7) % b.w);
            b.Reveal(sr, sc, seed);
            CHECK(CountMines(b) == kDims[d][2]);
            for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++)
                if (b.In(sr + dr, sc + dc)) CHECK(!b.c[sr + dr][sc + dc].mine);
            CHECK(b.c[sr][sc].revealed && b.c[sr][sc].adj == 0);
            CHECK(b.state == PLAYING || b.state == WIN);
        }

    // Bad difficulty falls back to intermediate instead of indexing past kDims
    { Board b; b.Init(7); CHECK(b.w == 16 && b.h == 16 && b.mines == 40); }

    // Flood fill stops at numbers, flags are respected, counts stay right
    {
        Board b; Rig(b, 0, { {0, 0} });
        b.Flag(8, 8);
        b.Reveal(4, 4, 1);
        CHECK(!b.c[8][8].revealed);            // flagged cell not opened
        CHECK(b.c[0][1].revealed && b.c[0][1].adj == 1);
        CHECK(!b.c[0][0].revealed);
        CHECK(b.revealed == 81 - 1 - 1);       // all but mine + flagged cell
        CHECK(b.state == PLAYING);
        b.Flag(8, 8);                          // unflag, open last safe cell -> win
        CHECK(b.flags == 0);
        b.Reveal(8, 8, 1);
        CHECK(b.state == WIN && b.c[0][0].flagged && b.flags == 1);
        b.Flag(0, 0);                          // no edits after game over
        CHECK(b.c[0][0].flagged && b.flags == 1);
    }

    // Hitting a mine: lose, boom cell recorded, other mines shown, flags left alone
    {
        Board b; Rig(b, 0, { {0, 0}, {5, 5} });
        b.Flag(5, 5);
        b.Flag(3, 3);                          // wrong flag stays a flag (drawn as wrong on loss)
        b.Reveal(0, 0, 1);
        CHECK(b.state == LOSE && b.boomR == 0 && b.boomC == 0);
        CHECK(!b.c[5][5].revealed && b.c[5][5].flagged);
        CHECK(b.c[3][3].flagged && !b.c[3][3].mine);
        b.Reveal(8, 8, 1);                     // no reveals after game over
        CHECK(!b.c[8][8].revealed);
    }

    // Chord: needs exactly adj flags; right flags open neighbors, wrong flag loses
    {
        Board b; Rig(b, 0, { {0, 0} });
        b.Reveal(1, 1, 1);
        CHECK(b.c[1][1].revealed && b.c[1][1].adj == 1);
        b.Chord(1, 1, 1);                      // 0 flags != 1: nothing
        CHECK(!b.c[0][1].revealed);
        b.Flag(0, 0);
        b.Chord(1, 1, 1);
        CHECK(b.c[0][1].revealed && b.c[1][0].revealed && b.state == WIN);
    }
    {
        Board b; Rig(b, 0, { {0, 0} });
        b.Reveal(1, 1, 1);
        b.Flag(0, 1);                          // wrong neighbor flagged
        b.Chord(1, 1, 1);
        CHECK(b.state == LOSE && b.boomR == 0 && b.boomC == 0);
    }

    // Revealed cells can't be flagged; counter goes negative with extra flags
    {
        Board b; Rig(b, 0, { {0, 0} });
        b.Reveal(1, 1, 1);
        b.Flag(1, 1);
        CHECK(!b.c[1][1].flagged && b.flags == 0);
        b.Init(0);
        for (int i = 0; i < 12; i++) b.Flag(i / 9, i % 9);
        CHECK(b.mines - b.flags == -2);
    }

    std::printf(s_failed ? "minesweeper_test: %d FAILED\n" : "minesweeper_test: all passed\n", s_failed);
    return s_failed ? 1 : 0;
}
