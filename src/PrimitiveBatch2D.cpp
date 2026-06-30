#include "PrimitiveBatch2D.h"
#include "DXTrace.h"
#include "d3dUtil.h"
#include <cmath>

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    // 픽셀 좌표 → NDC 변환 + 정점 색 통과. b0에 화면 역크기를 넘긴다.
    const char g_ShaderSrc[] = R"(
cbuffer CB : register(b0)
{
    float2 gInvScreen;   // (1/width, 1/height)
    float2 gPad;
};
struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };

VSOut VSMain(VSIn vin)
{
    VSOut o;
    float2 ndc;
    ndc.x = vin.pos.x * gInvScreen.x * 2.0 - 1.0;
    ndc.y = 1.0 - vin.pos.y * gInvScreen.y * 2.0;   // y는 아래로 증가하는 픽셀 좌표
    o.pos = float4(ndc, 0.0, 1.0);
    o.col = vin.col;
    return o;
}
float4 PSMain(VSOut pin) : SV_TARGET { return pin.col; }
)";

    struct CBData
    {
        float invW;
        float invH;
        float pad0;
        float pad1;
    };
}

bool PrimitiveBatch2D::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_context = context;

    // --- 셰이더 런타임 컴파일 ---
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

    HRESULT hr = D3DCompile(g_ShaderSrc, sizeof(g_ShaderSrc), "PrimitiveBatch2D",
        nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr))
    {
        if (errBlob) OutputDebugStringA(reinterpret_cast<const char*>(errBlob->GetBufferPointer()));
        return false;
    }
    errBlob.Reset();
    hr = D3DCompile(g_ShaderSrc, sizeof(g_ShaderSrc), "PrimitiveBatch2D",
        nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr))
    {
        if (errBlob) OutputDebugStringA(reinterpret_cast<const char*>(errBlob->GetBufferPointer()));
        return false;
    }

    HR(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.GetAddressOf()));
    HR(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HR(device->CreateInputLayout(layout, ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_layout.GetAddressOf()));

    // --- 동적 정점 버퍼 ---
    m_capacity = 30000;
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.ByteWidth = m_capacity * sizeof(Vertex);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR(device->CreateBuffer(&vbd, nullptr, m_vertexBuffer.GetAddressOf()));

    // --- 상수 버퍼 ---
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(CBData);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR(device->CreateBuffer(&cbd, nullptr, m_cbuffer.GetAddressOf()));

    // --- 알파 블렌드(채워진 반투명 도형용) ---
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HR(device->CreateBlendState(&bd, m_blend.GetAddressOf()));

    // --- 래스터라이저(컬링 없음, MSAA 라인) ---
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    HR(device->CreateRasterizerState(&rd, m_raster.GetAddressOf()));

    // --- 깊이 비활성(2D 오버레이) ---
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dsd.StencilEnable = FALSE;
    HR(device->CreateDepthStencilState(&dsd, m_depthDisabled.GetAddressOf()));

    return true;
}

void PrimitiveBatch2D::Begin(int screenWidth, int screenHeight)
{
    m_screenW = screenWidth > 0 ? screenWidth : 1;
    m_screenH = screenHeight > 0 ? screenHeight : 1;
    m_lineVerts.clear();
    m_triVerts.clear();
}

void PrimitiveBatch2D::End()
{
    // 파이프라인 상태 설정
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        CBData* cb = reinterpret_cast<CBData*>(mapped.pData);
        cb->invW = 1.0f / static_cast<float>(m_screenW);
        cb->invH = 1.0f / static_cast<float>(m_screenH);
        cb->pad0 = cb->pad1 = 0.0f;
        m_context->Unmap(m_cbuffer.Get(), 0);
    }

    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    m_context->IASetInputLayout(m_layout.Get());
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_cbuffer.GetAddressOf());
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend.Get(), blendFactor, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(m_depthDisabled.Get(), 0);
    m_context->RSSetState(m_raster.Get());

    // 채워진 도형을 먼저, 선을 그 위에 그린다(테두리가 위로 보이도록)
    Flush(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, m_triVerts, 3);
    Flush(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, m_lineVerts, 2);

    m_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(nullptr, 0);
}

void PrimitiveBatch2D::Flush(D3D11_PRIMITIVE_TOPOLOGY topology, const std::vector<Vertex>& verts, UINT granularity)
{
    if (verts.empty()) return;
    m_context->IASetPrimitiveTopology(topology);

    // 용량(capacity)을 초과하면 granularity(라인=2, 삼각형=3) 단위로 나눠 그린다
    UINT chunk = (m_capacity / granularity) * granularity;
    UINT total = static_cast<UINT>(verts.size());
    for (UINT start = 0; start < total; start += chunk)
    {
        UINT count = (total - start < chunk) ? (total - start) : chunk;
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        memcpy(mapped.pData, verts.data() + start, count * sizeof(Vertex));
        m_context->Unmap(m_vertexBuffer.Get(), 0);
        m_context->Draw(count, 0);
    }
}

