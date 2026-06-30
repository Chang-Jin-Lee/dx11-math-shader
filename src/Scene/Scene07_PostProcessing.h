//***************************************************************************************
// Scene07_PostProcessing.h
//
// 셰이더 프로그래밍 — 포스트 프로세싱. 씬을 HDR 렌더 타겟에 그린 뒤 전체화면 패스로 가공.
//   Q : Gaussian Blur (분리형 2-pass), [ ] 로 반경
//   W : Bilateral Filter (엣지 보존 블러)
//   E : Gamma Correction (좌: 미적용 / 우: 적용 비교)
//   R : HDR Tone Mapping (좌: Reinhard / 우: ACES), Z/X 노출
//   T : Bloom (Bright-pass → Blur → 합성)
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Render/OrbitCamera.h"
#include "../Render/RenderTexture.h"
#include <wrl/client.h>
#include <DirectXMath.h>

class Scene07_PostProcessing : public IScene
{
public:
    void Init(const SceneContext& ctx) override;
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene07 - 포스트 프로세싱"; }
    std::wstring HudText() const override;

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void RenderSceneToHDR(const SceneContext& ctx);
    void FullscreenTo(const SceneContext& ctx, ID3D11RenderTargetView* rtv, int w, int h,
                      ID3D11PixelShader* ps, ID3D11ShaderResourceView* src0, ID3D11ShaderResourceView* src1 = nullptr);

    int   m_mode = 3;     // 기본 ToneMap (HDR이 가장 잘 보임)
    int   m_radius = 4;
    float m_exposure = 1.0f;
    float m_time = 0.0f;
    bool  m_inited = false;

    OrbitCamera m_cam;
    int m_prevMouseX = 0, m_prevMouseY = 0, m_prevWheel = 0; bool m_dragging = false;

    // 씬(HDR Phong)
    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11VertexShader> m_sceneVS;
    ComPtr<ID3D11PixelShader>  m_scenePS;
    ComPtr<ID3D11Buffer> m_cbScene;
    ComPtr<ID3D11Buffer> m_sphereVB, m_sphereIB, m_planeVB, m_planeIB;
    UINT m_sphereCount = 0, m_planeCount = 0;

    // 포스트
    ComPtr<ID3D11PixelShader> m_psBlurH, m_psBlurV, m_psBilateral, m_psBright, m_psComposite, m_psFinal;
    ComPtr<ID3D11Buffer> m_cbPost;
    ComPtr<ID3D11SamplerState> m_samp;
    ComPtr<ID3D11DepthStencilState> m_depthOn, m_depthOff;
    ComPtr<ID3D11RasterizerState> m_rsBack, m_rsNone;

    RenderTexture m_hdr, m_rtA, m_rtB;
};
