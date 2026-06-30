#include "Scene06_ProceduralNoise.h"
#include "../Render/FullscreenPass.h"
#include "../DXTrace.h"
#include <d3dcompiler.h>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")
using namespace DirectX;

namespace
{
    const char g_PS[] = R"(
cbuffer CB : register(b0)
{
    float2 gRes; float gTime; float gMode;
    float gOctaves; float3 pad;
};
struct FQ { float4 pos:SV_Position; float2 uv:TEXCOORD0; };

float  hash1(float2 p){ return frac(sin(dot(p, float2(127.1,311.7))) * 43758.5453); }
float2 hash2(float2 p){ p = float2(dot(p,float2(127.1,311.7)), dot(p,float2(269.5,183.3))); return frac(sin(p)*43758.5453); }

float valueNoise(float2 p){
    float2 i=floor(p), f=frac(p); float2 u=f*f*(3.0-2.0*f);
    float a=hash1(i), b=hash1(i+float2(1,0)), c=hash1(i+float2(0,1)), d=hash1(i+float2(1,1));
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}
float perlinNoise(float2 p){
    float2 i=floor(p), f=frac(p); float2 u=f*f*(3.0-2.0*f);
    float2 g00=normalize(hash2(i)*2-1), g10=normalize(hash2(i+float2(1,0))*2-1);
    float2 g01=normalize(hash2(i+float2(0,1))*2-1), g11=normalize(hash2(i+float2(1,1))*2-1);
    float d00=dot(g00,f), d10=dot(g10,f-float2(1,0)), d01=dot(g01,f-float2(0,1)), d11=dot(g11,f-float2(1,1));
    return lerp(lerp(d00,d10,u.x), lerp(d01,d11,u.x), u.y)*0.5+0.5;
}
float fbm(float2 p, int oct){
    float v=0.0, a=0.5, fr=1.0;
    for(int i=0;i<oct;i++){ v+=a*valueNoise(p*fr); a*=0.5; fr*=2.0; }
    return v;
}

float4 PSMain(FQ i):SV_Target
{
    float aspect = gRes.x/gRes.y;
    float2 uv = i.uv;
    float2 p = float2(uv.x*aspect, uv.y);
    int oct = (int)gOctaves;

    if (gMode < 0.5)                 // 의사난수
    {
        float v = hash1(floor(p*18.0));
        return float4(v,v,v,1);
    }
    else if (gMode < 1.5)            // Value vs Perlin (좌우 분할)
    {
        float v = (uv.x < 0.5) ? valueNoise(p*9.0) : perlinNoise(p*9.0);
        float3 col = float3(v,v,v);
        if (abs(uv.x-0.5) < 0.0015) col = float3(0.9,0.7,0.2);   // 구분선
        return float4(col,1);
    }
    else if (gMode < 2.5)            // FBM
    {
        float v = fbm(p*4.0 + gTime*0.08, oct);
        float3 col = lerp(float3(0.1,0.3,0.6), float3(0.9,0.8,0.5), v);
        col = lerp(col, float3(1,1,1), smoothstep(0.85,1.0,v));
        return float4(col,1);
    }
    else if (gMode < 3.5)            // Domain Warp(좌) + Water Ripple(우)
    {
        if (uv.x < 0.5)
        {
            float2 q = float2(fbm(p*3.0, 4), fbm(p*3.0+float2(5.2,1.3), 4));
            float2 r = float2(fbm(p*3.0+4.0*q+float2(1.7,9.2)+gTime*0.08, 4),
                              fbm(p*3.0+4.0*q+float2(8.3,2.8)+gTime*0.08, 4));
            float v = fbm(p*3.0+4.0*r, 4);
            float3 col = lerp(float3(0.1,0.1,0.4), float3(0.95,0.6,0.1), clamp(v*v*4.0,0,1));
            return float4(col,1);
        }
        else
        {
            float2 c = float2(0.75, 0.5);                 // 우측 중앙
            float2 d = float2((uv.x-c.x)*aspect, uv.y-c.y);
            float dist = length(d);
            float ripple = sin(dist*60.0 - gTime*5.0)*0.5+0.5;
            ripple *= exp(-dist*4.0);
            return float4(ripple, ripple*0.8, ripple*0.6, 1);
        }
    }
    else                             // Truchet
    {
        float2 g = p*9.0;
        float2 cell = floor(g), local = frac(g);
        float r = step(0.5, hash1(cell));
        float2 q = r < 0.5 ? local : float2(1.0-local.x, local.y);
        float arc1 = abs(length(q) - 0.5);
        float arc2 = abs(length(q-float2(1,1)) - 0.5);
        float lw = min(arc1, arc2);
        float v = 1.0 - smoothstep(0.02, 0.06, lw);
        float3 col = lerp(float3(0.07,0.08,0.12), float3(0.3,0.85,0.9), v);
        return float4(col,1);
    }
}
)";

    struct CB { XMFLOAT2 res; float time; float mode; float octaves; XMFLOAT3 pad; };
}

