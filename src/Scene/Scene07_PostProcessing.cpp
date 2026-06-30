#include "Scene07_PostProcessing.h"
#include "../Render/Geometry.h"
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
    // 씬: HDR Phong (강한 점광원 → 하이라이트가 1.0 초과)
    const char g_Scene[] = R"(
cbuffer CBScene:register(b0){
    row_major float4x4 gVP; row_major float4x4 gWorld; row_major float4x4 gWIT;
    float3 gCam; float gT; float3 gLightPos; float gLightInt; float4 gBase;
};
struct VIn{ float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD; float4 t:TANGENT; };
struct VOut{ float4 sv:SV_Position; float3 wp:TEXCOORD0; float3 wn:TEXCOORD1; };
VOut VSMain(VIn v){ VOut o; float4 wp=mul(float4(v.p,1),gWorld); o.wp=wp.xyz; o.sv=mul(wp,gVP); o.wn=mul(v.n,(float3x3)gWIT); return o; }
float4 PSMain(VOut i):SV_Target{
    float3 N=normalize(i.wn); float3 L=gLightPos-i.wp; float d=length(L); L/=d;
    float3 V=normalize(gCam-i.wp); float3 R=reflect(-L,N);
    float atten=gLightInt/(1.0+0.08*d*d);
    float diff=max(dot(N,L),0); float spec=pow(max(dot(R,V),0),80);
    float3 col=gBase.rgb*diff*atten + spec*atten*1.5 + gBase.rgb*0.04;
    return float4(col,1);
}
)";

    // 포스트: 전체화면 패스들
    const char g_Post[] = R"(
cbuffer CBPost:register(b0){ float2 gTexel; float gExposure; float gRadius; float gThreshold; float gBloom; float gMode; float gPad; };
Texture2D gTex:register(t0); Texture2D gTex1:register(t1); SamplerState gS:register(s0);
struct FQ{ float4 pos:SV_Position; float2 uv:TEXCOORD0; };

float3 Reinhard(float3 h){ return h/(h+1.0); }
float3 ACES(float3 x){ float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14; return saturate((x*(a*x+b))/(x*(c*x+d)+e)); }

float4 blur(FQ i, float2 dir){
    int r=(int)gRadius; float s=max(gRadius*0.5,0.5);
    float4 sum=0; float wsum=0;
    for(int k=-r;k<=r;k++){ float w=exp(-(k*k)/(2.0*s*s)); sum+=gTex.Sample(gS,i.uv+dir*k)*w; wsum+=w; }
    return sum/wsum;
}
float4 PS_BlurH(FQ i):SV_Target{ return blur(i, float2(gTexel.x,0)); }
float4 PS_BlurV(FQ i):SV_Target{ return blur(i, float2(0,gTexel.y)); }

float4 PS_Bilateral(FQ i):SV_Target{
    float4 center=gTex.Sample(gS,i.uv); float4 sum=0; float wsum=0;
    float sigS=3.0, sigR=0.3;
    for(int dy=-3;dy<=3;dy++) for(int dx=-3;dx<=3;dx++){
        float2 off=float2(dx,dy)*gTexel; float4 nb=gTex.Sample(gS,i.uv+off);
        float wS=exp(-(dx*dx+dy*dy)/(2*sigS*sigS));
        float cd=length(nb.rgb-center.rgb); float wR=exp(-(cd*cd)/(2*sigR*sigR));
        float w=wS*wR; sum+=nb*w; wsum+=w;
    }
    return sum/wsum;
}
float4 PS_Bright(FQ i):SV_Target{
    float4 c=gTex.Sample(gS,i.uv); float l=dot(c.rgb,float3(0.2126,0.7152,0.0722));
    return c*step(gThreshold,l);
}
float4 PS_Composite(FQ i):SV_Target{  // hdr(t0)+bloom(t1) → ACES → gamma
    float3 hdr=gTex.Sample(gS,i.uv).rgb; float3 bl=gTex1.Sample(gS,i.uv).rgb;
    float3 c=ACES((hdr+bl*gBloom)*gExposure);
    return float4(pow(c,1.0/2.2),1);
}
float4 PS_Final(FQ i):SV_Target{
    float3 hdr=gTex.Sample(gS,i.uv).rgb*gExposure;
    if(gMode<0.5){                         // 블러/일반 결과 → ACES+gamma
        return float4(pow(ACES(hdr),1.0/2.2),1);
    } else if(gMode<1.5){                   // 감마 비교 (좌:미적용 우:적용)
        float3 c=saturate(hdr);
        float3 o=(i.uv.x<0.5)? c : pow(c,1.0/2.2);
        if(abs(i.uv.x-0.5)<0.0012) o=float3(0.9,0.7,0.2);
        return float4(o,1);
    } else {                                // 톤매핑 비교 (좌:Reinhard 우:ACES)
        float3 c=(i.uv.x<0.5)? Reinhard(hdr) : ACES(hdr);
        c=pow(c,1.0/2.2);
        if(abs(i.uv.x-0.5)<0.0012) c=float3(0.9,0.7,0.2);
        return float4(c,1);
    }
}
)";

    struct CBScene { XMMATRIX vp, world, wit; XMFLOAT3 cam; float t; XMFLOAT3 lightPos; float lightInt; XMFLOAT4 base; };
    struct CBPost { XMFLOAT2 texel; float exposure; float radius; float threshold; float bloom; float mode; float pad; };

    ID3D11Buffer* Imm(ID3D11Device* d, const void* p, UINT bytes, UINT bind) {
        D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.ByteWidth = bytes; bd.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = p; ID3D11Buffer* b = nullptr; d->CreateBuffer(&bd, &sd, &b); return b;
    }
    ID3D11Buffer* Dyn(ID3D11Device* d, UINT bytes) {
        D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = bytes;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* b = nullptr; d->CreateBuffer(&bd, nullptr, &b); return b;
    }
    ID3D11PixelShader* PS(ID3D11Device* d, const char* src, size_t n, const char* e) {
        UINT f = 0;
#if defined(DEBUG)||defined(_DEBUG)
        f |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        Microsoft::WRL::ComPtr<ID3DBlob> b, er;
        if (FAILED(D3DCompile(src, n, "post", nullptr, nullptr, e, "ps_5_0", f, 0, b.GetAddressOf(), er.GetAddressOf())))
        { if (er) OutputDebugStringA((char*)er->GetBufferPointer()); return nullptr; }
        ID3D11PixelShader* ps = nullptr; d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps); return ps;
    }
}

