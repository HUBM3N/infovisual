#pragma once
#include <math.h>

// Frame blending maths, engine-free for tests/fbmath_test.cpp. Exact identities tested:
//   1. still point reprojects onto ITSELF (wall = zero velocity however far camera flew)
//   2. point riding a player reprojects onto where HE was (his step out of 4th column)
namespace fbm
{
    // AngleVectors (mathlib)
    inline void AngleVectors(const float ang[3], float fwd[3], float right[3], float up[3])
    {
        const float k = 3.14159265f / 180.f;
        const float sp = sinf(ang[0] * k), cp = cosf(ang[0] * k);
        const float sy = sinf(ang[1] * k), cy = cosf(ang[1] * k);
        const float sr = sinf(ang[2] * k), cr = cosf(ang[2] * k);

        fwd[0]   = cp * cy;                 fwd[1]   = cp * sy;                 fwd[2]   = -sp;
        right[0] = -sr * sp * cy + cr * sy; right[1] = -sr * sp * sy - cr * cy; right[2] = -sr * cp;
        up[0]    = cr * sp * cy + sr * sy;  up[1]    = cr * sp * sy - sr * cy;  up[2]    = cr * cp;
    }

    // rows right, up, forward (shader order). p = basis * (world - eye) = shader view_pos()
    inline void Basis(const float ang[3], float basis[3][3])
    {
        float fwd[3], right[3], up[3];
        AngleVectors(ang, fwd, right, up);

        for (int a = 0; a < 3; ++a) {
            basis[0][a] = right[a];
            basis[1][a] = up[a];
            basis[2][a] = fwd[a];
        }
    }

