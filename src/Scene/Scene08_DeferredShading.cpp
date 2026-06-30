#include "Scene08_DeferredShading.h"
#include "../Render/Geometry.h"
#include "../Render/PrimitiveBatch3D.h"
#include "../Render/FullscreenPass.h"
#include "../d3dUtil.h"
#include "../DXTrace.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3dcompiler.lib")
using namespace DirectX;

namespace
{
    // Geometry Pass: 씬 → G-Buffer 3개(MRT)
    const char g_Geom[] = R"(
cbuffer CBGeom:register(b0){ row_major float4x4 gVP; row_major float4x4 gWorld; row_major float4x4 gWIT; float4 gAlbedo; };
struct VIn{ float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD; float4 t:TANGENT; };
struct VOut{ float4 sv:SV_Position; float3 wp:TEXCOORD0; float3 wn:TEXCOORD1; };
VOut VSMain(VIn v){ VOut o; float4 wp=mul(float4(v.p,1),gWorld); o.wp=wp.xyz; o.sv=mul(wp,gVP); o.wn=mul(v.n,(float3x3)gWIT); return o; }
struct MRT{ float4 albedo:SV_Target0; float4 normal:SV_Target1; float4 wpos:SV_Target2; };
MRT PSMain(VOut i){
    MRT o;
    o.albedo = float4(gAlbedo.rgb, 1);
    o.normal = float4(normalize(i.wn)*0.5+0.5, 0);   // [0,1] 인코딩
    o.wpos   = float4(i.wp, 1);
    return o;
}
)";

    // Lighting Pass: 전체화면, G-Buffer + 8 점광원
    const char g_Light[] = R"(
cbuffer CBLight:register(b0){
    float3 gCam; float gNum;
    float4 gLPos[8];   // xyz, w=range
    float4 gLCol[8];   // rgb, w=intensity
    float gDebug; float3 pad;
};
Texture2D gAlb:register(t0); Texture2D gNor:register(t1); Texture2D gPos:register(t2);
SamplerState gS:register(s0);
struct FQ{ float4 pos:SV_Position; float2 uv:TEXCOORD0; };

float3 shade(float2 uv){
    float3 alb=gAlb.Sample(gS,uv).rgb;
    float3 N=normalize(gNor.Sample(gS,uv).xyz*2.0-1.0);
    float3 wp=gPos.Sample(gS,uv).xyz;
    float3 V=normalize(gCam-wp);
    float3 total=alb*0.05;
    int n=(int)gNum;
    for(int k=0;k<n;k++){
        float3 L=gLPos[k].xyz-wp; float d=length(L); L/=max(d,0.0001);
        float at=saturate(1.0 - d/gLPos[k].w); at*=at;
        float diff=saturate(dot(N,L));
        float3 R=reflect(-L,N); float sp=pow(saturate(dot(R,V)),48.0);
        total += (alb*diff + sp)*gLCol[k].rgb*gLCol[k].w*at;
    }
    return total;
}
float4 PSMain(FQ i):SV_Target{
    if(gDebug>0.5){
        float2 uv=i.uv;
        if(uv.x<0.5 && uv.y<0.5)            return float4(gAlb.Sample(gS,uv*2.0).rgb,1);
        else if(uv.x>=0.5 && uv.y<0.5)      return float4(gNor.Sample(gS,(uv-float2(0.5,0))*2.0).xyz,1);
        else if(uv.x<0.5 && uv.y>=0.5){ float3 wp=gPos.Sample(gS,(uv-float2(0,0.5))*2.0).xyz; return float4(frac(wp*0.25),1); }
        else { float3 c=shade((uv-0.5)*2.0); c=c/(c+1.0); return float4(pow(c,1.0/2.2),1); }
    }
    float3 c=shade(i.uv); c=c/(c+1.0);
    return float4(pow(c,1.0/2.2),1);
}
)";

    struct CBGeom { XMMATRIX vp, world, wit; XMFLOAT4 albedo; };
    struct CBLight { XMFLOAT3 cam; float num; XMFLOAT4 lpos[8]; XMFLOAT4 lcol[8]; float debug; XMFLOAT3 pad; };

    ID3D11Buffer* Imm(ID3D11Device* d, const void* p, UINT bytes, UINT bind) {
        D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.ByteWidth = bytes; bd.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = p; ID3D11Buffer* b = nullptr; d->CreateBuffer(&bd, &sd, &b); return b;
    }
    ID3D11Buffer* Dyn(ID3D11Device* d, UINT bytes) {
        D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = bytes;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* b = nullptr; d->CreateBuffer(&bd, nullptr, &b); return b;
    }

    // 광원 색상 팔레트
    const XMFLOAT3 kColors[8] = {
        {1.0f,0.3f,0.3f}, {0.3f,1.0f,0.4f}, {0.4f,0.5f,1.0f}, {1.0f,0.9f,0.3f},
        {1.0f,0.4f,0.9f}, {0.3f,1.0f,0.95f}, {1.0f,0.6f,0.25f}, {0.7f,0.5f,1.0f},
    };
}