void Scene07_PostProcessing::Init(const SceneContext& ctx)
{
    if (m_inited) return;
    ID3D11Device* d = ctx.device;
    UINT f = 0;
#if defined(DEBUG)||defined(_DEBUG)
    f |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    // 씬 셰이더
    ComPtr<ID3DBlob> vsb, psb, err;
    D3DCompile(g_Scene, sizeof(g_Scene), "scene", nullptr, nullptr, "VSMain", "vs_5_0", f, 0, vsb.GetAddressOf(), err.GetAddressOf());
    if (!vsb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }
    err.Reset();
    D3DCompile(g_Scene, sizeof(g_Scene), "scene", nullptr, nullptr, "PSMain", "ps_5_0", f, 0, psb.GetAddressOf(), err.GetAddressOf());
    if (!psb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }
    d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_sceneVS.GetAddressOf());
    d->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_scenePS.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    d->CreateInputLayout(layout, ARRAYSIZE(layout), vsb->GetBufferPointer(), vsb->GetBufferSize(), m_layout.GetAddressOf());

    MeshData sp = geo::MakeSphere(1.0f, 48, 32), pl = geo::MakePlane(6.0f, 1.0f);
    m_sphereCount = (UINT)sp.indices.size(); m_planeCount = (UINT)pl.indices.size();
    m_sphereVB.Attach(Imm(d, sp.vertices.data(), (UINT)(sp.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_sphereIB.Attach(Imm(d, sp.indices.data(), (UINT)(sp.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));
    m_planeVB.Attach(Imm(d, pl.vertices.data(), (UINT)(pl.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_planeIB.Attach(Imm(d, pl.indices.data(), (UINT)(pl.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));

    m_cbScene.Attach(Dyn(d, sizeof(CBScene)));
    m_cbPost.Attach(Dyn(d, sizeof(CBPost)));

    m_psBlurH.Attach(PS(d, g_Post, sizeof(g_Post), "PS_BlurH"));
    m_psBlurV.Attach(PS(d, g_Post, sizeof(g_Post), "PS_BlurV"));
    m_psBilateral.Attach(PS(d, g_Post, sizeof(g_Post), "PS_Bilateral"));
    m_psBright.Attach(PS(d, g_Post, sizeof(g_Post), "PS_Bright"));
    m_psComposite.Attach(PS(d, g_Post, sizeof(g_Post), "PS_Composite"));
    m_psFinal.Attach(PS(d, g_Post, sizeof(g_Post), "PS_Final"));

    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    d->CreateSamplerState(&sd, m_samp.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    d->CreateDepthStencilState(&dd, m_depthOn.GetAddressOf());
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d->CreateDepthStencilState(&dd, m_depthOff.GetAddressOf());
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    rd.CullMode = D3D11_CULL_BACK; d->CreateRasterizerState(&rd, m_rsBack.GetAddressOf());
    rd.CullMode = D3D11_CULL_NONE; d->CreateRasterizerState(&rd, m_rsNone.GetAddressOf());

    m_cam.target = { 0, 0.8f, 0 }; m_cam.radius = 6.0f; m_cam.elevation = 0.25f; m_cam.azimuth = 0.7f;
    m_inited = true;
}

void Scene07_PostProcessing::Update(const SceneContext& ctx, float dt)
{
    m_time += dt;
    auto* kt = ctx.keyboardTracker;
    if (kt)
    {
        if (kt->IsKeyPressed(Keyboard::Q)) m_mode = 0;
        else if (kt->IsKeyPressed(Keyboard::W)) m_mode = 1;
        else if (kt->IsKeyPressed(Keyboard::E)) m_mode = 2;
        else if (kt->IsKeyPressed(Keyboard::R)) m_mode = 3;
        else if (kt->IsKeyPressed(Keyboard::T)) m_mode = 4;
        if (kt->IsKeyPressed(Keyboard::OemCloseBrackets)) m_radius = std::min(9, m_radius + 1);
        if (kt->IsKeyPressed(Keyboard::OemOpenBrackets))  m_radius = std::max(1, m_radius - 1);
        if (kt->IsKeyPressed(Keyboard::X)) m_exposure = std::min(8.0f, m_exposure + 0.3f);
        if (kt->IsKeyPressed(Keyboard::Z)) m_exposure = std::max(0.1f, m_exposure - 0.3f);
    }
    int mx = ctx.mouse.x, my = ctx.mouse.y;
    if (ctx.mouse.leftButton) { if (m_dragging) m_cam.Rotate((mx - m_prevMouseX) * 0.01f, (my - m_prevMouseY) * 0.01f); m_dragging = true; }
    else m_dragging = false;
    m_prevMouseX = mx; m_prevMouseY = my;
    m_cam.Zoom((ctx.mouse.scrollWheelValue - m_prevWheel) / 120.0f * 0.6f); m_prevWheel = ctx.mouse.scrollWheelValue;
}

void Scene07_PostProcessing::RenderSceneToHDR(const SceneContext& ctx)
{
    ID3D11DeviceContext* c = ctx.context;
    float aspect = (float)ctx.screenWidth / ctx.screenHeight;
    float clr[4] = { 0.02f, 0.02f, 0.03f, 1 };
    m_hdr.Begin(c, clr);

    c->OMSetDepthStencilState(m_depthOn.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    c->RSSetState(m_rsBack.Get());
    c->IASetInputLayout(m_layout.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(m_sceneVS.Get(), nullptr, 0);
    c->PSSetShader(m_scenePS.Get(), nullptr, 0);
    c->VSSetConstantBuffers(0, 1, m_cbScene.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, m_cbScene.GetAddressOf());

    XMFLOAT3 eye = m_cam.Eye();
    XMMATRIX vp = m_cam.View() * m_cam.Proj(aspect);
    XMFLOAT3 lpos = { 2.5f * std::cos(m_time * 0.8f), 2.2f, 2.5f * std::sin(m_time * 0.8f) };

    auto draw = [&](XMMATRIX world, XMFLOAT4 base, ID3D11Buffer* vb, ID3D11Buffer* ib, UINT n) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(c->Map(m_cbScene.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            CBScene* s = (CBScene*)m.pData;
            s->vp = vp; s->world = world; s->wit = InverseTranspose(world);
            s->cam = eye; s->t = m_time; s->lightPos = lpos; s->lightInt = 7.0f; s->base = base;
            c->Unmap(m_cbScene.Get(), 0);
        }
        UINT st = sizeof(VertexPNUT), of = 0;
        c->IASetVertexBuffers(0, 1, &vb, &st, &of); c->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        c->DrawIndexed(n, 0, 0);
    };
    draw(XMMatrixTranslation(0, 0, 0), XMFLOAT4(0.25f, 0.27f, 0.32f, 1), m_planeVB.Get(), m_planeIB.Get(), m_planeCount);
    draw(XMMatrixTranslation(0, 1.0f, 0), XMFLOAT4(0.9f, 0.55f, 0.3f, 1), m_sphereVB.Get(), m_sphereIB.Get(), m_sphereCount);
}

void Scene07_PostProcessing::FullscreenTo(const SceneContext& ctx, ID3D11RenderTargetView* rtv, int w, int h,
    ID3D11PixelShader* ps, ID3D11ShaderResourceView* s0, ID3D11ShaderResourceView* s1)
{
    ID3D11DeviceContext* c = ctx.context;
    c->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{}; vp.Width = (float)w; vp.Height = (float)h; vp.MaxDepth = 1.0f; c->RSSetViewports(1, &vp);
    c->OMSetDepthStencilState(m_depthOff.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    c->RSSetState(m_rsNone.Get());
    c->PSSetShader(ps, nullptr, 0);
    c->PSSetConstantBuffers(0, 1, m_cbPost.GetAddressOf());
    ID3D11ShaderResourceView* srvs[2] = { s0, s1 };
    c->PSSetShaderResources(0, 2, srvs);
    c->PSSetSamplers(0, 1, m_samp.GetAddressOf());
    ctx.fsquad->Draw(c);
    ID3D11ShaderResourceView* nn[2] = { nullptr, nullptr };
    c->PSSetShaderResources(0, 2, nn);
}

void Scene07_PostProcessing::Render(const SceneContext& ctx)
{
    if (!m_inited) return;
    ID3D11DeviceContext* c = ctx.context;
    int W = ctx.screenWidth, H = ctx.screenHeight;
    const DXGI_FORMAT HDRF = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_hdr.EnsureSize(ctx.device, W, H, HDRF, true);
    m_rtA.EnsureSize(ctx.device, W, H, HDRF, false);
    m_rtB.EnsureSize(ctx.device, W, H, HDRF, false);

    auto mapPost = [&](float radius, float exposure, float threshold, float bloom, float mode) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(c->Map(m_cbPost.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            CBPost* p = (CBPost*)m.pData;
            p->texel = { 1.0f / W, 1.0f / H }; p->exposure = exposure; p->radius = radius;
            p->threshold = threshold; p->bloom = bloom; p->mode = mode; p->pad = 0;
            c->Unmap(m_cbPost.Get(), 0);
        }
    };

    RenderSceneToHDR(ctx);

    if (m_mode == 0)        // Gaussian Blur (분리형 2-pass) → Final(ACES)
    {
        mapPost((float)m_radius, m_exposure, 0, 0, 0);
        FullscreenTo(ctx, m_rtA.RTV(), W, H, m_psBlurH.Get(), m_hdr.SRV());
        FullscreenTo(ctx, m_rtB.RTV(), W, H, m_psBlurV.Get(), m_rtA.SRV());
        FullscreenTo(ctx, ctx.backRTV, W, H, m_psFinal.Get(), m_rtB.SRV());
    }
    else if (m_mode == 1)   // Bilateral → Final
    {
        mapPost(0, m_exposure, 0, 0, 0);
        FullscreenTo(ctx, m_rtA.RTV(), W, H, m_psBilateral.Get(), m_hdr.SRV());
        FullscreenTo(ctx, ctx.backRTV, W, H, m_psFinal.Get(), m_rtA.SRV());
    }
    else if (m_mode == 2)   // Gamma 비교 (split)
    {
        mapPost(0, m_exposure, 0, 0, 1);
        FullscreenTo(ctx, ctx.backRTV, W, H, m_psFinal.Get(), m_hdr.SRV());
    }
    else if (m_mode == 3)   // Tone Mapping 비교 (Reinhard|ACES, split)
    {
        mapPost(0, m_exposure, 0, 0, 2);
        FullscreenTo(ctx, ctx.backRTV, W, H, m_psFinal.Get(), m_hdr.SRV());
    }
    else                    // Bloom
    {
        mapPost(6.0f, m_exposure, 1.0f, 1.2f, 0);
        FullscreenTo(ctx, m_rtA.RTV(), W, H, m_psBright.Get(), m_hdr.SRV());   // bright-pass
        FullscreenTo(ctx, m_rtB.RTV(), W, H, m_psBlurH.Get(), m_rtA.SRV());    // blur H
        FullscreenTo(ctx, m_rtA.RTV(), W, H, m_psBlurV.Get(), m_rtB.SRV());    // blur V
        FullscreenTo(ctx, ctx.backRTV, W, H, m_psComposite.Get(), m_hdr.SRV(), m_rtA.SRV()); // hdr+bloom
    }

    // 백버퍼 깊이 복원(다음 프레임/다른 패스 안전)
    c->OMSetRenderTargets(1, &ctx.backRTV, ctx.backDSV);
}

std::wstring Scene07_PostProcessing::HudText() const
{
    std::wstring s = L"서브모드:  Q=GaussianBlur  W=Bilateral  E=Gamma  R=ToneMap  T=Bloom   |   드래그:공전\n";
    switch (m_mode)
    {
    case 0: s += L"[Gaussian Blur] 분리형 2-pass.  [ / ] 로 반경 (현재 " + std::to_wstring(m_radius) + L")"; break;
    case 1: s += L"[Bilateral] 엣지 보존 블러"; break;
    case 2: s += L"[Gamma] 좌: 미적용(어두움) / 우: sRGB 감마 적용"; break;
    case 3: s += L"[Tone Mapping] 좌: Reinhard / 우: ACES.  Z/X 노출 (현재 " + std::to_wstring((int)(m_exposure * 100) / 100.0f).substr(0, 4) + L")"; break;
    default: s += L"[Bloom] Bright-pass → Blur → 합성.  Z/X 노출"; break;
    }
    return s;
}
