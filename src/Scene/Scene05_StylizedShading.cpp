#include "Scene05_StylizedShading.h"
#include "../Render/ModelLoader.h"
#include "../d3dUtil.h"
#include "../DXTrace.h"
#include <d3dcompiler.h>
#include <Windows.h>
#include <string>
#include <cmath>

#pragma comment(lib, "d3dcompiler.lib")
using namespace DirectX;

namespace
{
    const char g_Shader[] = R"(
cbuffer CBFrame  : register(b0){ row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObject : register(b1){ row_major float4x4 gWorld; row_major float4x4 gWorldIT; };
cbuffer CBMat    : register(b2){
    float3 gLightDir; float gOutline;
    float4 gBase;
    float2 gTexel; float gUseTex; float gAlphaTest;
    float gCutoff; float3 gPad;
};
Texture2D gTex0 : register(t0);
Texture2D gTex1 : register(t1);
SamplerState gSamp : register(s0);

struct VIn  { float3 pos:POSITION; float3 nor:NORMAL; float2 uv:TEXCOORD; float4 tan:TANGENT; };
struct VOut { float4 svpos:SV_Position; float3 wnor:TEXCOORD0; float3 wpos:TEXCOORD1; float2 uv:TEXCOORD2; };

float4 Albedo(float2 uv)
{
    float4 a = gBase;
    if (gUseTex > 0.5) a *= gTex0.Sample(gSamp, uv);
    return a;
}

VOut VSMain(VIn v){
    VOut o; float4 wp = mul(float4(v.pos,1), gWorld);
    o.wpos = wp.xyz; o.svpos = mul(wp, gViewProj);
    o.wnor = mul(v.nor, (float3x3)gWorldIT); o.uv = v.uv; return o;
}
VOut VSOutline(VIn v){
    // 스무스 노말(tangent.xyz = 같은 위치 정점 법선 평균)로 밀어내 외피를 하나로 용접
    VOut o; float3 sn = normalize(mul(v.tan.xyz, (float3x3)gWorld));
    float4 wp = mul(float4(v.pos,1), gWorld); wp.xyz += sn * gOutline;
    o.wpos = wp.xyz; o.svpos = mul(wp, gViewProj);
    o.wnor = normalize(mul(v.nor,(float3x3)gWorldIT)); o.uv = v.uv; return o;
}

float Lum(float3 c){ return dot(c, float3(0.299,0.587,0.114)); }

float4 PSToon(VOut i):SV_Target{
    float4 alb=Albedo(i.uv);
    if(gAlphaTest>0.5) clip(alb.a-gCutoff);
    float3 N=normalize(i.wnor); float3 L=normalize(-gLightDir);
    float ndl=max(dot(N,L),0);
    float band = ndl>0.66 ? 1.0 : (ndl>0.33 ? 0.7 : 0.45);
    float3 col = alb.rgb*band;
    float3 V=normalize(gCamPos-i.wpos);
    col += pow(1-saturate(dot(N,V)),4)*0.15;     // 림
    return float4(col,1);
}
float4 PSFlat(VOut i):SV_Target{
    float4 alb=Albedo(i.uv);
    if(gAlphaTest>0.5) clip(alb.a-gCutoff);
    float3 N=normalize(i.wnor); float3 L=normalize(-gLightDir);
    float d=max(dot(N,L),0)*0.8+0.2;
    return float4(alb.rgb*d,1);
}
float4 PSOutline(VOut i):SV_Target{
    if(gAlphaTest>0.5){ float a=gBase.a; if(gUseTex>0.5) a*=gTex0.Sample(gSamp,i.uv).a; clip(a-gCutoff); }
    return float4(0.02,0.02,0.03,1);
}

float4 PSHatch(VOut i):SV_Target{
    float4 alb=Albedo(i.uv);
    if(gAlphaTest>0.5) clip(alb.a-gCutoff);
    float3 N=normalize(i.wnor); float3 L=normalize(-gLightDir);
    float lum=max(dot(N,L),0);
    float2 sp=i.svpos.xy; float h=1.0;
    float s1=step(0.5,frac((sp.x+sp.y)/11.0));
    float s2=step(0.5,frac((sp.x-sp.y)/11.0));
    float s3=step(0.5,frac((sp.x+sp.y)/5.5));
    if(lum<0.78) h=min(h,s1);
    if(lum<0.52) h=min(h,s2);
    if(lum<0.26) h=min(h,s3);
    float3 col=lerp(float3(0.09,0.09,0.11), float3(0.95,0.95,0.90), h);
    return float4(col,1);
}

// 노말+깊이를 오프스크린에 기록 (Sobel 외곽선용). rgb=월드노말, a=카메라 거리
float4 PSNormalDepth(VOut i):SV_Target{
    float4 alb=Albedo(i.uv);
    if(gAlphaTest>0.5) clip(alb.a-gCutoff);
    float3 N=normalize(i.wnor);
    float dist=length(gCamPos - i.wpos);
    return float4(N*0.5+0.5, dist);
}

struct FQ{ float4 pos:SV_Position; float2 uv:TEXCOORD0; };

// 깊이·법선 불연속을 검출해 외곽선을 합성 (gTex0=색, gTex1=노말+깊이)
float4 PSSobelOutline(FQ i):SV_Target{
    float3 col = gTex0.Sample(gSamp,i.uv).rgb;
    float2 ts = gTexel;
    float4 c =gTex1.Sample(gSamp,i.uv);
    float4 r =gTex1.Sample(gSamp,i.uv+float2(ts.x,0));
    float4 l =gTex1.Sample(gSamp,i.uv-float2(ts.x,0));
    float4 u =gTex1.Sample(gSamp,i.uv+float2(0,ts.y));
    float4 dn=gTex1.Sample(gSamp,i.uv-float2(0,ts.y));
    float3 nC=c.xyz*2-1, nR=r.xyz*2-1, nL=l.xyz*2-1, nU=u.xyz*2-1, nD=dn.xyz*2-1;
    float dN = (1-saturate(dot(nC,nR)))+(1-saturate(dot(nC,nL)))+(1-saturate(dot(nC,nU)))+(1-saturate(dot(nC,nD)));
    float dD = abs(c.a-r.a)+abs(c.a-l.a)+abs(c.a-u.a)+abs(c.a-dn.a);
    float e = saturate(dN*1.3 + dD*4.0);
    e = smoothstep(0.25, 0.6, e);
    return float4(lerp(col, float3(0.02,0.02,0.03), e), 1);
}

float4 PSSobel(FQ i):SV_Target{
    float2 ts=gTexel;
    float tl=Lum(gTex0.Sample(gSamp,i.uv+ts*float2(-1,-1)).rgb);
    float  l=Lum(gTex0.Sample(gSamp,i.uv+ts*float2(-1, 0)).rgb);
    float bl=Lum(gTex0.Sample(gSamp,i.uv+ts*float2(-1, 1)).rgb);
    float  t=Lum(gTex0.Sample(gSamp,i.uv+ts*float2( 0,-1)).rgb);
    float  b=Lum(gTex0.Sample(gSamp,i.uv+ts*float2( 0, 1)).rgb);
    float tr=Lum(gTex0.Sample(gSamp,i.uv+ts*float2( 1,-1)).rgb);
    float  r=Lum(gTex0.Sample(gSamp,i.uv+ts*float2( 1, 0)).rgb);
    float br=Lum(gTex0.Sample(gSamp,i.uv+ts*float2( 1, 1)).rgb);
    float gx=-tl-2*l-bl+tr+2*r+br;
    float gy= tl+2*t+tr-bl-2*b-br;
    float e=saturate(sqrt(gx*gx+gy*gy)*1.6);
    return float4(lerp(float3(0.97,0.97,0.95), float3(0.04,0.04,0.06), e),1);
}
)";

