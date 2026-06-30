//***************************************************************************************
// Scene03_TransformProjection.h
//
// 게임수학 — 행렬 변환과 투영.
//   Q : TRS 행렬 분해 (S*R*T). 방향키 이동, [ ] 스케일, 회전 자동. 월드 행렬 텍스트 표시
//   W : 정사영(Orthographic) vs 원근(Perspective) 분할 화면. F/V 로 FOV 조절
//   E : LookAt 행렬을 외적으로 직접 전개. 마우스 좌드래그 공전, RGB 축 표시
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Render/OrbitCamera.h"
#include <DirectXMath.h>

class Scene03_TransformProjection : public IScene
{
public:
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene03 - 행렬 변환과 투영"; }
    std::wstring HudText() const override;

private:
    enum class Mode { TRS, Proj, LookAt };

    void RenderTRS(const SceneContext& ctx);
    void RenderProj(const SceneContext& ctx);
    void RenderLookAt(const SceneContext& ctx);

    Mode  m_mode = Mode::TRS;
    float m_time = 0.0f;

    // TRS 상태
    DirectX::XMFLOAT3 m_t = { 0.0f, 0.6f, 0.0f };
    float m_scale = 1.0f;
    DirectX::XMFLOAT4X4 m_world{};   // HUD 표시용

    // 투영
    float m_fovDeg = 60.0f;

    // 카메라(TRS/LookAt 공전)
    OrbitCamera m_cam;
    int  m_prevMouseX = 0, m_prevMouseY = 0;
    bool m_dragging = false;
};
