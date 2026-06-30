//***************************************************************************************
// Scene06_ProceduralNoise.h
//
// 셰이더 프로그래밍 — 절차적 노이즈. 전체화면 픽셀 셰이더로 렌더.
//   Q : 의사난수 (sin 기반 해시)
//   W : Value Noise vs Perlin Noise (좌우 분할)
//   E : FBM (Fractal Brownian Motion) — +/- 로 옥타브 조절
//   R : Domain Warping + Water Ripple (좌우 분할)
//   T : Truchet 패턴
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include <wrl/client.h>
#include <DirectXMath.h>

class Scene06_ProceduralNoise : public IScene
{
public:
    void Init(const SceneContext& ctx) override;
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene06 - 절차적 노이즈"; }
    std::wstring HudText() const override;

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    int   m_mode = 0;
    int   m_octaves = 5;
    float m_time = 0.0f;
    bool  m_inited = false;

    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11DepthStencilState> m_depthOff;
    ComPtr<ID3D11RasterizerState> m_rsNone;
};
