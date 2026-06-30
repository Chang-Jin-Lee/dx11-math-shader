//***************************************************************************************
// PrimitiveBatch2D.h
//
// 픽셀 좌표계(좌상단 0,0 / y는 아래로 증가) 기반의 단순한 2D 즉시 모드 배처.
// 라인 리스트와 채워진 삼각형 리스트를 모아 한 번에 그린다.
// Scene01~03의 2D 오버레이(충돌, 궤도, 곡선 등) 시각화에 공통으로 쓰인다.
//
// 셰이더는 런타임에 D3DCompile로 컴파일하므로 외부 파일/빌드 단계가 필요 없다.
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

class PrimitiveBatch2D
{
public:
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    // 한 프레임의 2D 그리기 시작/종료. End()에서 실제 드로우가 일어난다.
    void Begin(int screenWidth, int screenHeight);
    void End();

    // ---- 선(라인 리스트) ----
    void AddLine(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, DirectX::XMFLOAT4 color);
    void AddRectOutline(DirectX::XMFLOAT2 minP, DirectX::XMFLOAT2 maxP, DirectX::XMFLOAT4 color);
    void AddCircleOutline(DirectX::XMFLOAT2 center, float radius, DirectX::XMFLOAT4 color, int segments = 48);
    void AddPolyline(const DirectX::XMFLOAT2* pts, int count, DirectX::XMFLOAT4 color, bool closed);
    void AddArrow(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, DirectX::XMFLOAT4 color, float headSize = 14.0f);

    // ---- 채워진 도형(삼각형 리스트) ----
    void AddFilledTriangle(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, DirectX::XMFLOAT2 c, DirectX::XMFLOAT4 color);
    void AddFilledQuad(DirectX::XMFLOAT2 minP, DirectX::XMFLOAT2 maxP, DirectX::XMFLOAT4 color);
    void AddFilledConvexPolygon(const DirectX::XMFLOAT2* pts, int count, DirectX::XMFLOAT4 color);
    void AddFilledCircle(DirectX::XMFLOAT2 center, float radius, DirectX::XMFLOAT4 color, int segments = 48);

private:
    struct Vertex
    {
        DirectX::XMFLOAT2 pos;     // 픽셀 좌표
        DirectX::XMFLOAT4 color;
    };

    void Flush(D3D11_PRIMITIVE_TOPOLOGY topology, const std::vector<Vertex>& verts, UINT granularity);

    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11Buffer>            m_vertexBuffer;
    ComPtr<ID3D11Buffer>            m_cbuffer;
    ComPtr<ID3D11VertexShader>      m_vs;
    ComPtr<ID3D11PixelShader>       m_ps;
    ComPtr<ID3D11InputLayout>       m_layout;
    ComPtr<ID3D11BlendState>        m_blend;
    ComPtr<ID3D11RasterizerState>   m_raster;
    ComPtr<ID3D11DepthStencilState> m_depthDisabled;

    ID3D11DeviceContext* m_context = nullptr;

    std::vector<Vertex> m_lineVerts;
    std::vector<Vertex> m_triVerts;

    int  m_screenW = 1;
    int  m_screenH = 1;
    UINT m_capacity = 0;   // 정점 버퍼 용량(정점 개수)
};
