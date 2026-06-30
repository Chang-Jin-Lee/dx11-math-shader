#include "DemoApp.h"
#include "d3dUtil.h"
#include "DXTrace.h"
#include "Scene/Scene01_MathFundamentals.h"
#include "Scene/Scene03_TransformProjection.h"
#include "Scene/Scene04_PhongAndNormalMap.h"
#include <DirectXColors.h>
#include <cassert>

using namespace DirectX;

DemoApp::DemoApp(HINSTANCE hInstance, const std::wstring& windowName, int initWidth, int initHeight)
    : D3DApp(hInstance, windowName, initWidth, initHeight)
{
}

DemoApp::~DemoApp()
{
}

bool DemoApp::Init()
{
    if (!D3DApp::Init())
        return false;

    if (!m_batch.Init(m_pd3dDevice.Get(), m_pd3dImmediateContext.Get()))
        return false;
    if (!m_batch3d.Init(m_pd3dDevice.Get(), m_pd3dImmediateContext.Get()))
        return false;

    // 씬 등록 (인덱스 = 숫자키-1). 미구현 슬롯은 비워둔다.
    m_scenes[0] = std::make_unique<Scene01_MathFundamentals>();      // 1
    m_scenes[2] = std::make_unique<Scene03_TransformProjection>();   // 3
    m_scenes[3] = std::make_unique<Scene04_PhongAndNormalMap>();     // 4

    SceneContext ctx = MakeContext();
    for (auto& s : m_scenes)
        if (s) s->Init(ctx);
    m_scenesInitialized = true;

    // 마우스를 절대 좌표 모드로 (오버레이 좌표와 일치)
    m_pMouse->SetWindow(m_hMainWnd);
    m_pMouse->SetMode(DirectX::Mouse::MODE_ABSOLUTE);

    return true;
}

void DemoApp::OnResize()
{
    // D2D 자원은 백버퍼에 묶여 있으므로 리사이즈 전에 해제
    m_textBrush.Reset();
    m_titleBrush.Reset();
    m_pd2dRenderTarget.Reset();

    D3DApp::OnResize();

    // 백버퍼(DXGI 표면) 위에 D2D 렌더 타겟 재생성
    ComPtr<IDXGISurface> surface;
    HR(m_pSwapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = m_pd2dFactory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, m_pd2dRenderTarget.GetAddressOf());
    surface.Reset();

    if (hr == E_NOINTERFACE)
    {
        OutputDebugStringW(L"경고: Direct2D-Direct3D 상호운용이 제한되어 텍스트가 표시되지 않습니다.\n");
    }
    else if (SUCCEEDED(hr))
    {
        InitD2DText();
    }
    else
    {
        assert(m_pd2dRenderTarget);  // 예기치 못한 실패
    }
}

void DemoApp::InitD2DText()
{
    HR(m_pd2dRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.92f, 0.92f, 0.95f, 1.0f), m_textBrush.GetAddressOf()));
    HR(m_pd2dRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.98f, 0.82f, 0.25f, 1.0f), m_titleBrush.GetAddressOf()));

    if (!m_titleFormat)
    {
        HR(m_pdwriteFactory->CreateTextFormat(L"Malgun Gothic", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            22.0f, L"ko-kr", m_titleFormat.GetAddressOf()));
    }
    if (!m_bodyFormat)
    {
        HR(m_pdwriteFactory->CreateTextFormat(L"Malgun Gothic", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            16.0f, L"ko-kr", m_bodyFormat.GetAddressOf()));
    }
}

SceneContext DemoApp::MakeContext()
{
    SceneContext ctx;
    ctx.device = m_pd3dDevice.Get();
    ctx.context = m_pd3dImmediateContext.Get();
    ctx.batch = &m_batch;
    ctx.batch3d = &m_batch3d;
    ctx.screenWidth = m_ClientWidth;
    ctx.screenHeight = m_ClientHeight;
    ctx.mouse = m_pMouse->GetState();
    ctx.mouseTracker = &m_MouseTracker;
    ctx.keyboard = m_pKeyboard->GetState();
    ctx.keyboardTracker = &m_KeyboardTracker;
    return ctx;
}

void DemoApp::UpdateScene(float dt)
{
    // 입력 트래커 갱신
    Mouse::State ms = m_pMouse->GetState();
    m_MouseTracker.Update(ms);
    Keyboard::State ks = m_pKeyboard->GetState();
    m_KeyboardTracker.Update(ks);

    // ESC 종료
    if (m_KeyboardTracker.IsKeyPressed(Keyboard::Escape))
    {
        PostMessage(m_hMainWnd, WM_CLOSE, 0, 0);
        return;
    }

    // 숫자키 1~8로 씬 전환
    const Keyboard::Keys numKeys[8] = {
        Keyboard::D1, Keyboard::D2, Keyboard::D3, Keyboard::D4,
        Keyboard::D5, Keyboard::D6, Keyboard::D7, Keyboard::D8 };
    for (int i = 0; i < 8; ++i)
    {
        if (m_KeyboardTracker.IsKeyPressed(numKeys[i]) && m_scenes[i])
            m_currentScene = i;
    }

    if (!m_scenes[m_currentScene]) return;
    SceneContext ctx = MakeContext();
    m_scenes[m_currentScene]->Update(ctx, dt);
}

void DemoApp::DrawScene()
{
    assert(m_pd3dImmediateContext);
    assert(m_pSwapChain);

    static const float bg[4] = { 0.07f, 0.08f, 0.10f, 1.0f };
    m_pd3dImmediateContext->ClearRenderTargetView(m_pRenderTargetView.Get(), bg);
    m_pd3dImmediateContext->ClearDepthStencilView(m_pDepthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // 씬이 뷰포트를 바꿀 수 있으므로 매 프레임 전체 뷰포트로 초기화
    m_pd3dImmediateContext->RSSetViewports(1, &m_ScreenViewport);

    // 씬 렌더
    if (m_scenes[m_currentScene])
    {
        SceneContext ctx = MakeContext();
        m_batch.Begin(m_ClientWidth, m_ClientHeight);
        m_scenes[m_currentScene]->Render(ctx);
        m_batch.End();
    }

    // Direct2D 텍스트
    if (m_pd2dRenderTarget && m_scenes[m_currentScene])
    {
        IScene* scene = m_scenes[m_currentScene].get();
        m_pd2dRenderTarget->BeginDraw();

        std::wstring title = scene->Name();
        m_pd2dRenderTarget->DrawTextW(title.c_str(), (UINT32)title.size(), m_titleFormat.Get(),
            D2D1_RECT_F{ 14.0f, 10.0f, (float)m_ClientWidth - 10.0f, 48.0f }, m_titleBrush.Get());

        std::wstring body = L"숫자키 1~8: 씬 전환   |   ESC: 종료\n" + scene->HudText();
        m_pd2dRenderTarget->DrawTextW(body.c_str(), (UINT32)body.size(), m_bodyFormat.Get(),
            D2D1_RECT_F{ 14.0f, 46.0f, (float)m_ClientWidth - 10.0f, 280.0f }, m_textBrush.Get());

        HRESULT hr = m_pd2dRenderTarget->EndDraw();
        (void)hr;
    }

    HR(m_pSwapChain->Present(0, 0));
}
