#include "Scene03_TransformProjection.h"
#include "../Render/PrimitiveBatch3D.h"
#include "../PrimitiveBatch2D.h"
#include <cstdio>
#include <algorithm>

using namespace DirectX;

namespace
{
    const XMFLOAT4 kGridCol = { 0.35f, 0.37f, 0.42f, 0.6f };
    const XMFLOAT4 kFaint   = { 0.25f, 0.27f, 0.32f, 0.5f };

    XMFLOAT3 V3(FXMVECTOR v) { XMFLOAT3 r; XMStoreFloat3(&r, v); return r; }
}

void Scene03_TransformProjection::Update(const SceneContext& ctx, float dt)
{
    m_time += dt;
    auto* kt = ctx.keyboardTracker;
    if (kt)
    {
        if (kt->IsKeyPressed(Keyboard::Q)) m_mode = Mode::TRS;
        else if (kt->IsKeyPressed(Keyboard::W)) m_mode = Mode::Proj;
        else if (kt->IsKeyPressed(Keyboard::E)) m_mode = Mode::LookAt;

        if (kt->IsKeyPressed(Keyboard::F)) m_fovDeg = std::min(120.0f, m_fovDeg + 10.0f);
        if (kt->IsKeyPressed(Keyboard::V)) m_fovDeg = std::max(20.0f, m_fovDeg - 10.0f);
    }

    // TRS: 방향키 이동, 대괄호 스케일 (눌림 상태)
    const Keyboard::State& k = ctx.keyboard;
    if (m_mode == Mode::TRS)
    {
        float spd = 2.0f * dt;
        if (k.Left)  m_t.x -= spd;
        if (k.Right) m_t.x += spd;
        if (k.Up)    m_t.z += spd;
        if (k.Down)  m_t.z -= spd;
        if (k.OemOpenBrackets)  m_scale = std::max(0.2f, m_scale - dt);
        if (k.OemCloseBrackets) m_scale = std::min(3.0f, m_scale + dt);
    }

    // 마우스 좌드래그 공전 (TRS, LookAt)
    int mx = ctx.mouse.x, my = ctx.mouse.y;
    if (ctx.mouse.leftButton)
    {
        if (m_dragging)
            m_cam.Rotate((mx - m_prevMouseX) * 0.01f, (my - m_prevMouseY) * 0.01f);
        m_dragging = true;
    }
    else m_dragging = false;
    m_prevMouseX = mx; m_prevMouseY = my;
}

void Scene03_TransformProjection::Render(const SceneContext& ctx)
{
    switch (m_mode)
    {
    case Mode::TRS:    RenderTRS(ctx); break;
    case Mode::Proj:   RenderProj(ctx); break;
    case Mode::LookAt: RenderLookAt(ctx); break;
    }
}

void Scene03_TransformProjection::RenderTRS(const SceneContext& ctx)
{
    float aspect = (float)ctx.screenWidth / ctx.screenHeight;
    PrimitiveBatch3D* b = ctx.batch3d;
    b->Begin(m_cam.View() * m_cam.Proj(aspect));

    b->AddGrid(8.0f, 16, kGridCol, 0.0f);
    b->AddAxis(XMMatrixIdentity(), 2.0f);   // 월드 축

    // world = S * R * T  (순서 중요)
    XMMATRIX S = XMMatrixScaling(m_scale, m_scale, m_scale);
    XMMATRIX R = XMMatrixRotationY(m_time * 0.8f);
    XMMATRIX T = XMMatrixTranslation(m_t.x, m_t.y, m_t.z);
    XMMATRIX world = S * R * T;
    XMStoreFloat4x4(&m_world, world);

    b->AddSolidBox(world);
    b->AddWireBox(world, XMFLOAT4(1, 1, 1, 0.9f));
    // 큐브와 함께 도는 로컬 축 기즈모(스케일 제외)
    b->AddAxis(R * T, 1.3f);
    b->End();
}

void Scene03_TransformProjection::RenderProj(const SceneContext& ctx)
{
    ID3D11DeviceContext* c = ctx.context;
    float W = (float)ctx.screenWidth, H = (float)ctx.screenHeight;
    float halfAspect = (W * 0.5f) / H;

    // 공통 시점
    XMVECTOR eye = XMVectorSet(9.0f, 7.0f, -9.0f, 1.0f);
    XMVECTOR at  = XMVectorSet(0.0f, 1.0f, 4.5f, 1.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, XMVectorSet(0, 1, 0, 0));

    float orthoH = 14.0f;
    XMMATRIX ortho = XMMatrixOrthographicLH(orthoH * halfAspect, orthoH, 0.1f, 100.0f);
    XMMATRIX persp = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fovDeg), halfAspect, 0.1f, 100.0f);

    auto drawContent = [&](PrimitiveBatch3D* b) {
        b->AddGrid(8.0f, 16, kFaint, 0.0f);
        // z축을 따라 같은 크기 큐브 4개 (원근에서는 멀수록 작아 보임)
        for (int i = 0; i < 4; ++i)
            b->AddSolidBox(XMMatrixTranslation(0.0f, 0.7f, i * 3.0f));
    };

    auto setVP = [&](float x, float w) {
        D3D11_VIEWPORT vp{}; vp.TopLeftX = x; vp.TopLeftY = 0; vp.Width = w; vp.Height = H;
        vp.MinDepth = 0; vp.MaxDepth = 1; c->RSSetViewports(1, &vp);
    };

    PrimitiveBatch3D* b = ctx.batch3d;
    // 왼쪽: 정사영
    setVP(0.0f, W * 0.5f);
    b->Begin(view * ortho); drawContent(b); b->End();
    // 오른쪽: 원근
    setVP(W * 0.5f, W * 0.5f);
    b->Begin(view * persp); drawContent(b); b->End();

    // 전체 뷰포트 복구
    D3D11_VIEWPORT full{}; full.Width = W; full.Height = H; full.MinDepth = 0; full.MaxDepth = 1;
    c->RSSetViewports(1, &full);

    // 가운데 구분선 (2D 배처는 DemoApp이 flush)
    ctx.batch->AddLine({ W * 0.5f, 0 }, { W * 0.5f, H }, XMFLOAT4(0.7f, 0.7f, 0.75f, 1));
}