void Scene06_ProceduralNoise::Init(const SceneContext& ctx)
{
    if (m_inited) return;
    ID3D11Device* d = ctx.device;
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> b, e;
    if (FAILED(D3DCompile(g_PS, sizeof(g_PS), "S6", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, b.GetAddressOf(), e.GetAddressOf())))
    { if (e) OutputDebugStringA((char*)e->GetBufferPointer()); return; }
    d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, m_ps.GetAddressOf());

    D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = sizeof(CB);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    d->CreateBuffer(&bd, nullptr, m_cb.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dsd = {}; dsd.DepthEnable = FALSE;
    d->CreateDepthStencilState(&dsd, m_depthOff.GetAddressOf());
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    d->CreateRasterizerState(&rd, m_rsNone.GetAddressOf());

    m_inited = true;
}

void Scene06_ProceduralNoise::Update(const SceneContext& ctx, float dt)
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
        // FBM 옥타브 조절
        if (kt->IsKeyPressed(Keyboard::OemPlus) || kt->IsKeyPressed(Keyboard::Add))
            m_octaves = std::min(8, m_octaves + 1);
        if (kt->IsKeyPressed(Keyboard::OemMinus) || kt->IsKeyPressed(Keyboard::Subtract))
            m_octaves = std::max(1, m_octaves - 1);
    }
}

void Scene06_ProceduralNoise::Render(const SceneContext& ctx)
{
    if (!m_inited) return;
    ID3D11DeviceContext* c = ctx.context;

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        CB* cb = (CB*)m.pData;
        cb->res = { (float)ctx.screenWidth, (float)ctx.screenHeight };
        cb->time = m_time; cb->mode = (float)m_mode; cb->octaves = (float)m_octaves;
        cb->pad = { 0,0,0 };
        c->Unmap(m_cb.Get(), 0);
    }

    c->OMSetDepthStencilState(m_depthOff.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    c->RSSetState(m_rsNone.Get());
    c->PSSetShader(m_ps.Get(), nullptr, 0);
    c->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    ctx.fsquad->Draw(c);
}

std::wstring Scene06_ProceduralNoise::HudText() const
{
    std::wstring s = L"서브모드:  Q=의사난수  W=Value/Perlin  E=FBM  R=DomainWarp/Ripple  T=Truchet\n";
    const wchar_t* n[] = { L"의사난수(sin 해시)", L"Value(좌) vs Perlin(우)",
                           L"FBM", L"Domain Warp(좌) / Water Ripple(우)", L"Truchet 패턴" };
    s += L"[현재: "; s += n[m_mode]; s += L"]";
    if (m_mode == 2) s += L"   +/- 로 옥타브 조절 (현재 " + std::to_wstring(m_octaves) + L")";
    return s;
}
