#include "Scene01_MathFundamentals.h"
#include "../PrimitiveBatch2D.h"
#include <cmath>

using namespace DirectX;
using namespace math2d;

namespace
{
    const XMFLOAT4 kGreen = { 0.25f, 0.90f, 0.40f, 1.0f };
    const XMFLOAT4 kRed   = { 0.95f, 0.27f, 0.22f, 1.0f };
    const XMFLOAT4 kWhite = { 0.92f, 0.92f, 0.95f, 1.0f };
    const XMFLOAT4 kGray  = { 0.45f, 0.45f, 0.50f, 1.0f };
    const XMFLOAT4 kYellow= { 0.98f, 0.82f, 0.25f, 1.0f };
    const XMFLOAT4 kCyan  = { 0.30f, 0.80f, 0.95f, 1.0f };
    const XMFLOAT4 kOrange= { 0.98f, 0.55f, 0.20f, 1.0f };

    XMFLOAT4 Fill(const XMFLOAT4& c, float a) { return { c.x, c.y, c.z, a }; }
}

void Scene01_MathFundamentals::Update(const SceneContext& ctx, float dt)
{
    m_time += dt;

    // 서브모드 전환
    auto* kt = ctx.keyboardTracker;
    if (kt)
    {
        if (kt->IsKeyPressed(Keyboard::Q)) m_mode = Mode::AABB;
        else if (kt->IsKeyPressed(Keyboard::W)) m_mode = Mode::OBB;
        else if (kt->IsKeyPressed(Keyboard::E)) m_mode = Mode::Orbit;
        else if (kt->IsKeyPressed(Keyboard::R)) m_mode = Mode::Reflect;
    }

    if (m_mode == Mode::Reflect)
        UpdateReflect(ctx, dt);
}

void Scene01_MathFundamentals::UpdateReflect(const SceneContext& ctx, float dt)
{
    float W = static_cast<float>(ctx.screenWidth);
    float H = static_cast<float>(ctx.screenHeight);
    // 경계 사각형(여백)
    float margin = 80.0f;
    float left = margin, right = W - margin, top = margin, bottom = H - margin;

    if (!m_reflectInit)
    {
        m_ballPos = { W * 0.5f, H * 0.5f };
        m_reflectInit = true;
    }

    m_ballPos.x += m_ballVel.x * dt;
    m_ballPos.y += m_ballVel.y * dt;

    const float r = 14.0f;
    XMFLOAT2 n = { 0.0f, 0.0f };
    bool hit = false;

    if (m_ballPos.x - r < left)  { m_ballPos.x = left + r;  n = { 1.0f, 0.0f };  hit = true; }
    else if (m_ballPos.x + r > right) { m_ballPos.x = right - r; n = { -1.0f, 0.0f }; hit = true; }
    if (m_ballPos.y - r < top)   { m_ballPos.y = top + r;   n = { 0.0f, 1.0f };  hit = true; }
    else if (m_ballPos.y + r > bottom) { m_ballPos.y = bottom - r; n = { 0.0f, -1.0f }; hit = true; }

    if (hit)
    {
        XMFLOAT2 incident = Normalize(m_ballVel);
        m_ballVel = Reflect(m_ballVel, n);          // r = d - 2(d·n)n
        m_hitPoint = m_ballPos;
        m_incident = incident;
        m_reflected = Normalize(m_ballVel);
        m_hitTimer = 1.2f;
    }

    if (m_hitTimer > 0.0f) m_hitTimer -= dt;
}

