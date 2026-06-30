//***************************************************************************************
// Scene08_DeferredShading.h
//
// 셰이더 프로그래밍 — Deferred Shading.
//   Geometry Pass : 씬을 G-Buffer 3개(MRT)에 기록 (Albedo / Normal / WorldPos)
//   Lighting Pass : 전체화면에서 G-Buffer를 읽어 8개 점광원을 누적
//   F : G-Buffer 시각화(4분할) 토글 / +,- : 광원 수
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Render/OrbitCamera.h"
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

class Scene08_DeferredShading : public IScene
{
public:
    void Init(const SceneContext& ctx) override;
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene08 - 디퍼드 셰이딩"; }
    std::wstring HudText() const override;

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool EnsureGBuffer(ID3D11Device* d, int w, int h);
    void DrawMesh(ID3D11DeviceContext* c, const DirectX::XMMATRIX& vp, const DirectX::XMMATRIX& world,
                  const DirectX::XMFLOAT4& albedo, ID3D11Buffer* vb, ID3D11Buffer* ib, UINT n);

    int   m_numLights = 8;
    bool  m_debug = false;
    float m_time = 0.0f;
    bool  m_inited = false;

    OrbitCamera m_cam;
    int m_prevMouseX = 0, m_prevMouseY = 0, m_prevWheel = 0; bool m_dragging = false;

    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11VertexShader> m_geomVS;
    ComPtr<ID3D11PixelShader>  m_geomPS, m_lightPS;
    ComPtr<ID3D11Buffer> m_cbGeom, m_cbLight;
    ComPtr<ID3D11Buffer> m_sphereVB, m_sphereIB, m_planeVB, m_planeIB;
    UINT m_sphereCount = 0, m_planeCount = 0;

    // G-Buffer
    int m_gw = 0, m_gh = 0;
    ComPtr<ID3D11Texture2D> m_gTex[3], m_gDepth;
    ComPtr<ID3D11RenderTargetView> m_gRTV[3];
    ComPtr<ID3D11ShaderResourceView> m_gSRV[3];
    ComPtr<ID3D11DepthStencilView> m_gDSV;

    ComPtr<ID3D11SamplerState> m_samp;
    ComPtr<ID3D11DepthStencilState> m_depthOn, m_depthOff;
    ComPtr<ID3D11RasterizerState> m_rsBack, m_rsNone;
};
