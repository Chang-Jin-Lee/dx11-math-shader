#include "Scene02_CurvesAndSplines.h"
#include "../Math/Curves.h"
#include "../PrimitiveBatch2D.h"
#include <cmath>

using namespace DirectX;
using namespace curve;

namespace
{
    const XMFLOAT4 kWhite = { 0.92f, 0.92f, 0.95f, 1 };
    const XMFLOAT4 kGray  = { 0.45f, 0.47f, 0.52f, 1 };
    const XMFLOAT4 kCurve = { 0.30f, 0.80f, 0.95f, 1 };
    const XMFLOAT4 kCtrl  = { 0.98f, 0.55f, 0.20f, 1 };
    const XMFLOAT4 kCtrlLine = { 0.55f, 0.40f, 0.25f, 1 };
    const XMFLOAT4 kGreen = { 0.30f, 0.85f, 0.40f, 1 };
    const XMFLOAT4 kYellow= { 0.98f, 0.82f, 0.25f, 1 };
    const XMFLOAT4 kPink  = { 0.95f, 0.45f, 0.70f, 1 };

    float Dist2(XMFLOAT2 a, XMFLOAT2 b) { float dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy; }
}

void Scene02_CurvesAndSplines::EnsureInit(const SceneContext& ctx)
{
    if (m_inited) return;
    float W = (float)ctx.screenWidth, H = (float)ctx.screenHeight;

    m_bez[0] = { W * 0.18f, H * 0.72f };
    m_bez[1] = { W * 0.36f, H * 0.28f };
    m_bez[2] = { W * 0.62f, H * 0.28f };
    m_bez[3] = { W * 0.82f, H * 0.72f };

    m_herm[0] = { W * 0.22f, H * 0.55f };          // p0
    m_herm[1] = { W * 0.34f, H * 0.25f };          // handle0
    m_herm[2] = { W * 0.78f, H * 0.55f };          // p1
    m_herm[3] = { W * 0.66f, H * 0.85f };          // handle1

    m_cat.clear();
    XMFLOAT2 c = { W * 0.5f, H * 0.55f };
    float rx = W * 0.28f, ry = H * 0.30f;
    int N = 7;
    for (int i = 0; i < N; ++i)
    {
        float a = XM_2PI * i / N;
        float jitter = (i % 2) ? 0.78f : 1.0f;
        m_cat.push_back({ c.x + rx * jitter * std::cos(a), c.y + ry * jitter * std::sin(a) });
    }
    m_inited = true;
}

void Scene02_CurvesAndSplines::UpdateDrag(const SceneContext& ctx, XMFLOAT2* pts, int count)
{
    XMFLOAT2 m = { (float)ctx.mouse.x, (float)ctx.mouse.y };
    auto* mt = ctx.mouseTracker;
    const float grab = 22.0f * 22.0f;

    if (mt && mt->leftButton == Mouse::ButtonStateTracker::PRESSED)
    {
        m_drag = -1;
        float best = grab;
        for (int i = 0; i < count; ++i)
        {
            float d = Dist2(m, pts[i]);
            if (d < best) { best = d; m_drag = i; }
        }
    }
    if (!ctx.mouse.leftButton) m_drag = -1;
    if (m_drag >= 0 && m_drag < count) pts[m_drag] = m;
}

void Scene02_CurvesAndSplines::Update(const SceneContext& ctx, float dt)
{
    EnsureInit(ctx);
    m_time += dt;
    auto* kt = ctx.keyboardTracker;
    if (kt)
    {
        if (kt->IsKeyPressed(Keyboard::Q)) { m_mode = Mode::Interp;  m_drag = -1; }
        else if (kt->IsKeyPressed(Keyboard::W)) { m_mode = Mode::Bezier;  m_drag = -1; }
        else if (kt->IsKeyPressed(Keyboard::E)) { m_mode = Mode::Hermite; m_drag = -1; }
        else if (kt->IsKeyPressed(Keyboard::R)) { m_mode = Mode::Catmull; m_drag = -1; }
    }

    switch (m_mode)
    {
    case Mode::Bezier:  UpdateDrag(ctx, m_bez, 4); break;
    case Mode::Hermite: UpdateDrag(ctx, m_herm, 4); break;
    case Mode::Catmull: UpdateDrag(ctx, m_cat.data(), (int)m_cat.size()); break;
    default: break;
    }
}

