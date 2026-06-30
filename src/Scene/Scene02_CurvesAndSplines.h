//***************************************************************************************
// Scene02_CurvesAndSplines.h
//
// 게임수학 — 보간과 곡선 (2D 오버레이).
//   Q : 선형·이징 보간 비교 (이동 + 그래프)
//   W : 3차 Bezier (제어점 드래그, de Casteljau 시각화)
//   E : Hermite 곡선 (끝점·접선 핸들 드래그)
//   R : Catmull-Rom 스플라인 (닫힌 루프, 등속 이동 — arc-length)
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include <DirectXMath.h>
#include <vector>

class Scene02_CurvesAndSplines : public IScene
{
public:
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene02 - 보간과 곡선"; }
    std::wstring HudText() const override;

private:
    enum class Mode { Interp, Bezier, Hermite, Catmull };

    void EnsureInit(const SceneContext& ctx);
    void UpdateDrag(const SceneContext& ctx, DirectX::XMFLOAT2* pts, int count);

    void RenderInterp(const SceneContext& ctx);
    void RenderBezier(const SceneContext& ctx);
    void RenderHermite(const SceneContext& ctx);
    void RenderCatmull(const SceneContext& ctx);

    Mode  m_mode = Mode::Interp;
    float m_time = 0.0f;
    bool  m_inited = false;
    int   m_drag = -1;   // 드래그 중인 제어점 인덱스

    DirectX::XMFLOAT2 m_bez[4]{};
    DirectX::XMFLOAT2 m_herm[4]{};   // p0, handle0, p1, handle1
    std::vector<DirectX::XMFLOAT2> m_cat;
};
