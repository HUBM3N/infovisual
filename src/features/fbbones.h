#pragma once
#include <atomic>
#include <math.h>
#include <string.h>

// Gun animation as velocity (depth reproj only sees camera + entity pose, not bones).
// Ported from coldtires (viewmodel_bones.h). PreviousBoneMatrices-style: no velocity-target shader
// in TF2, so bone transforms go up as CONSTANTS, gun pixel blends nearest (fx_fb_velocity.hlsl).
// Picks fixed ONCE per model (farthest point sampling): re-picking per frame = jitter.
// Weapon + arms drawn separately: weapon 10 slots, arms 6, else hand eats them all.
// Ride (bob/lag/kick) removed HERE on draw thread vs the pose these bones were built from;
// later on another thread pairs frame N bones w/ N+1 ride -> gun smears on every mouse move.
namespace FbBones
{
    struct Proxy {
        float center[3]     = { 0.f, 0.f, 0.f };   // world pos THIS frame

        // LAST frame pos, stored not rebuilt: world-coord multiply rounds to pixels of streak
        float centerThen[3] = { 0.f, 0.f, 0.f };

        // world now -> world then, ride removed (pure rider = identity exactly)
        float rot[3][3]     = {};
    };

    // 16 = 4 const regs each, pass one unrolls all
    inline constexpr int kMaxProxies    = 16;
    inline constexpr int kWeaponProxies = 10;
    inline constexpr int kOtherProxies  = kMaxProxies - kWeaponProxies;

    struct Set {
        int   count  = 0;
        int   frame  = -1;
        int   weapon = 0;    // how many are WEAPON's, rest arms
        Proxy proxy[kMaxProxies] = {};
    };

    // ModelRenderInfo_t, x64 (public/ivmodelrender.h): origin 0x00, angles 0x0C,
    // pRenderable 0x18, pModel 0x20, pModelToWorld 0x28, flags 0x40, entity_index 0x44.
    // DrawModelState_t opens with studiohdr_t* (ivmodelrender.h:38), and studiohdr_t::numbones
    // is at 0x9C (public/studio.h: id/version/checksum/name[64]/length/6 Vectors/flags).
    inline constexpr size_t kMriOrigin       = 0x00;
    inline constexpr size_t kMriAngles       = 0x0C;
    inline constexpr size_t kMriModel        = 0x20;
    inline constexpr size_t kMriModelToWorld = 0x28;
    inline constexpr size_t kStudioNumBones  = 0x9C;

    inline constexpr int kMaxBones = 192;
    inline constexpr int kMaxSlots = 6;

    // matrix3x4_t, row major, translation in [row][3]
    struct Mat34 { float m[3][4]; };

    namespace detail
    {
        struct Slot {
            const void* model = nullptr;

            int  frame = -1;
            int  bones = 0;
            bool weapon = false;

            int picks = 0;               // chosen once, never touched again
            int pick[kMaxProxies] = {};

            bool  haveRide = false;      // last frame's pose for this part
            float ride[3][3] = {};
            float rideOrigin[3] = {};

            Mat34 bone[kMaxBones] = {};
        };

        struct State {
            Slot slot[kMaxSlots] = {};

            float distance[kMaxBones] = {};   // FPS scratch: dist to nearest picked

            // kept apart so arms never eat gun's share
            Proxy weaponProxy[kWeaponProxies] = {};
            Proxy otherProxy[kOtherProxies]   = {};

            int weaponCount = 0;
            int otherCount  = 0;
            int frame       = -1;

            Set published[2] = {};   // draws write one, screen pass reads other
            std::atomic<int> ready{ -1 };
        };

        inline State& S() { static State s; return s; }

        inline float SquaredBones(const Mat34& a, const Mat34& b)
        {
            const float d[3] = { a.m[0][3] - b.m[0][3], a.m[1][3] - b.m[1][3], a.m[2][3] - b.m[2][3] };
            return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        }

        // Farthest point sampling on positions, root seeded: spreads proxies over whole part.
        // Runs once on whatever pose; SET stability matters, not which pose chose it.
        inline void Choose(Slot& slot, const Mat34* bones, int boneCount)
        {
            State& s = S();

            int quota = slot.weapon ? kWeaponProxies : kOtherProxies;
            if (quota > boneCount) quota = boneCount;

            slot.pick[0] = 0;
            slot.picks   = 1;

            for (int b = 0; b < boneCount; ++b)
                s.distance[b] = SquaredBones(bones[b], bones[0]);

            while (slot.picks < quota) {
                int best = 0;
                for (int b = 1; b < boneCount; ++b)
                    if (s.distance[b] > s.distance[best]) best = b;

                // rest sit on picked bones: duplicates buy nothing
                if (s.distance[best] < 0.25f) break;

                slot.pick[slot.picks++] = best;

                for (int b = 0; b < boneCount; ++b) {
                    const float d = SquaredBones(bones[b], bones[best]);
                    if (d < s.distance[b]) s.distance[b] = d;
                }
            }
        }