void Scene02_CurvesAndSplines::Render(const SceneContext& ctx)
{
    switch (m_mode)
    {
    case Mode::Interp:  RenderInterp(ctx); break;
    case Mode::Bezier:  RenderBezier(ctx); break;
    case Mode::Hermite: RenderHermite(ctx); break;
    case Mode::Catmull: RenderCatmull(ctx); break;
    }
}

void Scene02_CurvesAndSplines::RenderInterp(const SceneContext& ctx)
{
    PrimitiveBatch2D* b = ctx.batch;
    float W = (float)ctx.screenWidth, H = (float)ctx.screenHeight;

    // 0..1 pingpong
    float tt = std::fmod(m_time * 0.4f, 2.0f);
    float t = tt < 1.0f ? tt : 2.0f - tt;

    struct E { const wchar_t* n; float (*f)(float); XMFLOAT4 c; };
    E es[4] = {
        { L"Linear",      EaseLinear,      kWhite },
        { L"Smoothstep",  EaseSmoothstep,  kGreen },
        { L"SineIn",      EaseSineIn,      kYellow },
        { L"SineInOut",   EaseSineInOut,   kPink },
    };

    float left = W * 0.12f, right = W * 0.88f;
    // 위쪽: 4개 점이 각 이징으로 좌우 이동
    for (int i = 0; i < 4; ++i)
    {
        float y = H * 0.16f + i * 34.0f;
        b->AddLine({ left, y }, { right, y }, kGray);
        float x = left + (right - left) * es[i].f(t);
        b->AddFilledCircle({ x, y }, 9.0f, es[i].c);
    }

    // 아래쪽: 이징 그래프
    float gx = W * 0.30f, gy = H * 0.92f, gw = W * 0.40f, gh = H * 0.42f;
    b->AddRectOutline({ gx, gy - gh }, { gx + gw, gy }, kGray);
    const int S = 64;
    for (int i = 0; i < 4; ++i)
    {
        std::vector<XMFLOAT2> pts(S + 1);
        for (int s = 0; s <= S; ++s)
        {
            float u = (float)s / S;
            pts[s] = { gx + gw * u, gy - gh * es[i].f(u) };
        }
        b->AddPolyline(pts.data(), S + 1, es[i].c, false);
    }
    // 현재 t 수직선
    b->AddLine({ gx + gw * t, gy - gh }, { gx + gw * t, gy }, kWhite);
}

void Scene02_CurvesAndSplines::RenderBezier(const SceneContext& ctx)
{
    PrimitiveBatch2D* b = ctx.batch;
    // 제어 다각형
    b->AddPolyline(m_bez, 4, kCtrlLine, false);
    // 곡선
    const int S = 100;
    std::vector<XMFLOAT2> pts(S + 1);
    for (int s = 0; s <= S; ++s)
        pts[s] = CubicBezier(m_bez[0], m_bez[1], m_bez[2], m_bez[3], (float)s / S);
    b->AddPolyline(pts.data(), S + 1, kCurve, false);

    // de Casteljau 시각화 (현재 t)
    float t = std::fmod(m_time * 0.25f, 1.0f);
    XMFLOAT2 a0 = Lerp(m_bez[0], m_bez[1], t), a1 = Lerp(m_bez[1], m_bez[2], t), a2 = Lerp(m_bez[2], m_bez[3], t);
    XMFLOAT2 b0 = Lerp(a0, a1, t), b1 = Lerp(a1, a2, t);
    XMFLOAT2 p = Lerp(b0, b1, t);
    b->AddLine(a0, a1, kGray); b->AddLine(a1, a2, kGray);
    b->AddLine(b0, b1, kGreen);
    b->AddFilledCircle(p, 8.0f, kYellow);

    // 제어점
    for (int i = 0; i < 4; ++i) b->AddFilledCircle(m_bez[i], 9.0f, kCtrl);
}

