//***************************************************************************************
// Scene05_StylizedShading.h
//
// 셰이더 프로그래밍 3학기 — 스타일라이즈드(비사실적) 셰이딩. 대상은 VRoid 캐릭터(.glb).
//   Q : Toon / Cell Shading (밝기 양자화)
//   W : Outline (뒷면 노멀 확장) + 일반 셰이딩
//   E : Toon + Outline (셀 셰이딩 룩)
//   R : Sobel 엣지 감지 (오프스크린 2-pass, 스케치 룩)
//   T : Hatching (밝기별 절차적 사선)
// 마우스 좌드래그 공전, 휠 줌.
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Render/OrbitCamera.h"
#include "../Render/RenderTexture.h"
#include "../Render/FullscreenPass.h"
#include <wrl/client.h>
#include <DirectXMath.h>

class Scene05_StylizedShading : public IScene
{
public:
    void Init(const SceneContext& ctx) override;
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene05 - 스타일라이즈드 셰이딩 (캐릭터)"; }
    std::wstring HudText() const override;

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void DrawModel(ID3D11DeviceContext* c, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11RasterizerState* rs);
    void UpdateFrameCB(const SceneContext& ctx);

    int   m_mode = 0;       // 0=Toon,1=Outline,2=Toon+Outline,3=Sobel,4=Hatching
    float m_time = 0.0f;
    bool  m_inited = false;
    bool  m_modelOK = false;

    OrbitCamera m_cam;
    int  m_prevMouseX = 0, m_prevMouseY = 0, m_prevWheel = 0;
    bool m_dragging = false;
    DirectX::XMMATRIX m_world = DirectX::XMMatrixIdentity();

    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11VertexShader> m_vsMain, m_vsOutline;
    ComPtr<ID3D11PixelShader>  m_psToon, m_psOutline, m_psFlat, m_psHatch, m_psSobel;
    ComPtr<ID3D11Buffer> m_cbFrame, m_cbObject, m_cbMat;
    ComPtr<ID3D11Buffer> m_vb, m_ib;
    UINT m_indexCount = 0;

    ComPtr<ID3D11RasterizerState> m_rsBack, m_rsFront, m_rsNone;
    ComPtr<ID3D11DepthStencilState> m_depthOn, m_depthOff;
    ComPtr<ID3D11SamplerState> m_samp;

    RenderTexture   m_rt;
    FullscreenPass  m_fs;
};
