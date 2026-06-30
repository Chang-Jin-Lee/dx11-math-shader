//***************************************************************************************
// Scene01_MathFundamentals.h
//
// 게임수학 1학기 핵심 — 충돌/운동/벡터를 2D 오버레이로 보여준다.
//   Q : AABB 충돌
//   W : OBB 충돌 (SAT)
//   E : 원형·타원·정현파·나선 운동
//   R : 벡터 반사 + 볼록 다각형 내부 판별
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Math/Collision2D.h"
#include <vector>

class Scene01_MathFundamentals : public IScene
{
public:
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene01 - 게임수학 기초 (충돌·운동·벡터)"; }
    std::wstring HudText() const override;

private:
    enum class Mode { AABB, OBB, Orbit, Reflect };

    void UpdateReflect(const SceneContext& ctx, float dt);

    Mode  m_mode = Mode::AABB;
    float m_time = 0.0f;

    // --- 반사 데모 상태 ---
    math2d::XMFLOAT2 m_ballPos = { 0.0f, 0.0f };
    math2d::XMFLOAT2 m_ballVel = { 220.0f, 160.0f };
    bool             m_reflectInit = false;
    // 마지막 충돌 시 입사/반사 벡터 시각화(잠시 표시)
    math2d::XMFLOAT2 m_hitPoint = { 0.0f, 0.0f };
    math2d::XMFLOAT2 m_incident = { 0.0f, 0.0f };
    math2d::XMFLOAT2 m_reflected = { 0.0f, 0.0f };
    float            m_hitTimer = 0.0f;
};
