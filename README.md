# DX11 Math & Shader Demo

게임수학·셰이더 프로그래밍의 핵심 개념을 **하나의 Direct3D 11 인터랙티브 데모**로 보여주는 교육용 프로젝트입니다. 숫자키로 씬을 전환하고, 각 씬 안에서 `Q/W/E/R` 등으로 세부 개념을 토글하며 실시간으로 확인할 수 있습니다.

- **환경**: Windows 10/11, Visual Studio 2022 (v143)
- **플랫폼**: Win32 Desktop, x64, Direct3D 11
- **의존성**: **Windows SDK만 사용** (vcpkg/NuGet/외부 라이브러리 불필요) — clone 후 바로 빌드됩니다.

> 작성 중인 프로젝트입니다. 현재 **Scene01(게임수학 기초)** 이 구현되어 있고, Scene02~08은 같은 씬 시스템 위에 순차적으로 추가됩니다. 전체 명세는 [`docs/dx11-math-shader-demo-guide.md`](docs/dx11-math-shader-demo-guide.md) 참고.

---

## 빌드 & 실행

```text
1. DX11MathShader.sln 을 Visual Studio 2022로 연다
2. 구성: Debug 또는 Release / 플랫폼: x64
3. 빌드 후 실행 (F5)
```

명령줄 빌드:

```powershell
msbuild DX11MathShader.sln /p:Configuration=Release /p:Platform=x64
# 결과물: bin\x64\Release\DX11MathShader.exe
```

실행 파일은 자체 완결형이라 별도 DLL/애셋 복사가 필요 없습니다.

---

## 조작법

| 키 | 동작 |
|----|------|
| `1` ~ `8` | 씬 전환 (현재 Scene01만 활성) |
| `Q` `W` `E` `R` | 현재 씬의 서브모드 전환 |
| `ESC` | 종료 |
| 마우스 이동 | 씬에 따라 박스/판정 대상 조작 |

화면 좌상단에 현재 씬 이름과 조작 안내가 Direct2D/DirectWrite 텍스트로 표시됩니다.

---

## Scene01 — 게임수학 기초

2D 오버레이로 1학기 게임수학의 핵심을 보여줍니다.

| 서브모드 | 내용 |
|----------|------|
| `Q` AABB | 축 정렬 경계 상자 충돌. 마우스로 박스 이동, 겹치면 빨강·아니면 초록 |
| `W` OBB | 방향성 경계 상자 충돌을 **SAT(분리축 정리)** 로 판정. 박스는 자동 회전 |
| `E` 운동 | 원형·타원·정현파·나선 운동과 궤도 경로 시각화 |
| `R` 반사/다각형 | 벽 반사 `r = d - 2(d·n)n` + 화살표, 볼록 다각형 내부 판별 |

충돌·반사·내부판별 수식은 [`src/Math/Collision2D.h`](src/Math/Collision2D.h)에 순수 함수로 분리되어 있어 그대로 재사용·검증할 수 있습니다.

---

## 아키텍처

```
src/
├── Main.cpp                         진입점 (WinMain)
├── DemoApp.{h,cpp}                  D3DApp 상속: 씬 매니저 + D2D 텍스트 오버레이
├── IScene.h                         씬 공통 인터페이스 + SceneContext(입력/리소스 전달)
├── PrimitiveBatch2D.{h,cpp}         2D 라인/도형 즉시모드 배처 (런타임 셰이더 컴파일)
├── Scene/
│   └── Scene01_MathFundamentals.{h,cpp}
├── Math/
│   └── Collision2D.h                AABB/OBB(SAT)/반사/다각형 내부판별
└── (프레임워크) d3dApp, d3dUtil, DXTrace, CpuTimer, Keyboard, Mouse, WinMin
```

- **씬 시스템**: `IScene`(`Init/Update/Render/Name/HudText`)을 `DemoApp`이 한 번에 하나씩 구동. Scene02~08 추가는 `m_scenes`에 등록만 하면 됩니다.
- **2D 배처**: 픽셀 좌표(좌상단 0,0)를 셰이더에서 NDC로 변환. 라인 리스트·삼각형 리스트를 모아 한 번에 그리며, 셰이더는 `D3DCompile`로 런타임 컴파일(외부 파일 불필요).
- **텍스트**: Direct2D + DirectWrite를 DXGI 표면과 상호운용해 한글 텍스트를 렌더.
- 4x MSAA로 라인/곡선이 매끄럽게 표시됩니다.

---

## 크레딧

- 베이스 프레임워크(`d3dApp`, `Keyboard`, `Mouse`, `DXTrace` 등)는
  [MKXJun/DirectX11-With-Windows-SDK](https://github.com/MKXJun/DirectX11-With-Windows-SDK) (MIT License)를 기반으로 정리했습니다.
- 데모 설계 및 Scene 구현은 본 저장소에서 작성.

## 라이선스

MIT License (`LICENSE` 참고).
