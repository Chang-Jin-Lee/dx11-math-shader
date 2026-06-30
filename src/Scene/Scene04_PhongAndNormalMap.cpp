#include "Scene04_PhongAndNormalMap.h"
#include "../Render/Geometry.h"
#include "../Render/ProceduralTexture.h"
#include "../d3dUtil.h"
#include "../DXTrace.h"
#include <d3dcompiler.h>
#include <cmath>

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    // 행렬은 row_major로 선언해 CPU(XMMATRIX, 행우선)에서 전치 없이 그대로 업로드한다.
    const char g_Shader[] = R"(
cbuffer CBFrame : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamPos; float gTime;
};
cbuffer CBObject : register(b1)
{
    row_major float4x4 gWorld;
    row_major float4x4 gWorldInvT;
    float4 gMatDiffuse;
};
cbuffer CBLight : register(b2)
{
    float3 gLightPos; float gMode;
    float3 gLightColor; float gPad;
    float4 gAmbient;
    float4 gSpecular;   // w = shininess
    float4 gEmissive;
};
Texture2D    gDiffuseTex : register(t0);
Texture2D    gNormalTex  : register(t1);
SamplerState gSamp       : register(s0);

struct VSIn  { float3 pos:POSITION; float3 nor:NORMAL; float2 uv:TEXCOORD; float4 tan:TANGENT; };
struct VSOut {
    float4 svpos:SV_Position; float3 wpos:TEXCOORD0; float3 wnor:TEXCOORD1;
    float2 uv:TEXCOORD2; float3 wtan:TEXCOORD3; float wsign:TEXCOORD4;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 wp = mul(float4(v.pos,1.0), gWorld);
    o.wpos  = wp.xyz;
    o.svpos = mul(wp, gViewProj);
    o.wnor  = mul(v.nor, (float3x3)gWorldInvT);
    o.wtan  = mul(v.tan.xyz, (float3x3)gWorld);
    o.wsign = v.tan.w;
    o.uv    = v.uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 N = normalize(i.wnor);
    float3 albedo = gMatDiffuse.rgb;

    if (gMode >= 3.5)   // T : Normal Mapping
    {
        float3 tn = gNormalTex.Sample(gSamp, i.uv).xyz * 2.0 - 1.0;
        float3 T = normalize(i.wtan - N * dot(i.wtan, N));
        float3 B = cross(N, T) * i.wsign;
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tn, TBN));
        albedo = gDiffuseTex.Sample(gSamp, i.uv).rgb;
    }

    float3 L = normalize(gLightPos - i.wpos);
    float3 V = normalize(gCamPos - i.wpos);
    float3 Rv = reflect(-L, N);

    float  d  = max(dot(N, L), 0.0);
    float3 diffuse  = albedo * gLightColor * d;
    float  s  = pow(max(dot(Rv, V), 0.0), max(gSpecular.w, 1.0));
    float3 specular = gSpecular.rgb * gLightColor * s;
    float3 ambient  = gAmbient.rgb * albedo;
    float3 emissive = gEmissive.rgb;

    float3 col = diffuse;
    if (gMode >= 0.5) col += specular;   // W,E,R,T
    if (gMode >= 1.5) col += ambient;    // E,R,T
    if (gMode >= 2.5) col += emissive;   // R,T
    return float4(col, 1.0);
}
)";

    struct CBFrame { XMMATRIX viewProj; XMFLOAT3 camPos; float time; };
    struct CBObject { XMMATRIX world; XMMATRIX worldInvT; XMFLOAT4 matDiffuse; };
    struct CBLight {
        XMFLOAT3 lightPos; float mode;
        XMFLOAT3 lightColor; float pad;
        XMFLOAT4 ambient;
        XMFLOAT4 specular;
        XMFLOAT4 emissive;
    };

    ID3D11Buffer* CreateImmutable(ID3D11Device* dev, const void* data, UINT bytes, UINT bind)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = bytes;
        bd.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = data;
        ID3D11Buffer* b = nullptr;
        dev->CreateBuffer(&bd, &sd, &b);
        return b;
    }
    ID3D11Buffer* CreateDynamicCB(ID3D11Device* dev, UINT bytes)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = bytes;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* b = nullptr;
        dev->CreateBuffer(&bd, nullptr, &b);
        return b;
    }
}

