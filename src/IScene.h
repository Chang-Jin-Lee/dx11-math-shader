//***************************************************************************************
// IScene.h
//
// 데모 씬 공통 인터페이스. DemoApp이 현재 활성 씬 하나를 Update/Render 한다.
// 숫자키 1~8로 씬을 전환하고, 각 씬은 Q/W/E/R/T 등으로 서브모드를 토글한다.
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <string>
#include "Mouse.h"
#include "Keyboard.h"

class PrimitiveBatch2D;
class PrimitiveBatch3D;

// 매 프레임 씬에 전달되는 입력/리소스 묶음
struct SceneContext
{
    ID3D11Device*        device  = nullptr;
    ID3D11DeviceContext* context = nullptr;
    PrimitiveBatch2D*    batch   = nullptr;   // 2D 오버레이 배처
    PrimitiveBatch3D*    batch3d = nullptr;   // 월드공간 3D 라인/도형 배처
    ID3D11RenderTargetView* backRTV = nullptr; // 백버퍼 RTV(오프스크린 후 복귀용)
    ID3D11DepthStencilView* backDSV = nullptr; // 백버퍼 DSV
    int screenWidth  = 0;
    int screenHeight = 0;

    // 입력 — 상태는 값으로(스냅샷), 트래커는 DemoApp이 소유하므로 포인터로 전달
    DirectX::Mouse::State                       mouse;
    DirectX::Mouse::ButtonStateTracker*         mouseTracker    = nullptr;
    DirectX::Keyboard::State                    keyboard;
    DirectX::Keyboard::KeyboardStateTracker*    keyboardTracker = nullptr;
};

class IScene
{
public:
    virtual ~IScene() = default;

    virtual void Init(const SceneContext& ctx) { (void)ctx; }
    virtual void Update(const SceneContext& ctx, float dt) = 0;
    virtual void Render(const SceneContext& ctx) = 0;

    // 화면 좌상단에 표시할 씬 이름
    virtual const wchar_t* Name() const = 0;
    // 현재 서브모드/조작법 안내(D2D 텍스트로 출력)
    virtual std::wstring HudText() const { return L""; }
};