// ----------------------------------------------------------------------------
// 선
// ----------------------------------------------------------------------------
void PrimitiveBatch2D::AddLine(XMFLOAT2 a, XMFLOAT2 b, XMFLOAT4 color)
{
    m_lineVerts.push_back({ a, color });
    m_lineVerts.push_back({ b, color });
}

void PrimitiveBatch2D::AddRectOutline(XMFLOAT2 minP, XMFLOAT2 maxP, XMFLOAT4 color)
{
    XMFLOAT2 p0 = { minP.x, minP.y };
    XMFLOAT2 p1 = { maxP.x, minP.y };
    XMFLOAT2 p2 = { maxP.x, maxP.y };
    XMFLOAT2 p3 = { minP.x, maxP.y };
    AddLine(p0, p1, color);
    AddLine(p1, p2, color);
    AddLine(p2, p3, color);
    AddLine(p3, p0, color);
}

void PrimitiveBatch2D::AddCircleOutline(XMFLOAT2 center, float radius, XMFLOAT4 color, int segments)
{
    if (segments < 3) segments = 3;
    XMFLOAT2 prev = { center.x + radius, center.y };
    for (int i = 1; i <= segments; ++i)
    {
        float t = XM_2PI * static_cast<float>(i) / static_cast<float>(segments);
        XMFLOAT2 cur = { center.x + radius * std::cos(t), center.y + radius * std::sin(t) };
        AddLine(prev, cur, color);
        prev = cur;
    }
}

void PrimitiveBatch2D::AddPolyline(const XMFLOAT2* pts, int count, XMFLOAT4 color, bool closed)
{
    for (int i = 0; i + 1 < count; ++i)
        AddLine(pts[i], pts[i + 1], color);
    if (closed && count > 2)
        AddLine(pts[count - 1], pts[0], color);
}

void PrimitiveBatch2D::AddArrow(XMFLOAT2 from, XMFLOAT2 to, XMFLOAT4 color, float headSize)
{
    AddLine(from, to, color);
    // 화살촉
    XMFLOAT2 dir = { to.x - from.x, to.y - from.y };
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-4f) return;
    dir.x /= len; dir.y /= len;
    XMFLOAT2 perp = { -dir.y, dir.x };
    XMFLOAT2 back = { to.x - dir.x * headSize, to.y - dir.y * headSize };
    XMFLOAT2 left = { back.x + perp.x * headSize * 0.5f, back.y + perp.y * headSize * 0.5f };
    XMFLOAT2 right = { back.x - perp.x * headSize * 0.5f, back.y - perp.y * headSize * 0.5f };
    AddLine(to, left, color);
    AddLine(to, right, color);
}

// ----------------------------------------------------------------------------
// 채워진 도형
// ----------------------------------------------------------------------------
void PrimitiveBatch2D::AddFilledTriangle(XMFLOAT2 a, XMFLOAT2 b, XMFLOAT2 c, XMFLOAT4 color)
{
    m_triVerts.push_back({ a, color });
    m_triVerts.push_back({ b, color });
    m_triVerts.push_back({ c, color });
}

void PrimitiveBatch2D::AddFilledQuad(XMFLOAT2 minP, XMFLOAT2 maxP, XMFLOAT4 color)
{
    XMFLOAT2 p0 = { minP.x, minP.y };
    XMFLOAT2 p1 = { maxP.x, minP.y };
    XMFLOAT2 p2 = { maxP.x, maxP.y };
    XMFLOAT2 p3 = { minP.x, maxP.y };
    AddFilledTriangle(p0, p1, p2, color);
    AddFilledTriangle(p0, p2, p3, color);
}

void PrimitiveBatch2D::AddFilledConvexPolygon(const XMFLOAT2* pts, int count, XMFLOAT4 color)
{
    // 삼각형 팬
    for (int i = 1; i + 1 < count; ++i)
        AddFilledTriangle(pts[0], pts[i], pts[i + 1], color);
}

void PrimitiveBatch2D::AddFilledCircle(XMFLOAT2 center, float radius, XMFLOAT4 color, int segments)
{
    if (segments < 3) segments = 3;
    XMFLOAT2 prev = { center.x + radius, center.y };
    for (int i = 1; i <= segments; ++i)
    {
        float t = XM_2PI * static_cast<float>(i) / static_cast<float>(segments);
        XMFLOAT2 cur = { center.x + radius * std::cos(t), center.y + radius * std::sin(t) };
        AddFilledTriangle(center, prev, cur, color);
        prev = cur;
    }
}