void Scene04_PhongAndNormalMap::Init(const SceneContext& ctx)
{
    if (m_inited) return;
    ID3D11Device* dev = ctx.device;

    // --- 셰이더 컴파일 ---
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(g_Shader, sizeof(g_Shader), "Scene04", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vsb.GetAddressOf(), err.GetAddressOf())))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return;
    }
    err.Reset();
    if (FAILED(D3DCompile(g_Shader, sizeof(g_Shader), "Scene04", nullptr, nullptr,
        "PSMain", "ps_5_0", flags, 0, psb.GetAddressOf(), err.GetAddressOf())))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return;
    }
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_vs.GetAddressOf());
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_ps.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(layout, ARRAYSIZE(layout), vsb->GetBufferPointer(), vsb->GetBufferSize(), m_layout.GetAddressOf());

    // --- 메시 ---
    MeshData sphere = geo::MakeSphere(1.0f, 48, 32);
    MeshData plane = geo::MakePlane(6.0f, 6.0f);
    m_sphereIndexCount = (UINT)sphere.indices.size();
    m_planeIndexCount = (UINT)plane.indices.size();
    m_sphereVB.Attach(CreateImmutable(dev, sphere.vertices.data(), (UINT)(sphere.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_sphereIB.Attach(CreateImmutable(dev, sphere.indices.data(), (UINT)(sphere.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));
    m_planeVB.Attach(CreateImmutable(dev, plane.vertices.data(), (UINT)(plane.vertices.size() * sizeof(VertexPNUT)), D3D11_BIND_VERTEX_BUFFER));
    m_planeIB.Attach(CreateImmutable(dev, plane.indices.data(), (UINT)(plane.indices.size() * sizeof(unsigned int)), D3D11_BIND_INDEX_BUFFER));

    // --- 상수 버퍼 ---
    m_cbFrame.Attach(CreateDynamicCB(dev, sizeof(CBFrame)));
    m_cbObject.Attach(CreateDynamicCB(dev, sizeof(CBObject)));
    m_cbLight.Attach(CreateDynamicCB(dev, sizeof(CBLight)));

    // --- 절차적 벽돌 텍스처 ---
    tex::CreateBrickTextures(dev, m_diffuseSRV, m_normalSRV, 256);

    D3D11_SAMPLER_DESC sampd = {};
    sampd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampd.AddressU = sampd.AddressV = sampd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sampd, m_sampler.GetAddressOf());

    // --- 파이프라인 상태 ---
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    dev->CreateDepthStencilState(&dsd, m_depthState.GetAddressOf());

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    dev->CreateRasterizerState(&rd, m_rasterState.GetAddressOf());

    m_inited = true;
}

void Scene04_PhongAndNormalMap::Update(const SceneContext& ctx, float dt)
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
    }

    // 카메라: 좌드래그 회전, 휠 줌
    int mx = ctx.mouse.x, my = ctx.mouse.y;
    if (ctx.mouse.leftButton)
    {
        if (m_dragging)
            m_cam.Rotate((mx - m_prevMouseX) * 0.01f, (my - m_prevMouseY) * 0.01f);
        m_dragging = true;
    }
    else m_dragging = false;
    m_prevMouseX = mx; m_prevMouseY = my;

    int wheel = ctx.mouse.scrollWheelValue;
    m_cam.Zoom((wheel - m_prevWheel) / 120.0f * 0.8f);
    m_prevWheel = wheel;
}

void Scene04_PhongAndNormalMap::DrawMesh(ID3D11DeviceContext* c, const XMMATRIX& world,
    const XMFLOAT4& diffuse, ID3D11Buffer* vb, ID3D11Buffer* ib, UINT indexCount)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(m_cbObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        CBObject* o = (CBObject*)m.pData;
        o->world = world;
        o->worldInvT = InverseTranspose(world);
        o->matDiffuse = diffuse;
        c->Unmap(m_cbObject.Get(), 0);
    }
    UINT stride = sizeof(VertexPNUT), offset = 0;
    c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    c->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    c->VSSetConstantBuffers(1, 1, m_cbObject.GetAddressOf());
    c->PSSetConstantBuffers(1, 1, m_cbObject.GetAddressOf());
    c->DrawIndexed(indexCount, 0, 0);
}

