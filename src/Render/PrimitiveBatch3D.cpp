#include "PrimitiveBatch3D.h"
#include "../DXTrace.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")
using namespace DirectX;

namespace
{
    const char g_Src[] = R"(
cbuffer CB : register(b0) { row_major float4x4 gViewProj; };
struct VIn  { float3 pos:POSITION; float4 col:COLOR; };
struct VOut { float4 pos:SV_Position; float4 col:COLOR; };
VOut VSMain(VIn i){ VOut o; o.pos = mul(float4(i.pos,1.0), gViewProj); o.col = i.col; return o; }
float4 PSMain(VOut i):SV_Target { return i.col; }
)";
}

bool PrimitiveBatch3D::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_context = context;
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vs, ps, err;
    if (FAILED(D3DCompile(g_Src, sizeof(g_Src), "PB3D", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), err.GetAddressOf())))
    { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return false; }
    err.Reset();
    if (FAILED(D3DCompile(g_Src, sizeof(g_Src), "PB3D", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, ps.GetAddressOf(), err.GetAddressOf())))
    { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return false; }

    HR(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vs.GetAddressOf()));
    HR(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ps.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HR(device->CreateInputLayout(layout, ARRAYSIZE(layout), vs->GetBufferPointer(), vs->GetBufferSize(), m_layout.GetAddressOf()));

    m_capacity = 60000;
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DYNAMIC; vbd.ByteWidth = m_capacity * sizeof(Vertex);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER; vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR(device->CreateBuffer(&vbd, nullptr, m_vb.GetAddressOf()));

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.ByteWidth = sizeof(XMMATRIX);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR(device->CreateBuffer(&cbd, nullptr, m_cb.GetAddressOf()));

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

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    HR(device->CreateRasterizerState(&rd, m_raster.GetAddressOf()));

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS;
    HR(device->CreateDepthStencilState(&dsd, m_depth.GetAddressOf()));
    return true;
}

void PrimitiveBatch3D::Begin(const XMMATRIX& viewProj)
{
    m_lines.clear(); m_tris.clear();
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(m_context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        *reinterpret_cast<XMMATRIX*>(m.pData) = viewProj;
        m_context->Unmap(m_cb.Get(), 0);
    }
}

void PrimitiveBatch3D::End()
{
    UINT stride = sizeof(Vertex), offset = 0;
    m_context->IASetInputLayout(m_layout.Get());
    m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);
    float bf[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blend.Get(), bf, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(m_depth.Get(), 0);
    m_context->RSSetState(m_raster.Get());
    Flush(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, m_tris, 3);
    Flush(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, m_lines, 2);
}

void PrimitiveBatch3D::Flush(D3D11_PRIMITIVE_TOPOLOGY topo, const std::vector<Vertex>& v, UINT gran)
{
    if (v.empty()) return;
    m_context->IASetPrimitiveTopology(topo);
    UINT chunk = (m_capacity / gran) * gran, total = (UINT)v.size();
    for (UINT s = 0; s < total; s += chunk)
    {
        UINT n = (total - s < chunk) ? (total - s) : chunk;
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        memcpy(m.pData, v.data() + s, n * sizeof(Vertex));
        m_context->Unmap(m_vb.Get(), 0);
        m_context->Draw(n, 0);
    }
}

void PrimitiveBatch3D::AddLine(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT4& c)
{
    m_lines.push_back({ a, c }); m_lines.push_back({ b, c });
}

void PrimitiveBatch3D::AddTriangle(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT4& col)
{
    m_tris.push_back({ a, col }); m_tris.push_back({ b, col }); m_tris.push_back({ c, col });
}

void PrimitiveBatch3D::AddGrid(float halfSize, int divisions, const XMFLOAT4& color, float y)
{
    float step = (halfSize * 2.0f) / divisions;
    for (int i = 0; i <= divisions; ++i)
    {
        float t = -halfSize + i * step;
        AddLine({ t, y, -halfSize }, { t, y, halfSize }, color);
        AddLine({ -halfSize, y, t }, { halfSize, y, t }, color);
    }
}

namespace
{
    inline XMFLOAT3 TX(const XMMATRIX& m, float x, float y, float z)
    {
        XMVECTOR v = XMVector3TransformCoord(XMVectorSet(x, y, z, 1.0f), m);
        XMFLOAT3 r; XMStoreFloat3(&r, v); return r;
    }
}

void PrimitiveBatch3D::AddWireBox(const XMMATRIX& w, const XMFLOAT4& c)
{
    XMFLOAT3 p[8];
    int k = 0;
    for (int zi = -1; zi <= 1; zi += 2)
        for (int yi = -1; yi <= 1; yi += 2)
            for (int xi = -1; xi <= 1; xi += 2)
                p[k++] = TX(w, xi * 0.5f, yi * 0.5f, zi * 0.5f);
    // 인덱스: x(0,1) y(0,2) z(0,4) 비트 패턴
    int edges[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
    for (auto& e : edges) AddLine(p[e[0]], p[e[1]], c);
}

void PrimitiveBatch3D::AddSolidBox(const XMMATRIX& w)
{
    // 8 corners
    XMFLOAT3 p[8]; int k = 0;
    for (int zi = -1; zi <= 1; zi += 2)
        for (int yi = -1; yi <= 1; yi += 2)
            for (int xi = -1; xi <= 1; xi += 2)
                p[k++] = TX(w, xi * 0.5f, yi * 0.5f, zi * 0.5f);
    auto quad = [&](int a, int b, int c, int d, XMFLOAT4 col) {
        AddTriangle(p[a], p[b], p[c], col);
        AddTriangle(p[a], p[c], p[d], col);
    };
    XMFLOAT4 px = { 0.82f,0.34f,0.34f,1 }, nx = { 0.50f,0.22f,0.22f,1 };
    XMFLOAT4 py = { 0.36f,0.80f,0.40f,1 }, ny = { 0.22f,0.50f,0.26f,1 };
    XMFLOAT4 pz = { 0.34f,0.45f,0.85f,1 }, nz = { 0.22f,0.30f,0.55f,1 };
    // corners indexed by (x + 2y + 4z) with -1->0,+1->1
    quad(1, 5, 7, 3, px);  // +x
    quad(0, 2, 6, 4, nx);  // -x
    quad(2, 3, 7, 6, py);  // +y
    quad(0, 4, 5, 1, ny);  // -y
    quad(4, 6, 7, 5, pz);  // +z
    quad(0, 1, 3, 2, nz);  // -z
}

void PrimitiveBatch3D::AddAxis(const XMMATRIX& w, float len)
{
    XMFLOAT3 o = TX(w, 0, 0, 0);
    AddLine(o, TX(w, len, 0, 0), { 0.95f, 0.30f, 0.30f, 1 });
    AddLine(o, TX(w, 0, len, 0), { 0.35f, 0.90f, 0.40f, 1 });
    AddLine(o, TX(w, 0, 0, len), { 0.35f, 0.55f, 0.95f, 1 });
}