    // prev_view^T * cur_view, prev_view^T * (eye_now - eye_then) in 4th col: view point now -> LAST frame
    inline void Reprojection(const float prevBasis[3][3], const float basis[3][3],
                             const float step[3], float rows[3][4])
    {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                rows[r][c] = prevBasis[r][0] * basis[c][0] +
                             prevBasis[r][1] * basis[c][1] +
                             prevBasis[r][2] * basis[c][2];

            rows[r][3] = prevBasis[r][0] * step[0] + prevBasis[r][1] * step[1] + prevBasis[r][2] * step[2];
        }
    }

    inline void Apply(const float rows[3][4], const float p[3], float out[3])
    {
        for (int r = 0; r < 3; ++r)
            out[r] = rows[r][0] * p[0] + rows[r][1] * p[1] + rows[r][2] * p[2] + rows[r][3];
    }

    // camera axes as columns, eye in .w: view -> world (player box test)
    inline void ViewToWorld(const float basis[3][3], const float origin[3], float rows[3][4])
    {
        for (int r = 0; r < 3; ++r) {
            rows[r][0] = basis[0][r];
            rows[r][1] = basis[1][r];
            rows[r][2] = basis[2][r];
            rows[r][3] = origin[r];
        }
    }

    inline void ProjectUv(const float p[3], float tanX, float tanY, float out[2])
    {
        out[0] = (p[0] / (p[2] * tanX)) * 0.5f + 0.5f;
        out[1] = 0.5f - (p[1] / (p[2] * tanY)) * 0.5f;
    }

    // Shutter as POWER of one-frame transform: arc, not linearly stretched chord (wrong direction).
    // Rigid A(x) = Rx + t = screw: rotate about axis through fixed p + slide along axis; A^s = s*angle,
    // s*slide. p from (I - R)p = t, axis outer product added to fix singular direction.
    // Small angle (<~1 deg): p runs to infinity, float rounding jitters a still gun -> linear scale.
    inline void PowerTransform(float rot[3][3], float translation[3], float exponent)
    {
        if (fabsf(exponent - 1.f) < 0.01f) return;

        const float trace = rot[0][0] + rot[1][1] + rot[2][2];

        float cosAngle = (trace - 1.f) * 0.5f;
        if (cosAngle < -1.f) cosAngle = -1.f;
        if (cosAngle >  1.f) cosAngle =  1.f;

        const float angle = acosf(cosAngle);

        float axis[3] = { rot[2][1] - rot[1][2], rot[0][2] - rot[2][0], rot[1][0] - rot[0][1] };

        const float sine = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]) * 0.5f;

        // no turn, or half turn (a switch, not animation): rotation kept, slide only
        if (sine < 1e-6f || angle < 1e-6f) {
            for (int a = 0; a < 3; ++a) translation[a] *= exponent;
            return;
        }

        for (int a = 0; a < 3; ++a) axis[a] /= sine * 2.f;

        // rodrigues at s * angle, same axis
        const float turned = angle * exponent;
        const float co = cosf(turned), si = sinf(turned);

        float scaled[3][3] = {};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                scaled[r][c] = ((r == c) ? co : 0.f) + (1.f - co) * axis[r] * axis[c];

        scaled[0][1] -= si * axis[2];
        scaled[0][2] += si * axis[1];
        scaled[1][0] += si * axis[2];
        scaled[1][2] -= si * axis[0];
        scaled[2][0] -= si * axis[1];
        scaled[2][1] += si * axis[0];

        // slide along axis ((I - R) maps it to zero)
        const float along = axis[0] * translation[0] + axis[1] * translation[1] + axis[2] * translation[2];

        const float across[3] = { translation[0] - axis[0] * along,
                                  translation[1] - axis[1] * along,
                                  translation[2] - axis[2] * along };

        bool  linear = angle < 0.02f;
        float fixed[3] = {};

        if (!linear) {
            // (I - R) + a a^T: invertible
            float solve[3][3] = {};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    solve[r][c] = ((r == c) ? 1.f : 0.f) - rot[r][c] + axis[r] * axis[c];

            const float det = solve[0][0] * (solve[1][1] * solve[2][2] - solve[1][2] * solve[2][1]) -
                              solve[0][1] * (solve[1][0] * solve[2][2] - solve[1][2] * solve[2][0]) +
                              solve[0][2] * (solve[1][0] * solve[2][1] - solve[1][1] * solve[2][0]);

            if (fabsf(det) < 1e-8f) {
                linear = true;
            } else {
                const float inv = 1.f / det;

                // cramer -> fixed point
                for (int col = 0; col < 3; ++col) {
                    float minor[3][3] = {};
                    for (int r = 0; r < 3; ++r)
                        for (int in = 0; in < 3; ++in)
                            minor[r][in] = (in == col) ? across[r] : solve[r][in];

                    fixed[col] = (minor[0][0] * (minor[1][1] * minor[2][2] - minor[1][2] * minor[2][1]) -
                                  minor[0][1] * (minor[1][0] * minor[2][2] - minor[1][2] * minor[2][0]) +
                                  minor[0][2] * (minor[1][0] * minor[2][1] - minor[1][1] * minor[2][0])) * inv;
                }

                // far centre = degenerate case the angle test misses
                if (fixed[0] * fixed[0] + fixed[1] * fixed[1] + fixed[2] * fixed[2] > 4096.f * 4096.f)
                    linear = true;
            }
        }

        // x -> R^s (x - p) + p + s (a . t) a, or straight scaling w/o usable p
        for (int r = 0; r < 3; ++r) {
            translation[r] = linear
                ? translation[r] * exponent
                : fixed[r] + axis[r] * along * exponent -
                  (scaled[r][0] * fixed[0] + scaled[r][1] * fixed[1] + scaled[r][2] * fixed[2]);

            for (int c = 0; c < 3; ++c) rot[r][c] = scaled[r][c];
        }
    }

    // same on 3x4 rows (bone layout)
    inline void PowerRows(float rows[3][4], float exponent)
    {
        float rot[3][3] = {};
        float translation[3] = {};

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) rot[r][c] = rows[r][c];
            translation[r] = rows[r][3];
        }

        PowerTransform(rot, translation, exponent);

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) rows[r][c] = rot[r][c];
            rows[r][3] = translation[r];
        }
    }

    // Bone proxy (world->world) -> view now -> view then, THIS frame's camera both ends:
    //   w = eye + basis^T p;  w' = A (w - c) + c_then;  p' = basis (w' - eye)
    // No world coord in the multiply: eye relative to bone, big numbers cancel BEFORE multiply
    // (after = rounding = streak on still gun).
    inline void BoneRows(const float basis[3][3], const float origin[3],
                         const float rot[3][3], const float center[3], const float centerThen[3],
                         float rows[3][4])
    {
        float worldToView[3][3] = {};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                worldToView[r][c] = rot[r][0] * basis[c][0] + rot[r][1] * basis[c][1] + rot[r][2] * basis[c][2];

        float shift[3] = {};
        for (int a = 0; a < 3; ++a)
            shift[a] = rot[a][0] * (origin[0] - center[0]) +
                       rot[a][1] * (origin[1] - center[1]) +
                       rot[a][2] * (origin[2] - center[2]) + centerThen[a] - origin[a];

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                rows[r][c] = basis[r][0] * worldToView[0][c] +
                             basis[r][1] * worldToView[1][c] +
                             basis[r][2] * worldToView[2][c];

            rows[r][3] = basis[r][0] * shift[0] + basis[r][1] * shift[1] + basis[r][2] * shift[2];
        }
    }
} // namespace fbm
