//***************************************************************************************
// RenderTexture.h
//
// 오프스크린 렌더 타겟(컬러 SRV + 자체 깊이). 단일 샘플(비-MSAA)이라 셰이더에서
// Texture2D.Sample로 바로 읽을 수 있다. Sobel(Scene05)·포스트프로세싱(Scene06~)에서 사용.
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>

class RenderTexture
{
public:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 크기/포맷이 바뀌면 재생성
    bool EnsureSize(ID3D11Device* dev, int w, int h, DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM, bool withDepth = true)
    {
        if (w <= 0 || h <= 0) return false;
        if (m_w == w && m_h == h && m_fmt == fmt && m_colorTex) return true;
        m_w = w; m_h = h; m_fmt = fmt;
        m_colorTex.Reset(); m_rtv.Reset(); m_srv.Reset(); m_depthTex.Reset(); m_dsv.Reset();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, m_colorTex.GetAddressOf()))) return false;
        if (FAILED(dev->CreateRenderTargetView(m_colorTex.Get(), nullptr, m_rtv.GetAddressOf()))) return false;
        if (FAILED(dev->CreateShaderResourceView(m_colorTex.Get(), nullptr, m_srv.GetAddressOf()))) return false;

        if (withDepth)
        {
            D3D11_TEXTURE2D_DESC dd = {};
            dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
            dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
            dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            if (FAILED(dev->CreateTexture2D(&dd, nullptr, m_depthTex.GetAddressOf()))) return false;
            if (FAILED(dev->CreateDepthStencilView(m_depthTex.Get(), nullptr, m_dsv.GetAddressOf()))) return false;
        }
        return true;
    }

    // 이 RT를 출력으로 바인딩하고 지운다
    void Begin(ID3D11DeviceContext* ctx, const float clear[4])
    {
        ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
        ctx->ClearRenderTargetView(m_rtv.Get(), clear);
        if (m_dsv) ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        D3D11_VIEWPORT vp{}; vp.Width = (float)m_w; vp.Height = (float)m_h; vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
    }

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }
    int Width() const { return m_w; }
    int Height() const { return m_h; }

private:
    ComPtr<ID3D11Texture2D> m_colorTex, m_depthTex;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
    int m_w = 0, m_h = 0;
    DXGI_FORMAT m_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
};