        // Picked bones, last vs this frame, ride out. A = M_then * M_now^-1; pure rider has
        // A = Ride_then * Ride_now^-1, so A_local = (Ride_now * Ride_then^-1) * A = D * A
        // (rider -> identity). D from these bones' own poses: no other thread/frame.
        inline void Emit(const Slot& slot, const Mat34* bones,
                         const float ride[3][3], const float rideOrigin[3])
        {
            State& s = S();

            float d[3][3] = {};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    d[r][c] = ride[r][0] * slot.ride[c][0] + ride[r][1] * slot.ride[c][1] +
                              ride[r][2] * slot.ride[c][2];

            for (int i = 0; i < slot.picks; ++i) {
                const int b = slot.pick[i];

                Proxy* target = nullptr;
                if (slot.weapon) {
                    if (s.weaponCount < kWeaponProxies) target = &s.weaponProxy[s.weaponCount++];
                } else if (s.otherCount < kOtherProxies) {
                    target = &s.otherProxy[s.otherCount++];
                }
                if (!target) return;

                const Mat34& now  = bones[b];
                const Mat34& then = slot.bone[b];

                // R_then * R_now^T (ride included)
                float rot[3][3] = {};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        rot[r][c] = then.m[r][0] * now.m[c][0] + then.m[r][1] * now.m[c][1] +
                                    then.m[r][2] * now.m[c][2];

                // D * A = animation only
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        target->rot[r][c] = d[r][0] * rot[0][c] + d[r][1] * rot[1][c] + d[r][2] * rot[2][c];

                // c_then' = o_now + D (c_then - o_then); relative to gun origin -> no world-size rounding
                for (int a = 0; a < 3; ++a) {
                    target->center[a] = now.m[a][3];
                    target->centerThen[a] = rideOrigin[a] +
                        d[a][0] * (then.m[0][3] - slot.rideOrigin[0]) +
                        d[a][1] * (then.m[1][3] - slot.rideOrigin[1]) +
                        d[a][2] * (then.m[2][3] - slot.rideOrigin[2]);
                }

                const float step[3] = { target->centerThen[0] - now.m[0][3],
                                        target->centerThen[1] - now.m[1][3],
                                        target->centerThen[2] - now.m[2][3] };

                // switch/respawn/taunt jump: FLATTEN to identity, never drop (list shape change = jitter)
                if (step[0] * step[0] + step[1] * step[1] + step[2] * step[2] > 24.f * 24.f) {
                    memset(target->rot, 0, sizeof(target->rot));
                    for (int a = 0; a < 3; ++a) {
                        target->rot[a][a] = 1.f;
                        target->centerThen[a] = target->center[a];
                    }
                }
            }
        }

        // weapon first: overflow never costs gun a slot
        inline void Publish(int frame)
        {
            State& s = S();

            const int ready = s.ready.load(std::memory_order_relaxed);
            const int next  = (ready == 0) ? 1 : 0;

            Set& out = s.published[next];

            int count = 0;
            for (int i = 0; i < s.weaponCount && count < kMaxProxies; ++i) out.proxy[count++] = s.weaponProxy[i];
            for (int i = 0; i < s.otherCount  && count < kMaxProxies; ++i) out.proxy[count++] = s.otherProxy[i];

            out.count  = count;
            out.frame  = frame;
            out.weapon = (s.weaponCount < count) ? s.weaponCount : count;

            s.ready.store(next, std::memory_order_release);
        }

        // AngleMatrix (mathlib): cols fwd/left/up; only compared to itself, convention cancels
        inline void AngleMatrix(const float ang[3], float out[3][3])
        {
            const float k = 3.14159265f / 180.f;
            const float sp = sinf(ang[0] * k), cp = cosf(ang[0] * k);
            const float sy = sinf(ang[1] * k), cy = cosf(ang[1] * k);
            const float sr = sinf(ang[2] * k), cr = cosf(ang[2] * k);

            out[0][0] = cp * cy;                 out[1][0] = cp * sy;                 out[2][0] = -sp;
            out[0][1] = sr * sp * cy - cr * sy;  out[1][1] = sr * sp * sy + cr * cy;  out[2][1] = sr * cp;
            out[0][2] = cr * sp * cy + sr * sy;  out[1][2] = cr * sp * sy - sr * cy;  out[2][2] = cr * cp;
        }

