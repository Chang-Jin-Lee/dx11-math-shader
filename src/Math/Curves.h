//***************************************************************************************
// Curves.h
//
// Scene02용 보간/곡선 함수 모음 — 모두 순수 함수.
//  - 선형/이징 보간
//  - 2차·3차 Bezier
//  - Hermite
//  - Catmull-Rom (Hermite로 환원)
//***************************************************************************************
#pragma once
#include <DirectXMath.h>
#include <cmath>

namespace curve
{
    using DirectX::XMFLOAT2;

    inline XMFLOAT2 Lerp(const XMFLOAT2& a, const XMFLOAT2& b, float t)
    {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    }

    // ---- 스칼라 이징 (0..1 → 0..1) ----
    inline float EaseLinear(float t)    { return t; }
    inline float EaseSmoothstep(float t){ return t * t * (3.0f - 2.0f * t); }
    inline float EaseSineIn(float t)    { return std::sin(t * DirectX::XM_PIDIV2); }          // ease-out 느낌
    inline float EaseSineInOut(float t) { return (1.0f - std::cos(t * DirectX::XM_PI)) * 0.5f; }

    // ---- 2차 Bezier ----
    inline XMFLOAT2 QuadraticBezier(const XMFLOAT2& p0, const XMFLOAT2& p1, const XMFLOAT2& p2, float t)
    {
        float u = 1.0f - t;
        return { u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
                 u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y };
    }

    // ---- 3차 Bezier ----
    inline XMFLOAT2 CubicBezier(const XMFLOAT2& p0, const XMFLOAT2& p1, const XMFLOAT2& p2, const XMFLOAT2& p3, float t)
    {
        float u = 1.0f - t;
        float uu = u * u, tt = t * t;
        return {
            uu * u * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + tt * t * p3.x,
            uu * u * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + tt * t * p3.y };
    }

    // ---- Hermite (끝점 p0,p1 / 접선 m0,m1) ----
    inline XMFLOAT2 Hermite(const XMFLOAT2& p0, const XMFLOAT2& m0, const XMFLOAT2& p1, const XMFLOAT2& m1, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        float h00 = 2 * t3 - 3 * t2 + 1;
        float h10 = t3 - 2 * t2 + t;
        float h01 = -2 * t3 + 3 * t2;
        float h11 = t3 - t2;
        return {
            h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x,
            h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y };
    }

    // ---- Catmull-Rom (p0~p3, 구간 p1→p2) ----
    inline XMFLOAT2 CatmullRom(const XMFLOAT2& p0, const XMFLOAT2& p1, const XMFLOAT2& p2, const XMFLOAT2& p3, float t)
    {
        XMFLOAT2 m1 = { (p2.x - p0.x) * 0.5f, (p2.y - p0.y) * 0.5f };
        XMFLOAT2 m2 = { (p3.x - p1.x) * 0.5f, (p3.y - p1.y) * 0.5f };
        return Hermite(p1, m1, p2, m2, t);
    }
}
