//***************************************************************************************
// PrimitiveBatch3D.h
//
// 월드 공간 3D 라인/도형 즉시 모드 배처. viewProj 행렬로 변환하며 깊이 테스트한다.
// 격자·축 기즈모·와이어/솔리드 박스 등 디버그·교육용 3D 프리미티브에 사용.
// Scene03(행렬·투영), Scene08(디퍼드 광원 표시) 등에서 공용.
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

class PrimitiveBatch3D
{
public:
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    void Begin(const DirectX::XMMATRIX& viewProj);
    void End();   // 누적된 삼각형 → 라인 순으로 그린다(현재 바인딩된 뷰포트에)

    void AddLine(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT4& color);
    void AddTriangle(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT4& color);

    // 단위 큐브([-0.5,0.5]^3)에 world를 적용
    void AddWireBox(const DirectX::XMMATRIX& world, const DirectX::XMFLOAT4& color);
    void AddSolidBox(const DirectX::XMMATRIX& world);   // 면을 축 방향 색으로 칠해 방향이 보이게

    // XZ 평면 격자 (y 높이)
    void AddGrid(float halfSize, int divisions, const DirectX::XMFLOAT4& color, float y = 0.0f);

    // world의 원점에서 X(빨강)/Y(초록)/Z(파랑) 축
    void AddAxis(const DirectX::XMMATRIX& world, float length);

private:
    struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT4 color; };
    void Flush(D3D11_PRIMITIVE_TOPOLOGY topo, const std::vector<Vertex>& v, UINT gran);

    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11Buffer> m_vb, m_cb;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11BlendState> m_blend;
    ComPtr<ID3D11RasterizerState> m_raster;
    ComPtr<ID3D11DepthStencilState> m_depth;
    ID3D11DeviceContext* m_context = nullptr;
    std::vector<Vertex> m_lines, m_tris;
    UINT m_capacity = 0;
};