bool Scene08_DeferredShading::EnsureGBuffer(ID3D11Device* d, int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    if (m_gw == w && m_gh == h && m_gTex[0]) return true;
    m_gw = w; m_gh = h;
    for (int i = 0; i < 3; ++i) { m_gTex[i].Reset(); m_gRTV[i].Reset(); m_gSRV[i].Reset(); }
    m_gDepth.Reset(); m_gDSV.Reset();

    DXGI_FORMAT fmt[3] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT };
    for (int i = 0; i < 3; ++i)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt[i]; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d->CreateTexture2D(&td, nullptr, m_gTex[i].GetAddressOf()))) return false;
        d->CreateRenderTargetView(m_gTex[i].Get(), nullptr, m_gRTV[i].GetAddressOf());
        d->CreateShaderResourceView(m_gTex[i].Get(), nullptr, m_gSRV[i].GetAddressOf());
    }
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(d->CreateTexture2D(&dd, nullptr, m_gDepth.GetAddressOf()))) return false;
    d->CreateDepthStencilView(m_gDepth.Get(), nullptr, m_gDSV.GetAddressOf());
    return true;
}

void Scene08_DeferredShading::Init(const SceneContext& ctx)
{
    if (m_inited) return;
    ID3D11Device* d = ctx.device;
    UINT f = 0;
#if defined(DEBUG)||defined(_DEBUG)
    f |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vsb, psb, lpb, err;
    D3DCompile(g_Geom, sizeof(g_Geom), "geom", nullptr, nullptr, "VSMain", "vs_5_0", f, 0, vsb.GetAddressOf(), err.GetAddressOf());
    if (!vsb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }
    err.Reset();
    D3DCompile(g_Geom, sizeof(g_Geom), "geom", nullptr, nullptr, "PSMain", "ps_5_0", f, 0, psb.GetAddressOf(), err.GetAddressOf());
    if (!psb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }
    err.Reset();
    D3DCompile(g_Light, sizeof(g_Light), "light", nullptr, nullptr, "PSMain", "ps_5_0", f, 0, lpb.GetAddressOf(), err.GetAddressOf());
    if (!lpb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }

    d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_geomVS.GetAddressOf());
    d->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_geomPS.GetAddressOf());
    d->CreatePixelShader(lpb->GetBufferPointer(), lpb->GetBufferSize(), nullptr, m_lightPS.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    d->CreateInputLayout(layout, ARRAYSIZE(layout), vsb->GetBufferPointer(), vsb->GetBufferSize(), m_layout.GetAddressOf());

    MeshData sp = geo::MakeSphere(1.0f, 32, 24), pl = geo::MakePlane(8.0f, 1.0f);
    m_sphereCount = (UINT)sp.indices.size(); m_planeCount = (UINT)pl.indices.size();
    m_sphereVB.Attach(Imm(d, sp.vertices.data(), (UINT)(sp.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_sphereIB.Attach(Imm(d, sp.indices.data(), (UINT)(sp.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));
    m_planeVB.Attach(Imm(d, pl.vertices.data(), (UINT)(pl.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_planeIB.Attach(Imm(d, pl.indices.data(), (UINT)(pl.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));

    m_cbGeom.Attach(Dyn(d, sizeof(CBGeom)));
    m_cbLight.Attach(Dyn(d, sizeof(CBLight)));

    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    d->CreateSamplerState(&sd, m_samp.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    d->CreateDepthStencilState(&dd, m_depthOn.GetAddressOf());
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d->CreateDepthStencilState(&dd, m_depthOff.GetAddressOf());
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.DepthClipEnable = TRUE; rd.MultisampleEnable = FALSE;
    rd.CullMode = D3D11_CULL_BACK; d->CreateRasterizerState(&rd, m_rsBack.GetAddressOf());
    rd.CullMode = D3D11_CULL_NONE; d->CreateRasterizerState(&rd, m_rsNone.GetAddressOf());

    m_cam.target = { 0, 0.8f, 0 }; m_cam.radius = 11.0f; m_cam.elevation = 0.55f; m_cam.azimuth = 0.9f;
    m_inited = true;
}

void Scene08_DeferredShading::Update(const SceneContext& ctx, float dt)
{
    m_time += dt;
    auto* kt = ctx.keyboardTracker;
    if (kt)
    {
        if (kt->IsKeyPressed(Keyboard::F)) m_debug = !m_debug;
        if (kt->IsKeyPressed(Keyboard::OemPlus) || kt->IsKeyPressed(Keyboard::Add)) m_numLights = std::min(8, m_numLights + 1);
        if (kt->IsKeyPressed(Keyboard::OemMinus) || kt->IsKeyPressed(Keyboard::Subtract)) m_numLights = std::max(1, m_numLights - 1);
    }
    int mx = ctx.mouse.x, my = ctx.mouse.y;
    if (ctx.mouse.leftButton) { if (m_dragging) m_cam.Rotate((mx - m_prevMouseX) * 0.01f, (my - m_prevMouseY) * 0.01f); m_dragging = true; }
    else m_dragging = false;
    m_prevMouseX = mx; m_prevMouseY = my;
    m_cam.Zoom((ctx.mouse.scrollWheelValue - m_prevWheel) / 120.0f * 0.8f); m_prevWheel = ctx.mouse.scrollWheelValue;
}

void Scene08_DeferredShading::DrawMesh(ID3D11DeviceContext* c, const XMMATRIX& vp, const XMMATRIX& world,
    const XMFLOAT4& albedo, ID3D11Buffer* vb, ID3D11Buffer* ib, UINT n)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(m_cbGeom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CBGeom* g = (CBGeom*)m.pData; g->vp = vp; g->world = world; g->wit = InverseTranspose(world); g->albedo = albedo;
        c->Unmap(m_cbGeom.Get(), 0);
    }
    UINT st = sizeof(VertexPNUT), of = 0;
    c->IASetVertexBuffers(0, 1, &vb, &st, &of); c->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    c->DrawIndexed(n, 0, 0);
}

void Scene08_DeferredShading::Render(const SceneContext& ctx)
{
    if (!m_inited) return;
    ID3D11DeviceContext* c = ctx.context;
    int W = ctx.screenWidth, H = ctx.screenHeight;
    if (!EnsureGBuffer(ctx.device, W, H)) return;
    float aspect = (float)W / H;
    XMMATRIX vp = m_cam.View() * m_cam.Proj(aspect);
    XMFLOAT3 eye = m_cam.Eye();

    // 광원 위치/색
    XMFLOAT4 lpos[8], lcol[8];
    for (int k = 0; k < 8; ++k)
    {
        float ph = m_time * 0.6f + k * (XM_2PI / 8);
        float rad = 5.0f;
        lpos[k] = { rad * std::cos(ph), 1.3f + 0.8f * std::sin(m_time + k), rad * std::sin(ph), 6.0f };
        lcol[k] = { kColors[k].x, kColors[k].y, kColors[k].z, 1.6f };
    }

    // ---- Geometry Pass → G-Buffer ----
    ID3D11RenderTargetView* rtvs[3] = { m_gRTV[0].Get(), m_gRTV[1].Get(), m_gRTV[2].Get() };
    float clr[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 3; ++i) c->ClearRenderTargetView(m_gRTV[i].Get(), clr);
    c->ClearDepthStencilView(m_gDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c->OMSetRenderTargets(3, rtvs, m_gDSV.Get());
    D3D11_VIEWPORT gvp{}; gvp.Width = (float)W; gvp.Height = (float)H; gvp.MaxDepth = 1.0f; c->RSSetViewports(1, &gvp);

    c->OMSetDepthStencilState(m_depthOn.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    c->RSSetState(m_rsBack.Get());
    c->IASetInputLayout(m_layout.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(m_geomVS.Get(), nullptr, 0);
    c->PSSetShader(m_geomPS.Get(), nullptr, 0);
    c->VSSetConstantBuffers(0, 1, m_cbGeom.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, m_cbGeom.GetAddressOf());

    DrawMesh(c, vp, XMMatrixTranslation(0, 0, 0), XMFLOAT4(0.55f, 0.55f, 0.58f, 1), m_planeVB.Get(), m_planeIB.Get(), m_planeCount);
    for (int gx = -1; gx <= 1; ++gx)
        for (int gz = -1; gz <= 1; ++gz)
            DrawMesh(c, vp, XMMatrixTranslation(gx * 3.0f, 0.8f, gz * 3.0f), XMFLOAT4(0.8f, 0.8f, 0.82f, 1),
                m_sphereVB.Get(), m_sphereIB.Get(), m_sphereCount);

    // ---- Lighting Pass (전체화면 → 백버퍼) ----
    c->OMSetRenderTargets(1, &ctx.backRTV, nullptr);
    c->RSSetViewports(1, &gvp);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(m_cbLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CBLight* l = (CBLight*)m.pData;
        l->cam = eye; l->num = (float)m_numLights;
        for (int k = 0; k < 8; ++k) { l->lpos[k] = lpos[k]; l->lcol[k] = lcol[k]; }
        l->debug = m_debug ? 1.0f : 0.0f; l->pad = { 0,0,0 };
        c->Unmap(m_cbLight.Get(), 0);
    }
    c->OMSetDepthStencilState(m_depthOff.Get(), 0);
    c->RSSetState(m_rsNone.Get());
    c->PSSetShader(m_lightPS.Get(), nullptr, 0);
    c->PSSetConstantBuffers(0, 1, m_cbLight.GetAddressOf());
    ID3D11ShaderResourceView* srvs[3] = { m_gSRV[0].Get(), m_gSRV[1].Get(), m_gSRV[2].Get() };
    c->PSSetShaderResources(0, 3, srvs);
    c->PSSetSamplers(0, 1, m_samp.GetAddressOf());
    ctx.fsquad->Draw(c);
    ID3D11ShaderResourceView* n3[3] = { nullptr,nullptr,nullptr };
    c->PSSetShaderResources(0, 3, n3);

    // ---- 광원 마커 (디버그가 아닐 때) ----
    if (!m_debug)
    {
        ctx.batch3d->Begin(vp);
        for (int k = 0; k < m_numLights; ++k)
        {
            XMMATRIX w = XMMatrixScaling(0.25f, 0.25f, 0.25f) * XMMatrixTranslation(lpos[k].x, lpos[k].y, lpos[k].z);
            ctx.batch3d->AddWireBox(w, XMFLOAT4(lcol[k].x, lcol[k].y, lcol[k].z, 1));
        }
        ctx.batch3d->End();
    }
}

std::wstring Scene08_DeferredShading::HudText() const
{
    std::wstring s = L"G-Buffer(MRT 3장: Albedo/Normal/WorldPos) → 전체화면 Lighting(점광원 누적)\n";
    s += L"F: G-Buffer 시각화(4분할) ";
    s += m_debug ? L"[ON]" : L"[OFF]";
    s += L"   +/-: 광원 수 (현재 " + std::to_wstring(m_numLights) + L")   드래그:공전";
    return s;
}