    struct CBFrame { XMMATRIX viewProj; XMFLOAT3 camPos; float time; };
    struct CBObject { XMMATRIX world; XMMATRIX worldIT; };
    struct CBMat {
        XMFLOAT3 lightDir; float outline;
        XMFLOAT4 base;
        XMFLOAT2 texel; float useTex; float alphaTest;
        float cutoff; XMFLOAT3 pad;
    };

    std::string ExeRelative(const char* rel)
    {
        char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf); auto pos = p.find_last_of("\\/");
        if (pos != std::string::npos) p = p.substr(0, pos + 1);
        return p + rel;
    }

    ID3D11PixelShader* MakePS(ID3D11Device* d, const char* entry)
    {
        UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        Microsoft::WRL::ComPtr<ID3DBlob> b, e;
        if (FAILED(D3DCompile(g_Shader, sizeof(g_Shader), "S5", nullptr, nullptr, entry, "ps_5_0", flags, 0, b.GetAddressOf(), e.GetAddressOf())))
        { if (e) OutputDebugStringA((char*)e->GetBufferPointer()); return nullptr; }
        ID3D11PixelShader* ps = nullptr;
        d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps);
        return ps;
    }
    ID3D11Buffer* DynCB(ID3D11Device* d, UINT bytes)
    {
        D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = bytes;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* b = nullptr; d->CreateBuffer(&bd, nullptr, &b); return b;
    }
}