        // game memory reads isolated: __try can't share frame w/ destructors (C2712)
        __declspec(noinline) inline bool ReadDraw(const void* pState, const void* pInfo,
                                                  int& boneCount, const void*& model,
                                                  float ride[3][3], float rideOrigin[3])
        {
            __try {
                const void* hdr = *reinterpret_cast<const void* const*>(pState);
                if (reinterpret_cast<uintptr_t>(hdr) < 0x10000) return false;

                boneCount = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(hdr) + kStudioNumBones);
                if (boneCount <= 0 || boneCount > kMaxBones) return false;

                const uint8_t* info = reinterpret_cast<const uint8_t*>(pInfo);

                model = *reinterpret_cast<const void* const*>(info + kMriModel);
                if (reinterpret_cast<uintptr_t>(model) < 0x10000) return false;

                // pose these bones were built from, here only. pModelToWorld preferred (renderer's
                // own), origin/angles fallback.
                const float* m2w = *reinterpret_cast<const float* const*>(info + kMriModelToWorld);

                if (reinterpret_cast<uintptr_t>(m2w) >= 0x10000) {
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 3; ++c) ride[r][c] = m2w[r * 4 + c];
                        rideOrigin[r] = m2w[r * 4 + 3];
                    }
                } else {
                    const float* o = reinterpret_cast<const float*>(info + kMriOrigin);
                    const float* a = reinterpret_cast<const float*>(info + kMriAngles);
                    const float angles[3] = { a[0], a[1], a[2] };
                    AngleMatrix(angles, ride);
                    rideOrigin[0] = o[0]; rideOrigin[1] = o[1]; rideOrigin[2] = o[2];
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }

        __declspec(noinline) inline bool CopyBones(const void* pBones, Mat34* out, int boneCount)
        {
            __try {
                memcpy(out, pBones, sizeof(Mat34) * (size_t)boneCount);
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }
    } // namespace detail

    // DME detour, once per first-person part per frame. weapon = c_ attachment (entity_index -1)
    // vs v_models/_arms stack.
    inline void Collect(const void* pState, const void* pInfo, const void* pBones,
                        bool weapon, int frame)
    {
        if (!pState || !pInfo || !pBones) return;

        detail::State& s = detail::S();

        int         boneCount = 0;
        const void* model     = nullptr;
        float       ride[3][3] = {};
        float       rideOrigin[3] = {};

        if (!detail::ReadDraw(pState, pInfo, boneCount, model, ride, rideOrigin)) return;

        // new frame: last list already published
        if (frame != s.frame) {
            s.frame       = frame;
            s.weaponCount = 0;
            s.otherCount  = 0;
        }

        detail::Slot* slot = nullptr;
        for (auto& e : s.slot) {
            if (e.model == model) { slot = &e; break; }
        }

        if (!slot) {
            slot = &s.slot[0];
            for (auto& e : s.slot)
                if (e.frame < slot->frame) slot = &e;

            slot->model    = model;
            slot->frame    = -1;
            slot->bones    = 0;
            slot->picks    = 0;
            slot->haveRide = false;
            slot->weapon   = weapon;
        }

        // same part twice in a frame: one measurement, else frame vs itself
        if (slot->frame == frame) return;

        static Mat34 s_now[kMaxBones];   // draw thread only; 36KB off the stack
        if (!detail::CopyBones(pBones, s_now, boneCount)) return;

        // new skeleton (first sight / lod change): pick once
        if (slot->picks <= 0 || slot->bones != boneCount)
            detail::Choose(*slot, s_now, boneCount);

        if (slot->frame == frame - 1 && slot->bones == boneCount && slot->haveRide)
            detail::Emit(*slot, s_now, ride, rideOrigin);

        memcpy(slot->bone, s_now, sizeof(Mat34) * (size_t)boneCount);

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) slot->ride[r][c] = ride[r][c];
            slot->rideOrigin[r] = rideOrigin[r];
        }

        slot->haveRide = true;
        slot->bones    = boneCount;
        slot->frame    = frame;

        // publish after EVERY part (no end-of-frame callback); all parts draw before gun pass
        if (s.weaponCount > 0 || s.otherCount > 0) detail::Publish(frame);
    }

    // copy: draws run on matsys thread when queued
    inline Set Read()
    {
        detail::State& s = detail::S();
        const int ready = s.ready.load(std::memory_order_acquire);
        if (ready < 0) return Set{};
        return s.published[ready];
    }

    // map change recycles model_t addresses: forget slots or stale pose pairs w/ new skeleton
    inline void Flush()
    {
        detail::State& s = detail::S();
        for (auto& e : s.slot) { e.model = nullptr; e.frame = -1; e.picks = 0; e.haveRide = false; }
        s.weaponCount = 0;
        s.otherCount  = 0;
        s.frame       = -1;
        s.ready.store(-1, std::memory_order_release);
    }
} // namespace FbBones
