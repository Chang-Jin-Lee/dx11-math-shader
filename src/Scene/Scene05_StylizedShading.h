//***************************************************************************************
// Scene05_StylizedShading.h
//
// 셰이더 프로그래밍 — 스타일라이즈드(비사실적) 셰이딩. 대상은 VRoid 캐릭터(.glb).
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
#include <vector>

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

    struct GpuSub
    {
        ComPtr<ID3D11Buffer> vb, ib;
        UINT count = 0;
        int  imageIndex = -1;
        DirectX::XMFLOAT4 base = { 1,1,1,1 };
        bool  alphaTest = false;
        float cutoff = 0.5f;
    };

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
    ComPtr<ID3D11PixelShader>  m_psNormalDepth, m_psSobelOutline;
    ComPtr<ID3D11Buffer> m_cbFrame, m_cbObject, m_cbMat;
    std::vector<GpuSub> m_subs;
    std::vector<ComPtr<ID3D11ShaderResourceView>> m_texSRVs;
    DirectX::XMFLOAT2 m_texel = { 1.0f / 1280, 1.0f / 720 };

    ComPtr<ID3D11RasterizerState> m_rsBack, m_rsFront, m_rsNone;
    ComPtr<ID3D11DepthStencilState> m_depthOn, m_depthOff;
    ComPtr<ID3D11SamplerState> m_samp;

    RenderTexture   m_rt;     // Sobel 색/스케치
    RenderTexture   m_rtNrm;  // 노말+깊이 (Sobel 외곽선)
    FullscreenPass  m_fs;
};
