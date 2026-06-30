//***************************************************************************************
// FullscreenPass.h
//
// 정점 버퍼 없이 SV_VertexID로 전체화면 삼각형을 만드는 VS를 보관한다.
// 포스트 프로세싱(Sobel, Blur, ToneMap 등)에서 공용.
// 사용법: 외부에서 PS/SRV/Sampler/CB를 바인딩한 뒤 Draw() 호출.
//   VS 출력: float4 SV_Position, float2 TEXCOORD0 (uv, 좌상단 0,0)
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <d3dcompiler.h>

class FullscreenPass
{
public:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool Init(ID3D11Device* dev)
    {
        const char* src = R"(
struct VOut { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
VOut VSMain(uint id : SV_VertexID)
{
    VOut o;
    float2 t = float2((id << 1) & 2, id & 2);   // (0,0)(2,0)(0,2)
    o.uv = t;
    o.pos = float4(t * float2(2,-2) + float2(-1,1), 0, 1);
    return o;
}
)";
        UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3DCompile(src, strlen(src), "Fullscreen", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, blob.GetAddressOf(), err.GetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        return SUCCEEDED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_vs.GetAddressOf()));
    }

    // PS/리소스는 호출 전에 외부에서 바인딩한다. 입력 레이아웃/VB 불필요.
    void Draw(ID3D11DeviceContext* ctx)
    {
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* nullVB = nullptr; UINT z = 0;
        ctx->IASetVertexBuffers(0, 1, &nullVB, &z, &z);
        ctx->VSSetShader(m_vs.Get(), nullptr, 0);
        ctx->Draw(3, 0);
    }

private:
    ComPtr<ID3D11VertexShader> m_vs;
};