void Scene03_TransformProjection::RenderLookAt(const SceneContext& ctx)
{
    float aspect = (float)ctx.screenWidth / ctx.screenHeight;
    PrimitiveBatch3D* b = ctx.batch3d;

    // LookAt 행렬을 외적으로 직접 구성 (XMMatrixLookAtLH와 동일)
    XMFLOAT3 eye = m_cam.Eye();
    XMFLOAT3 tgt = m_cam.target;
    XMVECTOR E = XMLoadFloat3(&eye), Tg = XMLoadFloat3(&tgt), Up = XMVectorSet(0, 1, 0, 0);
    XMVECTOR zA = XMVector3Normalize(Tg - E);            // forward
    XMVECTOR xA = XMVector3Normalize(XMVector3Cross(Up, zA)); // right
    XMVECTOR yA = XMVector3Cross(zA, xA);                // up
    XMFLOAT3 x = V3(xA), y = V3(yA), z = V3(zA);
    float tx = -XMVectorGetX(XMVector3Dot(xA, E));
    float ty = -XMVectorGetX(XMVector3Dot(yA, E));
    float tz = -XMVectorGetX(XMVector3Dot(zA, E));
    XMMATRIX view(
        x.x, y.x, z.x, 0,
        x.y, y.y, z.y, 0,
        x.z, y.z, z.z, 0,
        tx,  ty,  tz,  1);

    b->Begin(view * m_cam.Proj(aspect));
    b->AddGrid(8.0f, 16, kFaint, 0.0f);
    b->AddAxis(XMMatrixIdentity(), 3.0f);   // 월드 X(R)/Y(G)/Z(B) 축
    // 주변에 큐브 몇 개
    b->AddSolidBox(XMMatrixTranslation(0, 0.6f, 0));
    b->AddSolidBox(XMMatrixScaling(0.7f, 0.7f, 0.7f) * XMMatrixTranslation(3, 0.5f, 1));
    b->AddSolidBox(XMMatrixScaling(0.7f, 0.7f, 0.7f) * XMMatrixTranslation(-2, 0.5f, 2.5f));
    b->End();
}

std::wstring Scene03_TransformProjection::HudText() const
{
    wchar_t buf[512];
    if (m_mode == Mode::TRS)
    {
        swprintf(buf, 512,
            L"서브모드: Q=TRS분해  W=투영비교  E=LookAt   |   방향키:이동  [ ]:스케일  드래그:공전\n"
            L"[TRS]  T=(%.1f, %.1f, %.1f)  R_y=%.0f°  S=%.2f   world = S*R*T:\n"
            L"  %6.2f %6.2f %6.2f %6.2f\n  %6.2f %6.2f %6.2f %6.2f\n"
            L"  %6.2f %6.2f %6.2f %6.2f\n  %6.2f %6.2f %6.2f %6.2f",
            m_t.x, m_t.y, m_t.z, XMConvertToDegrees(m_time * 0.8f), m_scale,
            m_world._11, m_world._12, m_world._13, m_world._14,
            m_world._21, m_world._22, m_world._23, m_world._24,
            m_world._31, m_world._32, m_world._33, m_world._34,
            m_world._41, m_world._42, m_world._43, m_world._44);
        return buf;
    }
    if (m_mode == Mode::Proj)
    {
        swprintf(buf, 512,
            L"서브모드: Q=TRS분해  W=투영비교  E=LookAt\n"
            L"[투영]  왼쪽=정사영(Orthographic)  오른쪽=원근(Perspective).  F/V: FOV 조절 (현재 %.0f°)\n"
            L"같은 크기 큐브 — 정사영은 깊이와 무관하게 동일, 원근은 멀수록 작아진다.", m_fovDeg);
        return buf;
    }
    return
        L"서브모드: Q=TRS분해  W=투영비교  E=LookAt   |   마우스 좌드래그: 카메라 공전\n"
        L"[LookAt]  View 행렬을 z=normalize(target-eye), x=normalize(cross(up,z)), y=cross(z,x)\n"
        L"로 직접 구성. 축: X=빨강 Y=초록 Z=파랑.";
}
