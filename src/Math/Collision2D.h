//***************************************************************************************
// Collision2D.h
//
// Scene01에서 사용하는 2D 게임수학 기초 함수 모음.
//  - AABB / OBB 충돌(SAT, 분리축 정리)
//  - 벡터 반사
//  - 볼록 다각형 내부 판별
//
// 모든 함수는 순수 함수(렌더링/상태 의존 없음)라 그대로 단위 테스트가 가능하다.
//***************************************************************************************
#pragma once
#include <DirectXMath.h>
#include <cmath>

namespace math2d
{
    using DirectX::XMFLOAT2;

    // ------------------------------------------------------------
    // 작은 2D 벡터 헬퍼 (XMFLOAT2 기반, 가독성 목적)
    // ------------------------------------------------------------
    inline XMFLOAT2 Add(const XMFLOAT2& a, const XMFLOAT2& b) { return { a.x + b.x, a.y + b.y }; }
    inline XMFLOAT2 Sub(const XMFLOAT2& a, const XMFLOAT2& b) { return { a.x - b.x, a.y - b.y }; }
    inline XMFLOAT2 Mul(const XMFLOAT2& a, float s)           { return { a.x * s,   a.y * s }; }
    inline float    Dot(const XMFLOAT2& a, const XMFLOAT2& b) { return a.x * b.x + a.y * b.y; }
    // 2D 외적의 z성분 (스칼라). 점이 선분의 어느 쪽에 있는지 판별에 사용
    inline float    Cross(const XMFLOAT2& a, const XMFLOAT2& b) { return a.x * b.y - a.y * b.x; }
    inline float    Length(const XMFLOAT2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
    inline XMFLOAT2 Normalize(const XMFLOAT2& a)
    {
        float len = Length(a);
        if (len < 1e-6f) return { 0.0f, 0.0f };
        return { a.x / len, a.y / len };
    }
    // 90도 회전(법선 생성용)
    inline XMFLOAT2 Perp(const XMFLOAT2& a) { return { -a.y, a.x }; }

    // ------------------------------------------------------------
    // AABB (축 정렬 경계 상자)
    // ------------------------------------------------------------
    struct AABB
    {
        XMFLOAT2 min;
        XMFLOAT2 max;
    };

    // 두 AABB가 겹치는가: 모든 축(x, y)에서 구간이 겹치면 충돌
    inline bool IntersectAABB(const AABB& a, const AABB& b)
    {
        return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
               (a.min.y <= b.max.y && a.max.y >= b.min.y);
    }

    // ------------------------------------------------------------
    // OBB (방향성 경계 상자) — SAT(분리축 정리)
    // ------------------------------------------------------------
    struct OBB
    {
        XMFLOAT2 center;
        XMFLOAT2 halfExtents;
        float    rotation;   // 라디안
    };

    // OBB의 네 꼭짓점을 반시계 방향으로 채운다
    inline void OBBCorners(const OBB& o, XMFLOAT2 out[4])
    {
        float c = std::cos(o.rotation);
        float s = std::sin(o.rotation);
        // 로컬 축 (회전된 x, y)
        XMFLOAT2 ax = { c, s };          // local +x
        XMFLOAT2 ay = { -s, c };         // local +y
        XMFLOAT2 ex = Mul(ax, o.halfExtents.x);
        XMFLOAT2 ey = Mul(ay, o.halfExtents.y);
        out[0] = Add(o.center, Add(Mul(ex, -1.0f), Mul(ey, -1.0f)));
        out[1] = Add(o.center, Add(ex,             Mul(ey, -1.0f)));
        out[2] = Add(o.center, Add(ex,             ey));
        out[3] = Add(o.center, Add(Mul(ex, -1.0f), ey));
    }

    // 꼭짓점들을 축 ax에 투영해 [min, max] 구간을 구한다
    inline void ProjectPoints(const XMFLOAT2* pts, int n, const XMFLOAT2& ax,
                              float& outMin, float& outMax)
    {
        outMin = outMax = Dot(pts[0], ax);
        for (int i = 1; i < n; ++i)
        {
            float p = Dot(pts[i], ax);
            if (p < outMin) outMin = p;
            if (p > outMax) outMax = p;
        }
    }

    // 두 OBB가 겹치는가: 두 박스의 축 4개 중 하나라도 분리되면 미충돌
    inline bool IntersectOBB(const OBB& a, const OBB& b)
    {
        XMFLOAT2 ca[4], cb[4];
        OBBCorners(a, ca);
        OBBCorners(b, cb);

        // 검사할 분리축 = a의 두 면 법선 + b의 두 면 법선
        XMFLOAT2 axes[4] = {
            Normalize(Sub(ca[1], ca[0])),
            Normalize(Sub(ca[3], ca[0])),
            Normalize(Sub(cb[1], cb[0])),
            Normalize(Sub(cb[3], cb[0])),
        };

        for (int i = 0; i < 4; ++i)
        {
            float minA, maxA, minB, maxB;
            ProjectPoints(ca, 4, axes[i], minA, maxA);
            ProjectPoints(cb, 4, axes[i], minB, maxB);
            // 이 축에서 두 구간이 겹치지 않으면 분리축 발견 → 미충돌
            if (maxA < minB || maxB < minA)
                return false;
        }
        return true; // 모든 축에서 겹침 → 충돌
    }

    // ------------------------------------------------------------
    // 벡터 반사 : r = d - 2(d·n)n   (n은 정규화된 법선)
    // ------------------------------------------------------------
    inline XMFLOAT2 Reflect(const XMFLOAT2& d, const XMFLOAT2& n)
    {
        float dn = Dot(d, n);
        return Sub(d, Mul(n, 2.0f * dn));
    }

    // ------------------------------------------------------------
    // 볼록 다각형 내부 판별 (꼭짓점이 반시계(CCW) 순서라고 가정)
    // 모든 엣지에 대해 점 p가 왼쪽이면 내부
    // ------------------------------------------------------------
    inline bool PointInConvexPolygonCCW(const XMFLOAT2* poly, int n, const XMFLOAT2& p)
    {
        for (int i = 0; i < n; ++i)
        {
            const XMFLOAT2& a = poly[i];
            const XMFLOAT2& b = poly[(i + 1) % n];
            // 엣지 a->b 기준으로 p가 오른쪽이면(cross < 0) 외부
            if (Cross(Sub(b, a), Sub(p, a)) < 0.0f)
                return false;
        }
        return true;
    }
}