void Scene05_StylizedShading::Init(const SceneContext& ctx)
{
    if (m_inited) return;
    ID3D11Device* d = ctx.device;
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // VS
    ComPtr<ID3DBlob> vsb, vso, err;
    D3DCompile(g_Shader, sizeof(g_Shader), "S5", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, vsb.GetAddressOf(), err.GetAddressOf());
    if (!vsb) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return; }
    err.Reset();
    D3DCompile(g_Shader, sizeof(g_Shader), "S5", nullptr, nullptr, "VSOutline", "vs_5_0", flags, 0, vso.GetAddressOf(), err.GetAddressOf());
    d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_vsMain.GetAddressOf());
    d->CreateVertexShader(vso->GetBufferPointer(), vso->GetBufferSize(), nullptr, m_vsOutline.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    d->CreateInputLayout(layout, ARRAYSIZE(layout), vsb->GetBufferPointer(), vsb->GetBufferSize(), m_layout.GetAddressOf());

    m_psToon.Attach(MakePS(d, "PSToon"));
    m_psOutline.Attach(MakePS(d, "PSOutline"));
    m_psFlat.Attach(MakePS(d, "PSFlat"));
    m_psHatch.Attach(MakePS(d, "PSHatch"));
    m_psSobel.Attach(MakePS(d, "PSSobel"));
    m_psNormalDepth.Attach(MakePS(d, "PSNormalDepth"));
    m_psSobelOutline.Attach(MakePS(d, "PSSobelOutline"));

    m_cbFrame.Attach(DynCB(d, sizeof(CBFrame)));
    m_cbObject.Attach(DynCB(d, sizeof(CBObject)));
    m_cbMat.Attach(DynCB(d, sizeof(CBMat)));

    // 래스터/깊이/샘플러
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    rd.CullMode = D3D11_CULL_BACK;  d->CreateRasterizerState(&rd, m_rsBack.GetAddressOf());
    rd.CullMode = D3D11_CULL_FRONT; d->CreateRasterizerState(&rd, m_rsFront.GetAddressOf());
    rd.CullMode = D3D11_CULL_NONE;  d->CreateRasterizerState(&rd, m_rsNone.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    d->CreateDepthStencilState(&dd, m_depthOn.GetAddressOf());
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d->CreateDepthStencilState(&dd, m_depthOff.GetAddressOf());

    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    d->CreateSamplerState(&sd, m_samp.GetAddressOf());

    m_fs.Init(d);

    // 모델 로드 (머티리얼별 서브메시 + base color 텍스처)
    model::Model mdl;
    std::string path = ExeRelative("assets\\character.glb");
    if (model::LoadGLB(path.c_str(), mdl) && !mdl.subs.empty())
    {
        // 디코드된 이미지 → SRV
        m_texSRVs.resize(mdl.images.size());
        for (size_t i = 0; i < mdl.images.size(); ++i)
        {
            const auto& im = mdl.images[i];
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = im.w; td.Height = im.h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = im.rgba.data(); sd.SysMemPitch = im.w * 4;
            ComPtr<ID3D11Texture2D> t;
            if (SUCCEEDED(d->CreateTexture2D(&td, &sd, t.GetAddressOf())))
                d->CreateShaderResourceView(t.Get(), nullptr, m_texSRVs[i].GetAddressOf());
        }

        for (const auto& sub : mdl.subs)
        {
            GpuSub g;
            D3D11_BUFFER_DESC vbd = {}; vbd.Usage = D3D11_USAGE_IMMUTABLE;
            vbd.ByteWidth = (UINT)(sub.vertices.size() * sizeof(VertexPNUT)); vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vsd = {}; vsd.pSysMem = sub.vertices.data();
            d->CreateBuffer(&vbd, &vsd, g.vb.GetAddressOf());

            D3D11_BUFFER_DESC ibd = {}; ibd.Usage = D3D11_USAGE_IMMUTABLE;
            ibd.ByteWidth = (UINT)(sub.indices.size() * sizeof(unsigned int)); ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA isd = {}; isd.pSysMem = sub.indices.data();
            d->CreateBuffer(&ibd, &isd, g.ib.GetAddressOf());

            g.count = (UINT)sub.indices.size();
            g.imageIndex = sub.imageIndex;
            g.base = sub.baseColor;
            g.alphaTest = sub.alphaTest;
            g.cutoff = sub.alphaCutoff;
            m_subs.push_back(std::move(g));
        }

        // 모델 맞추기: 중심을 원점으로, 높이 2.4로 스케일, 발을 y=0에
        XMFLOAT3 mn = mdl.min, mx = mdl.max;
        XMFLOAT3 c = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
        float height = (mx.y - mn.y); if (height < 1e-3f) height = 1.0f;
        float s = 2.4f / height;
        m_world = XMMatrixTranslation(-c.x, -c.y, -c.z) * XMMatrixScaling(s, s, s) * XMMatrixTranslation(0, (mx.y - mn.y) * 0.5f * s, 0);
        m_cam.target = { 0.0f, 1.2f, 0.0f };
        m_cam.radius = 4.0f;
        m_cam.elevation = 0.12f;
        m_cam.azimuth = 2.0f;   // 캐릭터 정면 3/4 뷰
        m_modelOK = true;
    }

    m_inited = true;
}

void Scene05_StylizedShading::Update(const SceneContext& ctx, float dt)
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
        else if (kt->IsKeyPressed(Keyboard::Y)) m_mode = 5;
    }
    int mx = ctx.mouse.x, my = ctx.mouse.y;
    if (ctx.mouse.leftButton)
    {
        if (m_dragging) m_cam.Rotate((mx - m_prevMouseX) * 0.01f, (my - m_prevMouseY) * 0.01f);
        m_dragging = true;
    }
    else m_dragging = false;
    m_prevMouseX = mx; m_prevMouseY = my;
    m_cam.Zoom((ctx.mouse.scrollWheelValue - m_prevWheel) / 120.0f * 0.6f);
    m_prevWheel = ctx.mouse.scrollWheelValue;
}