void Scene04_PhongAndNormalMap::Render(const SceneContext& ctx)
{
    if (!m_inited) return;
    ID3D11DeviceContext* c = ctx.context;
    float aspect = ctx.screenHeight > 0 ? (float)ctx.screenWidth / ctx.screenHeight : 1.7778f;

    XMFLOAT3 eye = m_cam.Eye();
    XMFLOAT3 lightPos = { 4.0f * std::cos(m_time * 0.7f), 3.5f, 4.0f * std::sin(m_time * 0.7f) };

    // CBFrame
    D3D11_MAPPED_SUBRESOURCE mp;
    if (SUCCEEDED(c->Map(m_cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mp)))
    {
        CBFrame* f = (CBFrame*)mp.pData;
        f->viewProj = m_cam.View() * m_cam.Proj(aspect);
        f->camPos = eye;
        f->time = m_time;
        c->Unmap(m_cbFrame.Get(), 0);
    }
    // CBLight (재질 specular/ambient/emissive 공용)
    if (SUCCEEDED(c->Map(m_cbLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mp)))
    {
        CBLight* l = (CBLight*)mp.pData;
        l->lightPos = lightPos;
        l->mode = (float)m_mode;
        l->lightColor = { 1.0f, 0.97f, 0.9f };
        l->pad = 0;
        l->ambient = { 0.22f, 0.24f, 0.30f, 1.0f };
        l->specular = { 1.0f, 1.0f, 1.0f, 64.0f };   // w = shininess
        l->emissive = { 0.10f, 0.04f, 0.16f, 1.0f };
        c->Unmap(m_cbLight.Get(), 0);
    }

    // 파이프라인 상태
    c->OMSetDepthStencilState(m_depthState.Get(), 0);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    c->RSSetState(m_rasterState.Get());
    c->IASetInputLayout(m_layout.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(m_vs.Get(), nullptr, 0);
    c->PSSetShader(m_ps.Get(), nullptr, 0);
    c->VSSetConstantBuffers(0, 1, m_cbFrame.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, m_cbFrame.GetAddressOf());
    c->PSSetConstantBuffers(2, 1, m_cbLight.GetAddressOf());
    ID3D11ShaderResourceView* srvs[2] = { m_diffuseSRV.Get(), m_normalSRV.Get() };
    c->PSSetShaderResources(0, 2, srvs);
    c->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

    // 바닥
    DrawMesh(c, XMMatrixTranslation(0, 0, 0), XMFLOAT4(0.30f, 0.32f, 0.38f, 1.0f),
        m_planeVB.Get(), m_planeIB.Get(), m_planeIndexCount);
    // 구체
    DrawMesh(c, XMMatrixTranslation(0, 1.0f, 0), XMFLOAT4(0.85f, 0.85f, 0.90f, 1.0f),
        m_sphereVB.Get(), m_sphereIB.Get(), m_sphereIndexCount);
    // 광원 마커(작은 구체)
    DrawMesh(c, XMMatrixScaling(0.15f, 0.15f, 0.15f) * XMMatrixTranslation(lightPos.x, lightPos.y, lightPos.z),
        XMFLOAT4(1.0f, 0.92f, 0.4f, 1.0f), m_sphereVB.Get(), m_sphereIB.Get(), m_sphereIndexCount);
}

std::wstring Scene04_PhongAndNormalMap::HudText() const
{
    std::wstring s = L"서브모드:  Q=Diffuse  W=+Specular  E=+Ambient  R=전체Phong  T=NormalMap\n";
    s += L"마우스 좌드래그: 카메라 공전  |  휠: 줌    ";
    const wchar_t* names[] = { L"Diffuse(난반사)", L"+ Specular(정반사)", L"+ Ambient(주변광)",
                               L"전체 Phong(+Emissive)", L"Normal Mapping(벽돌)" };
    s += L"[현재: ";
    s += names[m_mode];
    s += L"]";
    return s;
}
