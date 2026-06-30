//***************************************************************************************
// DemoApp.h
//
// D3DApp를 상속한 데모 본체.
//  - 숫자키 1~8로 씬 전환(현재 Scene01 구현)
//  - PrimitiveBatch2D로 2D 오버레이 렌더
//  - Direct2D/DirectWrite로 화면 좌상단에 씬 이름/조작법 표시
//***************************************************************************************
#pragma once
#include "d3dApp.h"
#include "PrimitiveBatch2D.h"
#include "IScene.h"
#include <memory>
#include <vector>

class DemoApp : public D3DApp
{
public:
    DemoApp(HINSTANCE hInstance, const std::wstring& windowName, int initWidth, int initHeight);
    ~DemoApp() override;

    bool Init() override;
    void OnResize() override;
    void UpdateScene(float dt) override;
    void DrawScene() override;

private:
    void InitD2DText();
    SceneContext MakeContext();

    PrimitiveBatch2D m_batch;
    std::vector<std::unique_ptr<IScene>> m_scenes;
    int  m_currentScene = 0;
    bool m_scenesInitialized = false;

    // Direct2D 텍스트
    ComPtr<ID2D1SolidColorBrush> m_textBrush;
    ComPtr<ID2D1SolidColorBrush> m_titleBrush;
    ComPtr<IDWriteTextFormat>    m_titleFormat;
    ComPtr<IDWriteTextFormat>    m_bodyFormat;
};