void Scene02_CurvesAndSplines::RenderHermite(const SceneContext& ctx)
{
    PrimitiveBatch2D* b = ctx.batch;
    XMFLOAT2 p0 = m_herm[0], h0 = m_herm[1], p1 = m_herm[2], h1 = m_herm[3];
    // 접선 = 핸들 - 끝점 (스케일)
    XMFLOAT2 m0 = { (h0.x - p0.x) * 3.0f, (h0.y - p0.y) * 3.0f };
    XMFLOAT2 m1 = { (p1.x - h1.x) * 3.0f, (p1.y - h1.y) * 3.0f };

    const int S = 100;
    std::vector<XMFLOAT2> pts(S + 1);
    for (int s = 0; s <= S; ++s)
        pts[s] = Hermite(p0, m0, p1, m1, (float)s / S);
    b->AddPolyline(pts.data(), S + 1, kCurve, false);

    // 접선 핸들
    b->AddArrow(p0, h0, kGreen);
    b->AddArrow(p1, h1, kGreen);
    b->AddFilledCircle(p0, 9.0f, kCtrl);
    b->AddFilledCircle(p1, 9.0f, kCtrl);
    b->AddFilledCircle(h0, 7.0f, kYellow);
    b->AddFilledCircle(h1, 7.0f, kYellow);
}

void Scene02_CurvesAndSplines::RenderCatmull(const SceneContext& ctx)
{
    PrimitiveBatch2D* b = ctx.batch;
    int K = (int)m_cat.size();
    if (K < 4) return;

    // 닫힌 루프 샘플 + arc-length
    std::vector<XMFLOAT2> samp;
    std::vector<float> cum;
    float total = 0.0f;
    samp.push_back(m_cat[0]); cum.push_back(0.0f);
    const int M = 24;
    for (int i = 0; i < K; ++i)
    {
        XMFLOAT2 p0 = m_cat[(i - 1 + K) % K], p1 = m_cat[i], p2 = m_cat[(i + 1) % K], p3 = m_cat[(i + 2) % K];
        for (int s = 1; s <= M; ++s)
        {
            XMFLOAT2 q = CatmullRom(p0, p1, p2, p3, (float)s / M);
            total += std::sqrt(Dist2(q, samp.back()));
            samp.push_back(q); cum.push_back(total);
        }
    }
    // 곡선
    b->AddPolyline(samp.data(), (int)samp.size(), kCurve, false);
    // 제어점
    for (int i = 0; i < K; ++i) b->AddFilledCircle(m_cat[i], 8.0f, kCtrl);

    // 등속 이동 점 (arc-length 기반)
    if (total > 1.0f)
    {
        float s = std::fmod(m_time * 160.0f, total);
        size_t idx = 0;
        while (idx + 1 < cum.size() && cum[idx + 1] < s) ++idx;
        float segLen = cum[idx + 1] - cum[idx];
        float f = segLen > 1e-4f ? (s - cum[idx]) / segLen : 0.0f;
        XMFLOAT2 pos = Lerp(samp[idx], samp[idx + 1], f);
        b->AddFilledCircle(pos, 11.0f, kYellow);
    }
}

std::wstring Scene02_CurvesAndSplines::HudText() const
{
    std::wstring s = L"서브모드:  Q=보간비교  W=Bezier  E=Hermite  R=Catmull-Rom\n";
    switch (m_mode)
    {
    case Mode::Interp:  s += L"[보간] 같은 시간, 다른 이징 함수. 아래 그래프는 t→값 곡선."; break;
    case Mode::Bezier:  s += L"[Bezier] 3차 곡선. 주황 제어점을 드래그. 노란 점은 de Casteljau 결과."; break;
    case Mode::Hermite: s += L"[Hermite] 끝점(주황)·접선 핸들(노랑)을 드래그. 접선이 곡선 방향을 결정."; break;
    case Mode::Catmull: s += L"[Catmull-Rom] 제어점을 모두 지나는 닫힌 스플라인. 노란 점은 등속(arc-length) 이동."; break;
    }
    return s;
}
