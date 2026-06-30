//***************************************************************************************
// Scene04_PhongAndNormalMap.h
//
// 셰이더 프로그래밍 — Phong 조명과 Normal Mapping.
// 구체 + 바닥 평면에 점광원을 비추고, 서브모드로 조명 항을 하나씩 누적해 보여준다.
//   Q : Diffuse
//   W : + Specular
//   E : + Ambient
//   R : + Emissive (전체 Phong)
//   T : Normal Mapping (절차적 벽돌 디퓨즈/노말맵)
// 마우스 좌드래그로 카메라 공전, 휠로 줌.
//***************************************************************************************
#pragma once
#include "../IScene.h"
#include "../Render/OrbitCamera.h"
#include <wrl/client.h>
#include <DirectXMath.h>

class Scene04_PhongAndNormalMap : public IScene
{
public:
    void Init(const SceneContext& ctx) override;
    void Update(const SceneContext& ctx, float dt) override;
    void Render(const SceneContext& ctx) override;
    const wchar_t* Name() const override { return L"Scene04 - Phong 조명 + Normal Mapping"; }
    std::wstring HudText() const override;

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void DrawMesh(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& world,
                  const DirectX::XMFLOAT4& diffuse, ID3D11Buffer* vb, ID3D11Buffer* ib,
                  UINT indexCount);

    int   m_mode = 0;     // 0=Q,1=W,2=E,3=R,4=T
    float m_time = 0.0f;
    bool  m_inited = false;

    OrbitCamera m_cam;
    int  m_prevMouseX = 0, m_prevMouseY = 0;
    int  m_prevWheel = 0;
    bool m_dragging = false;

    // GPU 리소스
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
    ComPtr<ID3D11InputLayout>  m_layout;
    ComPtr<ID3D11Buffer> m_cbFrame, m_cbObject, m_cbLight;
    ComPtr<ID3D11Buffer> m_sphereVB, m_sphereIB;
    ComPtr<ID3D11Buffer> m_planeVB, m_planeIB;
    UINT m_sphereIndexCount = 0, m_planeIndexCount = 0;
    ComPtr<ID3D11ShaderResourceView> m_diffuseSRV, m_normalSRV;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterState;
};