void Scene05_StylizedShading::UpdateFrameCB(const SceneContext& ctx)
{
    ID3D11DeviceContext* c = ctx.context;
    float aspect = (float)ctx.screenWidth / ctx.screenHeight;
    XMFLOAT3 eye = m_cam.Eye();
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(m_cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        CBFrame* f = (CBFrame*)m.pData;
        f->viewProj = m_cam.View() * m_cam.Proj(aspect);
        f->camPos = eye; f->time = m_time;
        c->Unmap(m_cbFrame.Get(), 0);
    }
    if (SUCCEEDED(c->Map(m_cbObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        CBObject* o = (CBObject*)m.pData;
        o->world = m_world; o->worldIT = InverseTranspose(m_world);
        c->Unmap(m_cbObject.Get(), 0);
    }
}

void Scene05_StylizedShading::DrawModel(ID3D11DeviceContext* c, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11RasterizerState* rs)
{
    UINT stride = sizeof(VertexPNUT), offset = 0;
    c->IASetInputLayout(m_layout.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs, nullptr, 0);
    c->PSSetShader(ps, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, m_cbFrame.GetAddressOf());
    c->VSSetConstantBuffers(1, 1, m_cbObject.GetAddressOf());
    c->VSSetConstantBuffers(2, 1, m_cbMat.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, m_cbFrame.GetAddressOf());
    c->PSSetConstantBuffers(2, 1, m_cbMat.GetAddressOf());
    c->PSSetSamplers(0, 1, m_samp.GetAddressOf());
    c->RSSetState(rs);

    XMVECTOR ld = XMVector3Normalize(XMVectorSet(-0.3f, -0.6f, -0.55f, 0));
    XMFLOAT3 lightDir; XMStoreFloat3(&lightDir, ld);

    for (const auto& g : m_subs)
    {
        ID3D11ShaderResourceView* srv = (g.imageIndex >= 0 && g.imageIndex < (int)m_texSRVs.size())
            ? m_texSRVs[g.imageIndex].Get() : nullptr;

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(c->Map(m_cbMat.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            CBMat* mat = (CBMat*)m.pData;
            mat->lightDir = lightDir;
            mat->outline = 0.012f;
            mat->base = g.base;
            mat->texel = m_texel;
            mat->useTex = srv ? 1.0f : 0.0f;
            mat->alphaTest = g.alphaTest ? 1.0f : 0.0f;
            mat->cutoff = g.cutoff;
            mat->pad = { 0,0,0 };
            c->Unmap(m_cbMat.Get(), 0);
        }
        c->PSSetShaderResources(0, 1, &srv);
        c->IASetVertexBuffers(0, 1, g.vb.GetAddressOf(), &stride, &offset);
        c->IASetIndexBuffer(g.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        c->DrawIndexed(g.count, 0, 0);
    }
}

void Scene05_StylizedShading::Render(const SceneContext& ctx)
{
    if (!m_inited || !m_modelOK) return;
    ID3D11DeviceContext* c = ctx.context;
    UpdateFrameCB(ctx);
    m_texel = { 1.0f / ctx.screenWidth, 1.0f / ctx.screenHeight };

    c->OMSetDepthStencilState(m_depthOn.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    if (m_mode == 0)         // Toon
        DrawModel(c, m_vsMain.Get(), m_psToon.Get(), m_rsNone.Get());
    else if (m_mode == 1)    // Outline + Flat
    {
        DrawModel(c, m_vsOutline.Get(), m_psOutline.Get(), m_rsFront.Get());
        DrawModel(c, m_vsMain.Get(), m_psFlat.Get(), m_rsNone.Get());
    }
    else if (m_mode == 2)    // Toon + Outline
    {
        DrawModel(c, m_vsOutline.Get(), m_psOutline.Get(), m_rsFront.Get());
        DrawModel(c, m_vsMain.Get(), m_psToon.Get(), m_rsNone.Get());
    }
    else if (m_mode == 4)    // Hatching
        DrawModel(c, m_vsMain.Get(), m_psHatch.Get(), m_rsNone.Get());
    else if (m_mode == 3)    // Sobel (오프스크린 → 풀스크린)
    {
        if (m_rt.EnsureSize(ctx.device, ctx.screenWidth, ctx.screenHeight))
        {
            float clear[4] = { 0.55f, 0.57f, 0.62f, 1.0f };
            m_rt.Begin(c, clear);
            DrawModel(c, m_vsMain.Get(), m_psFlat.Get(), m_rsNone.Get());

            // 백버퍼로 복귀
            c->OMSetRenderTargets(1, &ctx.backRTV, ctx.backDSV);
            D3D11_VIEWPORT vp{}; vp.Width = (float)ctx.screenWidth; vp.Height = (float)ctx.screenHeight; vp.MaxDepth = 1.0f;
            c->RSSetViewports(1, &vp);

            // Sobel용 CBMat (texel만 필요)
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(c->Map(m_cbMat.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            {
                CBMat* mat = (CBMat*)m.pData; *mat = {};
                mat->texel = m_texel;
                c->Unmap(m_cbMat.Get(), 0);
            }
            c->OMSetDepthStencilState(m_depthOff.Get(), 0);
            c->RSSetState(m_rsNone.Get());
            c->PSSetShader(m_psSobel.Get(), nullptr, 0);
            c->PSSetConstantBuffers(2, 1, m_cbMat.GetAddressOf());
            ID3D11ShaderResourceView* srv = m_rt.SRV();
            c->PSSetShaderResources(0, 1, &srv);
            c->PSSetSamplers(0, 1, m_samp.GetAddressOf());
            m_fs.Draw(c);
            ID3D11ShaderResourceView* nullSRV = nullptr;
            c->PSSetShaderResources(0, 1, &nullSRV);
        }
    }
    else if (m_mode == 5)    // Toon + Sobel 외곽선 (깊이·법선 불연속)
    {
        int W = ctx.screenWidth, H = ctx.screenHeight;
        if (m_rt.EnsureSize(ctx.device, W, H) &&
            m_rtNrm.EnsureSize(ctx.device, W, H, DXGI_FORMAT_R16G16B16A16_FLOAT, true))
        {
            // 1) 툰 색 → m_rt
            float bg[4] = { 0.07f, 0.08f, 0.10f, 1.0f };
            m_rt.Begin(c, bg);
            c->OMSetDepthStencilState(m_depthOn.Get(), 0);
            DrawModel(c, m_vsMain.Get(), m_psToon.Get(), m_rsNone.Get());
            // 2) 노말+깊이 → m_rtNrm (배경 깊이는 크게 → 실루엣 검출)
            float clrN[4] = { 0.5f, 0.5f, 0.5f, 1000.0f };
            m_rtNrm.Begin(c, clrN);
            c->OMSetDepthStencilState(m_depthOn.Get(), 0);
            DrawModel(c, m_vsMain.Get(), m_psNormalDepth.Get(), m_rsNone.Get());

            // 3) 백버퍼로 복귀 + Sobel 외곽선 합성
            c->OMSetRenderTargets(1, &ctx.backRTV, ctx.backDSV);
            D3D11_VIEWPORT vp{}; vp.Width = (float)W; vp.Height = (float)H; vp.MaxDepth = 1.0f;
            c->RSSetViewports(1, &vp);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(c->Map(m_cbMat.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            {
                CBMat* mat = (CBMat*)m.pData; *mat = {}; mat->texel = m_texel;
                c->Unmap(m_cbMat.Get(), 0);
            }
            c->OMSetDepthStencilState(m_depthOff.Get(), 0);
            c->RSSetState(m_rsNone.Get());
            c->PSSetShader(m_psSobelOutline.Get(), nullptr, 0);
            c->PSSetConstantBuffers(2, 1, m_cbMat.GetAddressOf());
            ID3D11ShaderResourceView* srvs[2] = { m_rt.SRV(), m_rtNrm.SRV() };
            c->PSSetShaderResources(0, 2, srvs);
            c->PSSetSamplers(0, 1, m_samp.GetAddressOf());
            m_fs.Draw(c);
            ID3D11ShaderResourceView* nn[2] = { nullptr, nullptr };
            c->PSSetShaderResources(0, 2, nn);
        }
    }
}

std::wstring Scene05_StylizedShading::HudText() const
{
    if (!m_modelOK)
        return L"[오류] assets/character.glb 를 찾을 수 없습니다. (exe 옆 assets 폴더 확인)";
    std::wstring s = L"서브모드:  Q=Toon  W=Outline  E=Toon+Outline  R=Sobel  T=Hatching  Y=Sobel외곽선   |   드래그:공전 휠:줌\n";
    const wchar_t* n[] = { L"Toon(셀 셰이딩)", L"Outline(뒷면 확장, 스무스 노말)", L"Toon+Outline",
                           L"Sobel 엣지(스케치)", L"Hatching(사선)", L"Toon + Sobel 외곽선(깊이·법선 검출)" };
    s += L"[현재: "; s += n[m_mode]; s += L"]  대상: VRoid 캐릭터(.glb, base color 텍스처 적용)";
    return s;
}