void Scene01_MathFundamentals::Render(const SceneContext& ctx)
{
    PrimitiveBatch2D* b = ctx.batch;
    float W = static_cast<float>(ctx.screenWidth);
    float H = static_cast<float>(ctx.screenHeight);
    XMFLOAT2 mouse = { static_cast<float>(ctx.mouse.x), static_cast<float>(ctx.mouse.y) };

    switch (m_mode)
    {
    // ---------------------------------------------------------------
    case Mode::AABB:
    {
        AABB boxB;
        XMFLOAT2 cB = { W * 0.5f, H * 0.5f };
        XMFLOAT2 hB = { 110.0f, 75.0f };
        boxB.min = { cB.x - hB.x, cB.y - hB.y };
        boxB.max = { cB.x + hB.x, cB.y + hB.y };

        AABB boxA;
        XMFLOAT2 hA = { 70.0f, 50.0f };
        boxA.min = { mouse.x - hA.x, mouse.y - hA.y };
        boxA.max = { mouse.x + hA.x, mouse.y + hA.y };

        bool hit = IntersectAABB(boxA, boxB);
        XMFLOAT4 col = hit ? kRed : kGreen;

        b->AddFilledQuad(boxB.min, boxB.max, Fill(kCyan, 0.18f));
        b->AddRectOutline(boxB.min, boxB.max, kCyan);
        b->AddFilledQuad(boxA.min, boxA.max, Fill(col, 0.22f));
        b->AddRectOutline(boxA.min, boxA.max, col);
        break;
    }
    // ---------------------------------------------------------------
    case Mode::OBB:
    {
        OBB obbB;
        obbB.center = { W * 0.5f, H * 0.5f };
        obbB.halfExtents = { 120.0f, 70.0f };
        obbB.rotation = 0.5f;   // 고정 회전

        OBB obbA;
        obbA.center = mouse;
        obbA.halfExtents = { 90.0f, 55.0f };
        obbA.rotation = m_time * 0.8f;   // 천천히 회전

        bool hit = IntersectOBB(obbA, obbB);
        XMFLOAT4 col = hit ? kRed : kGreen;

        XMFLOAT2 cB[4], cA[4];
        OBBCorners(obbB, cB);
        OBBCorners(obbA, cA);
        b->AddFilledConvexPolygon(cB, 4, Fill(kCyan, 0.18f));
        b->AddPolyline(cB, 4, kCyan, true);
        b->AddFilledConvexPolygon(cA, 4, Fill(col, 0.22f));
        b->AddPolyline(cA, 4, col, true);
        break;
    }
    // ---------------------------------------------------------------
    case Mode::Orbit:
    {
        XMFLOAT2 center = { W * 0.5f, H * 0.5f };
        float t = m_time;

        // 원형 운동
        {
            float R = 130.0f;
            b->AddCircleOutline(center, R, kGray, 64);
            XMFLOAT2 p = { center.x + R * std::cos(t * 1.2f), center.y + R * std::sin(t * 1.2f) };
            b->AddFilledCircle(p, 10.0f, kGreen);
        }
        // 타원 운동
        {
            float rx = 230.0f, ry = 110.0f;
            const int N = 80;
            std::vector<XMFLOAT2> path(N);
            for (int i = 0; i < N; ++i)
            {
                float a = XM_2PI * i / N;
                path[i] = { center.x + rx * std::cos(a), center.y + ry * std::sin(a) };
            }
            b->AddPolyline(path.data(), N, kGray, true);
            XMFLOAT2 p = { center.x + rx * std::cos(t * 0.9f), center.y + ry * std::sin(t * 0.9f) };
            b->AddFilledCircle(p, 10.0f, kYellow);
        }
        // 정현파 운동 (x는 선형, y는 sin)
        {
            float amp = 70.0f, baseY = H - 110.0f;
            const int N = 120;
            std::vector<XMFLOAT2> path(N);
            for (int i = 0; i < N; ++i)
            {
                float x = 60.0f + (W - 120.0f) * i / (N - 1);
                path[i] = { x, baseY + amp * std::sin(x * 0.02f) };
            }
            b->AddPolyline(path.data(), N, kGray, false);
            float px = 60.0f + std::fmod(t * 130.0f, W - 120.0f);
            XMFLOAT2 p = { px, baseY + amp * std::sin(px * 0.02f) };
            b->AddFilledCircle(p, 10.0f, kCyan);
        }
        // 나선 운동
        {
            XMFLOAT2 sc = { 180.0f, 150.0f };
            float ang = t * 2.0f;
            float rad = std::fmod(t * 22.0f, 90.0f);
            XMFLOAT2 p = { sc.x + rad * std::cos(ang), sc.y + rad * std::sin(ang) };
            b->AddFilledCircle(p, 8.0f, kOrange);
        }
        break;
    }
    // ---------------------------------------------------------------
    case Mode::Reflect:
    {
        float margin = 80.0f;
        XMFLOAT2 bmin = { margin, margin };
        XMFLOAT2 bmax = { W - margin, H - margin };
        b->AddRectOutline(bmin, bmax, kGray);

        // 공 + 진행 방향 화살표
        b->AddFilledCircle(m_ballPos, 14.0f, kCyan);
        XMFLOAT2 vdir = Normalize(m_ballVel);
        XMFLOAT2 tip = { m_ballPos.x + vdir.x * 60.0f, m_ballPos.y + vdir.y * 60.0f };
        b->AddArrow(m_ballPos, tip, kWhite);

        // 마지막 충돌의 입사(노랑)·반사(초록) 벡터
        if (m_hitTimer > 0.0f)
        {
            XMFLOAT2 inTail = { m_hitPoint.x - m_incident.x * 70.0f, m_hitPoint.y - m_incident.y * 70.0f };
            b->AddArrow(inTail, m_hitPoint, kYellow);
            XMFLOAT2 refTip = { m_hitPoint.x + m_reflected.x * 70.0f, m_hitPoint.y + m_reflected.y * 70.0f };
            b->AddArrow(m_hitPoint, refTip, kGreen);
        }

        // 볼록 오각형 + 마우스 내부 판별
        const int N = 5;
        XMFLOAT2 poly[N];
        XMFLOAT2 pc = { W - 230.0f, 200.0f };
        float pr = 90.0f;
        for (int i = 0; i < N; ++i)
        {
            float a = XM_2PI * i / N - XM_PIDIV2;   // 위쪽 꼭짓점부터
            poly[i] = { pc.x + pr * std::cos(a), pc.y + pr * std::sin(a) };
        }
        bool inside = PointInConvexPolygonCCW(poly, N, mouse);
        XMFLOAT4 pcol = inside ? kOrange : kGray;
        b->AddFilledConvexPolygon(poly, N, Fill(pcol, inside ? 0.35f : 0.15f));
        b->AddPolyline(poly, N, pcol, true);
        break;
    }
    }
}

std::wstring Scene01_MathFundamentals::HudText() const
{
    std::wstring s = L"서브모드:  Q=AABB충돌  W=OBB충돌  E=운동  R=반사/다각형\n";
    switch (m_mode)
    {
    case Mode::AABB:
        s += L"[AABB] 마우스를 움직여 박스를 옮기세요. 겹치면 빨강, 아니면 초록.";
        break;
    case Mode::OBB:
        s += L"[OBB] 회전 박스 충돌(SAT 분리축 정리). 마우스로 이동, 박스는 자동 회전.";
        break;
    case Mode::Orbit:
        s += L"[운동] 원형·타원·정현파·나선 운동. 흰 선은 궤도 경로.";
        break;
    case Mode::Reflect:
        s += L"[반사] 공이 벽에 반사(r=d-2(d·n)n). 마우스가 오각형 내부면 채워짐.";
        break;
    }
    return s;
}
