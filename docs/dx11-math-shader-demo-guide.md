# DX11 수학·셰이더 프로그래밍 데모 — AI 구현 지침서

> **대상 독자:** 이 문서를 받은 AI 구현자.  
> **목표:** 오규환 교수 게임수학(1·2학기)·셰이더 프로그래밍(3·4학기) 4학기 커리큘럼의 핵심 개념을 하나의 D3D11 인터랙티브 데모로 구현한다.  
> **기반:** 구현자가 넘겨받은 기존 D3D11 베이스 코드(디바이스, 스왑체인, 렌더 타겟, 기본 드로우 루프 포함). 새 파일을 추가하고 기존 진입점(Update/Render)에 연결하는 방식으로만 수정한다.

---

## 1. 전체 아키텍처

### 1.1 씬 시스템

```
DemoSceneManager
├── Scene01_MathFundamentals     (1학기 — 충돌·벡터·운동)
├── Scene02_CurvesAndSplines     (2학기 — 보간·Bezier·스플라인)
├── Scene03_TransformProjection  (2학기 — 행렬변환·투영·카메라)
├── Scene04_PhongAndNormalMap    (3학기 — Phong 조명·Normal Mapping)
├── Scene05_StylizedShading      (3학기 — Toon·Outline·Sobel·Hatching)
├── Scene06_ProceduralNoise      (4학기 — Noise·FBM·Domain Warping)
├── Scene07_PostProcessing       (4학기 — Blur·HDR·Bloom·SSAO)
└── Scene08_DeferredShading      (4학기 — Deferred·Multi-Light)
```

**씬 전환:** 숫자키 `1`–`8`. 현재 씬 번호와 이름을 화면 좌상단에 텍스트로 표시.

### 1.2 IScene 인터페이스

```cpp
class IScene {
public:
    virtual ~IScene() = default;
    virtual void Init(ID3D11Device* device, ID3D11DeviceContext* ctx) = 0;
    virtual void Update(float dt) = 0;
    virtual void Render(ID3D11DeviceContext* ctx) = 0;
    virtual void Shutdown() = 0;
    virtual void OnKeyDown(UINT vk) {}  // 씬별 서브 모드 전환
    virtual const char* Name() const = 0;
};
```

### 1.3 공통 헬퍼 구조체

```cpp
// 모든 씬에서 공용
struct Vertex_PosNormUV {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

struct Vertex_PosNormUVTan {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT4 tangent;  // w = bitangent handedness
};

struct CB_PerFrame {
    DirectX::XMMATRIX viewProj;
    DirectX::XMFLOAT3 camPos;
    float time;
};

struct CB_PerObject {
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX worldInvTranspose;
};
```

### 1.4 공통 유틸리티 HLSL (Common.hlsli)

```hlsl
// 이 파일을 #include 해서 모든 셰이더에서 공용으로 사용

float2 hash2(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float hash1(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// Smooth Value Noise
float valueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);  // smoothstep
    float a = hash1(i + float2(0,0));
    float b = hash1(i + float2(1,0));
    float c = hash1(i + float2(0,1));
    float d = hash1(i + float2(1,1));
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

// FBM (Fractal Brownian Motion)
float fbm(float2 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < octaves; i++) {
        val += amp * valueNoise(p * freq);
        amp  *= 0.5;
        freq *= 2.0;
    }
    return val;
}

// Linear interpolation
float3 lerp3(float3 a, float3 b, float t) { return a + t * (b - a); }
```

---

## 2. Scene01 — 게임수학 기초 (1학기)

**키보드 서브모드 (씬 내 Q/W/E/R 토글):**
- Q: AABB 충돌
- W: OBB 충돌
- E: 원형·정현파 운동
- R: 벡터 반사 + 다각형 내부 판별

### 2.1 AABB / OBB 충돌 (서브모드 Q, W)

**시각 구성:** 2D 오버레이. 마우스로 박스A를 드래그. 박스B는 고정. 충돌 시 빨간색, 미충돌 시 초록색으로 색상 변경.

**AABB C++ 로직:**
```cpp
struct AABB { DirectX::XMFLOAT2 min, max; };

bool IntersectsAABB(const AABB& a, const AABB& b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y);
}
```

**OBB C++ 로직 (SAT — 분리축 정리):**
```cpp
struct OBB2D {
    DirectX::XMFLOAT2 center;
    DirectX::XMFLOAT2 halfExtents;
    float rotation;   // 라디안
};

// 축 ax에 OBB의 모든 꼭짓점을 투영한 [min, max] 반환
void ProjectOBB(const OBB2D& obb, const DirectX::XMFLOAT2& ax,
                float& outMin, float& outMax);

bool IntersectsOBB(const OBB2D& a, const OBB2D& b) {
    // 4개 분리축 검사: a의 2축 + b의 2축
    // 각 축에서 겹치지 않으면 false 반환
    // 모든 축에서 겹치면 true
}
```

**렌더:** 2D 선분 드로우 (Line List). 박스 모서리 → 4개 선분. 충돌 여부에 따라 ConstantBuffer에 색상 전달.

**셰이더 (최소):**
```hlsl
// VS: NDC 좌표를 바로 쓰거나 ortho matrix 사용
// PS: float4 gColor 를 cbuffer에서 받아 그대로 반환
```

---

### 2.2 원형·타원·정현파 운동 (서브모드 E)

**시각 구성:** 3개 오브젝트가 각각 다른 궤도로 움직임. 궤도 경로를 흰 선으로 그림.

**C++ Update 로직:**
```cpp
// 원형 운동
float cx = centerX + radius * cosf(time * speed);
float cy = centerY + radius * sinf(time * speed);

// 타원 운동
float ex = centerX + rx * cosf(time * speed);
float ey = centerY + ry * sinf(time * speed);

// 정현파 (y방향 왕복)
float sx = centerX + time * linearSpeed;  // x는 선형
float sy = centerY + amplitude * sinf(time * freq);

// 나선형
float spiral_r = time * growRate;
float spx = spiral_r * cosf(time * angSpeed);
float spy = spiral_r * sinf(time * angSpeed);
```

---

### 2.3 벡터 반사 + 다각형 내부 판별 (서브모드 R)

**벡터 반사:**
```cpp
// 입사벡터 d, 표면 법선 n (정규화됨)
// r = d - 2*(d·n)*n
DirectX::XMFLOAT2 Reflect(DirectX::XMFLOAT2 d, DirectX::XMFLOAT2 n);
```
시각: 공이 화면 경계 벽에 반사. 입사·반사 벡터를 화살표로 시각화.

**볼록 다각형 내부 판별 (CCW 가정):**
```cpp
// 점 P가 볼록 다각형 내부인지: 모든 엣지에 대해 P가 왼쪽(CCW)인지 확인
bool PointInConvexPolygon(const std::vector<XMFLOAT2>& poly, XMFLOAT2 p) {
    for (int i = 0; i < poly.size(); ++i) {
        XMFLOAT2 a = poly[i], b = poly[(i+1)%poly.size()];
        float cross = (b.x-a.x)*(p.y-a.y) - (b.y-a.y)*(p.x-a.x);
        if (cross < 0.0f) return false;
    }
    return true;
}
```
시각: 마우스 위치가 다각형 내부면 다각형 채우기 색 변경.

---

## 3. Scene02 — 보간과 곡선 (2학기)

**서브모드 (Q/W/E/R):**
- Q: 선형·정현 보간 비교
- W: Bezier 곡선 (2차·3차·구간별)
- E: Hermite 곡선
- R: Catmull-Rom 스플라인

### 3.1 보간 비교 (서브모드 Q)

```cpp
// t는 0→1 자동 증가 (pingpong)
float Linear   = lerp(0.0f, 1.0f, t);
float Sine     = sinf(t * XM_PI * 0.5f);          // ease-in
float SineInOut= (1.0f - cosf(t * XM_PI)) * 0.5f; // ease-in-out
float Smoothstep = t*t*(3.0f - 2.0f*t);
```
시각: 4개 구체가 왼쪽↔오른쪽으로 각각 다른 보간으로 이동. 아래쪽에 보간 그래프를 선으로 그림.

### 3.2 Bezier 곡선 (서브모드 W)

**2차 Bezier:**
```cpp
// P0, P1(제어), P2(끝)
XMFLOAT2 QuadraticBezier(XMFLOAT2 p0, XMFLOAT2 p1, XMFLOAT2 p2, float t) {
    float u = 1.0f - t;
    return { u*u*p0.x + 2*u*t*p1.x + t*t*p2.x,
             u*u*p0.y + 2*u*t*p1.y + t*t*p2.y };
}
```

**3차 Bezier:**
```cpp
XMFLOAT2 CubicBezier(XMFLOAT2 p0, XMFLOAT2 p1, XMFLOAT2 p2, XMFLOAT2 p3, float t) {
    float u = 1.0f - t;
    return {
        u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
        u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
    };
}
```

**C2 구간별 3차 Bezier:** 여러 세그먼트를 C2 연속성(곡률 연속)으로 연결. 각 접합점에서 앞뒤 제어점이 거울 대칭.

**렌더:** 곡선을 100개 선분으로 분할해 Line Strip으로 그림. 제어점은 작은 원으로 표시. 마우스로 제어점 드래그 가능.

### 3.3 Hermite 곡선 (서브모드 E)

```cpp
// 두 끝점 p0, p1과 두 끝점의 접선벡터 t0, t1
XMFLOAT2 Hermite(XMFLOAT2 p0, XMFLOAT2 t0, XMFLOAT2 p1, XMFLOAT2 t1, float t) {
    float h00 =  2*t*t*t - 3*t*t + 1;
    float h10 =    t*t*t - 2*t*t + t;
    float h01 = -2*t*t*t + 3*t*t;
    float h11 =    t*t*t -   t*t;
    return {
        h00*p0.x + h10*t0.x + h01*p1.x + h11*t1.x,
        h00*p0.y + h10*t0.y + h01*p1.y + h11*t1.y
    };
}
```

### 3.4 Catmull-Rom 스플라인 (서브모드 R)

```cpp
// 4개의 제어점 p0~p3, 구간은 p1→p2
XMFLOAT2 CatmullRom(XMFLOAT2 p0, XMFLOAT2 p1, XMFLOAT2 p2, XMFLOAT2 p3, float t) {
    // t0 = (p2-p0)/2, t1 = (p3-p1)/2 로 Hermite 변환
    XMFLOAT2 tangent0 = { (p2.x-p0.x)*0.5f, (p2.y-p0.y)*0.5f };
    XMFLOAT2 tangent1 = { (p3.x-p1.x)*0.5f, (p3.y-p1.y)*0.5f };
    return Hermite(p1, tangent0, p2, tangent1, t);
}
```
시각: 닫힌 루프 스플라인 위를 구체가 일정 속도로 이동. 속도가 균일하려면 arc-length reparameterization 적용(룩업 테이블로 구현).

---

## 4. Scene03 — 행렬 변환과 투영 (2학기)

**서브모드:**
- Q: TRS 행렬 분해 데모 (개별 변환 슬라이더)
- W: 정사영(Orthographic) vs 원근(Perspective) 비교
- E: 카메라 행렬 직접 구성 (View = LookAt 수식 전개)

### 4.1 TRS 행렬 분해 (서브모드 Q)

**C++ cbuffer 구성:**
```cpp
// Ctrl+방향키로 T/R/S 개별 조작
XMMATRIX T = XMMatrixTranslation(tx, ty, tz);
XMMATRIX R = XMMatrixRotationRollPitchYaw(rx, ry, rz);
XMMATRIX S = XMMatrixScaling(sx, sy, sz);
XMMATRIX world = S * R * T;  // 순서 중요
```
시각: 축 기즈모를 함께 그려서 각 변환의 효과를 실시간으로 확인. 화면에 현재 행렬 값을 텍스트로 표시.

### 4.2 정사영 vs 원근 투영 (서브모드 W)

화면을 좌우로 분할:
- 왼쪽: `XMMatrixOrthographicLH(width, height, near, far)`
- 오른쪽: `XMMatrixPerspectiveFovLH(fovY, aspect, near, far)`

같은 격자 구조를 양쪽에서 렌더. F키로 FOV 증감.

### 4.3 LookAt 행렬 수식 전개 (서브모드 E)

```cpp
// LookAt을 XMMatrixLookAtLH 대신 직접 구성
XMFLOAT3 zAxis = Normalize(target - eye);          // forward
XMFLOAT3 xAxis = Normalize(Cross(up, zAxis));       // right
XMFLOAT3 yAxis = Cross(zAxis, xAxis);               // up (재정규화)

XMMATRIX view = {
    xAxis.x, yAxis.x, zAxis.x, 0,
    xAxis.y, yAxis.y, zAxis.y, 0,
    xAxis.z, yAxis.z, zAxis.z, 0,
    -Dot(xAxis,eye), -Dot(yAxis,eye), -Dot(zAxis,eye), 1
};
```
시각: 카메라가 구체 주위를 마우스 드래그로 공전. 각 축(X=빨강,Y=초록,Z=파랑) 표시.

---

## 5. Scene04 — Phong 조명 + Normal Mapping (3학기)

**오브젝트:** 구체(tessellation 없이 sphere 메시) + 바닥 평면.

**서브모드 (Q/W/E/R/T):**
- Q: Diffuse만
- W: Diffuse + Specular
- E: Diffuse + Specular + Ambient
- R: 전체 Phong (+ Emissive)
- T: Normal Mapping 추가

### 5.1 Phong 셰이더

**Vertex Shader:**
```hlsl
cbuffer CB_PerFrame : register(b0) {
    matrix viewProj;
    float3 camPos;
    float  time;
}
cbuffer CB_PerObject : register(b1) {
    matrix world;
    matrix worldInvTranspose;
}

struct VS_IN  { float3 pos : POSITION; float3 nor : NORMAL; float2 uv : TEXCOORD; };
struct VS_OUT { float4 svpos : SV_Position; float3 wpos : TEXCOORD0;
                float3 wnor  : TEXCOORD1;   float2 uv   : TEXCOORD2; };

VS_OUT VS_Phong(VS_IN v) {
    VS_OUT o;
    float4 wp   = mul(float4(v.pos,1), world);
    o.svpos     = mul(wp, viewProj);
    o.wpos      = wp.xyz;
    o.wnor      = mul(v.nor, (float3x3)worldInvTranspose);
    o.uv        = v.uv;
    return o;
}
```

**Pixel Shader:**
```hlsl
cbuffer CB_Light : register(b2) {
    float3 lightPos;
    float  pad0;
    float3 lightColor;
    float  pad1;
    float4 matAmbient;
    float4 matDiffuse;
    float4 matSpecular;  // w = shininess
    float4 matEmissive;
}

float4 PS_Phong(VS_OUT i) : SV_Target {
    float3 N = normalize(i.wnor);
    float3 L = normalize(lightPos - i.wpos);
    float3 V = normalize(camPos   - i.wpos);
    float3 R = reflect(-L, N);

    // Diffuse (Lambert)
    float  diff   = max(dot(N, L), 0.0);
    float3 diffuse = matDiffuse.rgb * lightColor * diff;

    // Specular (Phong)
    float  spec    = pow(max(dot(R, V), 0.0), matSpecular.w);
    float3 specular = matSpecular.rgb * lightColor * spec;

    // Ambient
    float3 ambient = matAmbient.rgb * lightColor * 0.15;

    // Emissive
    float3 emissive = matEmissive.rgb;

    float4 color = float4(ambient + diffuse + specular + emissive, 1.0);
    return color;
}
```

### 5.2 Normal Mapping (서브모드 T)

**Vertex Shader 추가 (탄젠트 공간 계산):**
```hlsl
struct VS_IN_TBN {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD;
    float4 tangent : TANGENT;  // w = bitangent sign
};

VS_OUT_TBN VS_NormalMap(VS_IN_TBN v) {
    VS_OUT_TBN o;
    float4 wp = mul(float4(v.pos,1), world);
    o.svpos   = mul(wp, viewProj);
    o.wpos    = wp.xyz;
    o.wtan    = mul(v.tangent.xyz, (float3x3)world);
    o.wnor    = mul(v.normal,      (float3x3)worldInvTranspose);
    o.wbitan  = cross(o.wnor, o.wtan) * v.tangent.w;
    o.uv      = v.uv;
    return o;
}
```

**Pixel Shader:**
```hlsl
Texture2D    gDiffuseTex : register(t0);
Texture2D    gNormalTex  : register(t1);
SamplerState gSampler    : register(s0);

float4 PS_NormalMap(VS_OUT_TBN i) : SV_Target {
    // 노말맵에서 탄젠트 공간 법선 읽기
    float3 tn = gNormalTex.Sample(gSampler, i.uv).xyz * 2.0 - 1.0;

    // 탄젠트 → 월드 공간 변환
    float3x3 TBN = float3x3(normalize(i.wtan), normalize(i.wbitan), normalize(i.wnor));
    float3 N = normalize(mul(tn, TBN));

    // 이후 Phong 계산과 동일
    ...
}
```

**리소스:** 벽돌 디퓨즈/노말맵 텍스처를 `assets/` 폴더에 제공. 없으면 절차적으로 노말맵 생성.

---

## 6. Scene05 — 스타일라이즈드 셰이딩 (3학기)

**서브모드:**
- Q: Toon/Cell Shading (Ramp Texture 방식)
- W: Outline Pass (Back-face Extrusion)
- E: Toon + Outline 합산
- R: Sobel 엣지 감지 (2-pass)
- T: Hatching Shader

### 6.1 Toon Shading (서브모드 Q)

```hlsl
Texture2D    gRampTex : register(t0);  // 1D 그라디언트 텍스처 (밝기→색상 룩업)
SamplerState gSampler : register(s0);

float4 PS_Toon(VS_OUT i) : SV_Target {
    float3 N = normalize(i.wnor);
    float3 L = normalize(lightPos - i.wpos);
    float  NdotL = dot(N, L) * 0.5 + 0.5;  // [-1,1] → [0,1]

    // Ramp 텍스처 룩업
    float3 rampColor = gRampTex.Sample(gSampler, float2(NdotL, 0.5)).rgb;
    return float4(rampColor * matDiffuse.rgb, 1.0);
}
```

**Ramp 텍스처 생성:** 코드로 1D 텍스처 생성 (1×256, 어두운→밝은 단계를 3~4 레벨로 양자화).

### 6.2 Outline Pass (서브모드 W)

**2-패스 방식:**  
Pass1: 뒷면만 그리되, 법선 방향으로 버텍스를 약간 밀어냄.

```hlsl
// VS_Outline
float3 wpos = mul(float4(v.pos, 1), world).xyz;
float3 wnor = normalize(mul(v.normal, (float3x3)world));
wpos += wnor * gOutlineThickness;  // cbuffer에서 두께 조절
o.svpos = mul(float4(wpos, 1), viewProj);

// PS_Outline
return float4(0, 0, 0, 1);  // 검정 고정
```

Pass2: 앞면 정상 렌더(Toon).

**래스터라이저 상태:**
- Pass1: CullMode = FRONT (앞면 제거, 뒷면만 그림)
- Pass2: CullMode = BACK  (기본)

### 6.3 Sobel 엣지 감지 (서브모드 R)

**Pass1:** 씬을 오프스크린 렌더 타겟에 렌더 (깊이 버퍼 포함).

**Pass2:** 전체화면 쿼드에 Sobel 적용.

```hlsl
// Sobel Kernel (3×3)
// Gx:  -1  0  +1     Gy:  +1  +2  +1
//      -2  0  +2          0   0   0
//      -1  0  +1         -1  -2  -1

Texture2D gSceneTex : register(t0);

float4 PS_Sobel(FullscreenVS_OUT i) : SV_Target {
    float2 texelSize = 1.0 / gScreenSize;
    float gx = 0, gy = 0;

    // 3×3 이웃 샘플
    float tl = gSceneTex.Sample(s, i.uv + texelSize * float2(-1,-1)).r;
    float  l = gSceneTex.Sample(s, i.uv + texelSize * float2(-1, 0)).r;
    float bl = gSceneTex.Sample(s, i.uv + texelSize * float2(-1,+1)).r;
    float  t = gSceneTex.Sample(s, i.uv + texelSize * float2( 0,-1)).r;
    float  b = gSceneTex.Sample(s, i.uv + texelSize * float2( 0,+1)).r;
    float tr = gSceneTex.Sample(s, i.uv + texelSize * float2(+1,-1)).r;
    float  r = gSceneTex.Sample(s, i.uv + texelSize * float2(+1, 0)).r;
    float br = gSceneTex.Sample(s, i.uv + texelSize * float2(+1,+1)).r;

    gx = -tl - 2*l - bl + tr + 2*r + br;
    gy =  tl + 2*t + tr - bl - 2*b - br;

    float edge = saturate(sqrt(gx*gx + gy*gy));
    return float4(1-edge, 1-edge, 1-edge, 1);  // 엣지는 검정
}
```

### 6.4 Hatching Shader (서브모드 T)

```hlsl
// 밝기 레벨에 따라 다른 해칭 텍스처(또는 절차적 패턴) 블렌딩
Texture2D gHatch[6] : register(t0);  // 6단계 해칭 텍스처 (또는 절차적 생성)

float4 PS_Hatching(VS_OUT i) : SV_Target {
    float NdotL = saturate(dot(normalize(i.wnor), normalize(lightPos - i.wpos)));
    float lum   = NdotL;  // 0=어두움, 1=밝음

    // 밝기를 6단계로 분할, 각 단계에서 해칭 패턴 샘플
    // 해칭 텍스처 없으면 절차적 사선 패턴 사용:
    float2 hatchUV = i.uv * 10.0;
    float hatch45  = step(0.5, frac(hatchUV.x + hatchUV.y));   // 45° 사선
    float hatch135 = step(0.5, frac(hatchUV.x - hatchUV.y));   // 135° 사선

    float hatchWeight = 1.0 - lum;
    float hatch = lerp(1.0, hatch45, saturate(hatchWeight * 2));
    hatch = lerp(hatch, hatch * hatch135, saturate((hatchWeight - 0.5) * 2));

    return float4(hatch, hatch, hatch, 1.0);
}
```

---

## 7. Scene06 — 절차적 노이즈 (4학기)

**서브모드:**
- Q: Pseudo-Random (sin 기반)
- W: Value Noise → Perlin Noise 비교
- E: FBM (Fractal Brownian Motion)
- R: Domain Warping + Water Ripple
- T: Truchet 패턴

모든 효과를 **전체화면 쿼드**에 픽셀 셰이더로 렌더. UV 좌표 = 화면 좌표로 사용.

### 7.1 Pseudo-Random (서브모드 Q)

```hlsl
// sin을 이용한 고주파 의사난수 (품질 낮지만 교육 목적으로 충분)
float pseudoRandom(float2 uv) {
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

// UV Scale 방법 (UV 스케일링으로 셀 크기 조절)
float uvScaleRandom(float2 uv, float scale) {
    float2 grid = floor(uv * scale);
    return pseudoRandom(grid);
}

float4 PS_PseudoRandom(FullscreenVS_OUT i) : SV_Target {
    float v = uvScaleRandom(i.uv, 10.0);
    return float4(v, v, v, 1);
}
```

### 7.2 Value Noise vs Perlin Noise (서브모드 W)

화면을 좌우 분할:  
- 왼쪽: `valueNoise(uv * freq)` (Common.hlsli의 함수 사용)  
- 오른쪽: Perlin Noise (그라디언트 기반)

```hlsl
// Perlin Noise (2D 그라디언트 노이즈)
float perlinNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f*f*(3.0 - 2.0*f);

    // 코너별 그라디언트 벡터
    float2 g00 = normalize(hash2(i + float2(0,0)) * 2 - 1);
    float2 g10 = normalize(hash2(i + float2(1,0)) * 2 - 1);
    float2 g01 = normalize(hash2(i + float2(0,1)) * 2 - 1);
    float2 g11 = normalize(hash2(i + float2(1,1)) * 2 - 1);

    float d00 = dot(g00, f - float2(0,0));
    float d10 = dot(g10, f - float2(1,0));
    float d01 = dot(g01, f - float2(0,1));
    float d11 = dot(g11, f - float2(1,1));

    return lerp(lerp(d00,d10,u.x), lerp(d01,d11,u.x), u.y) * 0.5 + 0.5;
}
```

### 7.3 FBM (서브모드 E)

```hlsl
// Common.hlsli의 fbm() 함수 사용
// 옥타브 수를 키보드(+/-)로 1~8 사이 실시간 조절

float4 PS_FBM(FullscreenVS_OUT i) : SV_Target {
    float2 uv = i.uv * 4.0;
    float v = fbm(uv + gTime * 0.1, gOctaves);  // gTime으로 애니메이션
    // 컬러 매핑 (저지 → 고지 토포그래픽 색상)
    float3 col = lerp(float3(0.1,0.3,0.6), float3(0.9,0.8,0.5), v);
    col = lerp(col, float3(1,1,1), smoothstep(0.85, 1.0, v));
    return float4(col, 1);
}
```

### 7.4 Domain Warping + Water Ripple (서브모드 R)

```hlsl
// Domain Warping: 입력 좌표 자체를 노이즈로 변위
float4 PS_DomainWarp(FullscreenVS_OUT i) : SV_Target {
    float2 uv = i.uv * 3.0;

    // 1차 왜곡
    float2 q = float2(fbm(uv, 4),
                      fbm(uv + float2(5.2, 1.3), 4));
    // 2차 왜곡 (double domain warping)
    float2 r = float2(fbm(uv + 4.0*q + float2(1.7, 9.2) + gTime*0.1, 4),
                      fbm(uv + 4.0*q + float2(8.3, 2.8) + gTime*0.1, 4));

    float v = fbm(uv + 4.0*r, 4);
    float3 col = lerp(float3(0.1,0.1,0.4), float3(0.9,0.6,0.1), clamp(v*v*4,0,1));
    return float4(col, 1);
}

// Water Ripple
float4 PS_WaterRipple(FullscreenVS_OUT i) : SV_Target {
    float2 uv  = i.uv;
    float2 center = float2(0.5, 0.5);
    float  dist = length(uv - center);
    float  ripple = sin(dist * 30.0 - gTime * 5.0) * 0.5 + 0.5;
    ripple *= exp(-dist * 5.0);  // 거리에 따라 감쇠
    return float4(ripple, ripple*0.8, ripple*0.6, 1);
}
```

### 7.5 Truchet 패턴 (서브모드 T)

```hlsl
float4 PS_Truchet(FullscreenVS_OUT i) : SV_Target {
    float2 uv   = i.uv * 8.0;
    float2 cell = floor(uv);
    float2 local = frac(uv);

    float r = step(0.5, hash1(cell));  // 각 셀 무작위 0 또는 1
    // 0: 좌상→우하 대각선 호, 1: 우상→좌하 대각선 호
    float2 p = r < 0.5 ? local : float2(1.0 - local.x, local.y);
    float arc1 = abs(length(p) - 0.5);
    float arc2 = abs(length(p - float2(1,1)) - 0.5);

    float line = min(arc1, arc2);
    float v = 1.0 - smoothstep(0.02, 0.05, line);
    return float4(v, v, v, 1);
}
```

---

## 8. Scene07 — 포스트 프로세싱 (4학기)

**렌더링 순서:**  
1. 씬(구체+바닥 with Phong)을 HDR 렌더 타겟 (`DXGI_FORMAT_R16G16B16A16_FLOAT`)에 렌더  
2. 포스트 패스들을 순서대로 적용  
3. 최종을 백 버퍼에 출력

**서브모드:**
- Q: Gaussian Blur (2-pass separable)
- W: Box / Median / Bilateral 필터
- E: Gamma Correction
- R: HDR + Tone Mapping (Reinhard / ACES)
- T: Bloom

### 8.1 Separable Gaussian Blur (서브모드 Q)

**패스1 (수평):**
```hlsl
// 반경 5, 가중치 미리 계산 (정규분포)
static const float gWeights[9] = {
    0.0625, 0.125, 0.25, 0.5, 1.0, 0.5, 0.25, 0.125, 0.0625
};
// 합계로 정규화해서 사용

float4 PS_BlurH(FullscreenVS_OUT i) : SV_Target {
    float2 ts = float2(1.0 / gScreenWidth, 0);
    float4 col = 0;
    float  wSum = 0;
    [unroll] for (int k = -4; k <= 4; k++) {
        float w = gWeights[k+4];
        col  += gSceneTex.Sample(s, i.uv + ts * k) * w;
        wSum += w;
    }
    return col / wSum;
}
```

**패스2 (수직):** 동일, `ts = float2(0, 1.0/gScreenHeight)`.

**키보드 조절:** `[`/`]` 로 블러 반경(1~9) 실시간 변경.

### 8.2 Bilateral Filter (서브모드 W)

```hlsl
// 공간 가중치(거리) × 색상 가중치(색상 차이)로 엣지 보존 블러
float4 PS_Bilateral(FullscreenVS_OUT i) : SV_Target {
    float4 center = gSceneTex.Sample(s, i.uv);
    float4 sum = 0;
    float  wSum = 0;
    const float sigmaS = 3.0;  // 공간 시그마
    const float sigmaR = 0.1;  // 색상 시그마

    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            float2 offset = float2(dx, dy) / gScreenSize;
            float4 neighbor = gSceneTex.Sample(s, i.uv + offset);

            float wS = exp(-(dx*dx + dy*dy) / (2*sigmaS*sigmaS));
            float colorDiff = length(neighbor.rgb - center.rgb);
            float wR = exp(-(colorDiff*colorDiff) / (2*sigmaR*sigmaR));
            float w = wS * wR;

            sum  += neighbor * w;
            wSum += w;
        }
    }
    return sum / wSum;
}
```

### 8.3 Gamma Correction (서브모드 E)

```hlsl
// Linear → sRGB
float4 PS_GammaCorrect(FullscreenVS_OUT i) : SV_Target {
    float4 hdr = gSceneTex.Sample(s, i.uv);
    // sRGB 인코딩 (정확한 분기 버전)
    float3 srgb = pow(max(hdr.rgb, 0.0), 1.0/2.2);
    return float4(srgb, 1.0);
}
```

### 8.4 HDR + Tone Mapping (서브모드 R)

```hlsl
// Reinhard Tone Mapping
float3 TonemapReinhard(float3 hdr) {
    return hdr / (hdr + 1.0);
}

// ACES Filmic (Krzysztof Narkowicz 근사)
float3 TonemapACES(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float4 PS_ToneMap(FullscreenVS_OUT i) : SV_Target {
    float3 hdr = gHDRTex.Sample(s, i.uv).rgb * gExposure;
    float3 ldr = (gTonemapMode == 0) ? TonemapReinhard(hdr) : TonemapACES(hdr);
    float3 srgb = pow(max(ldr, 0.0), 1.0/2.2);
    return float4(srgb, 1.0);
}
```

**cbuffer:** `gExposure` (키보드 Q/E로 0.1~10), `gTonemapMode` (0=Reinhard, 1=ACES).  
씬에서 광원 강도를 10.0 이상으로 설정해서 HDR 효과가 보이게 함.

### 8.5 Bloom (서브모드 T)

```
패스1: HDR 씬 렌더
패스2: Bright-pass (밝기 임계값 이상만 추출)
패스3: Gaussian Blur H (추출된 밝은 영역)
패스4: Gaussian Blur V
패스5: 원본 HDR + 블러 합산 → Tone mapping
```

```hlsl
// Bright-pass
float4 PS_BrightPass(FullscreenVS_OUT i) : SV_Target {
    float4 c = gHDRTex.Sample(s, i.uv);
    float lum = dot(c.rgb, float3(0.2126, 0.7152, 0.0722));
    return c * step(gThreshold, lum);  // 임계값 이상만 통과
}

// 최종 합산
float4 PS_BloomComposite(FullscreenVS_OUT i) : SV_Target {
    float3 hdr    = gHDRTex.Sample(s, i.uv).rgb;
    float3 bloom  = gBloomTex.Sample(s, i.uv).rgb;
    float3 result = TonemapACES((hdr + bloom * gBloomStrength) * gExposure);
    return float4(pow(result, 1.0/2.2), 1.0);
}
```

---

## 9. Scene08 — Deferred Shading (4학기)

### 9.1 G-Buffer 구성

| 렌더 타겟 | 포맷 | 저장 내용 |
|-----------|------|----------|
| RT0 | `R8G8B8A8_UNORM` | Albedo (rgb) + roughness (a) |
| RT1 | `R16G16B16A16_FLOAT` | World Normal (xyz) + metallic (a) |
| RT2 | `R16G16B16A16_FLOAT` | World Position (xyz) + depth (a) |

**D3D11 MRT 설정:**
```cpp
ID3D11RenderTargetView* rtvs[3] = { gRT[0], gRT[1], gRT[2] };
ctx->OMSetRenderTargets(3, rtvs, dsvGBuffer);
```

### 9.2 Geometry Pass

```hlsl
struct PS_GBUFFER_OUT {
    float4 albedo  : SV_Target0;
    float4 normal  : SV_Target1;
    float4 wpos    : SV_Target2;
};

PS_GBUFFER_OUT PS_GBuffer(VS_OUT i) {
    PS_GBUFFER_OUT o;
    o.albedo = float4(gDiffuseTex.Sample(s, i.uv).rgb, gRoughness);
    o.normal = float4(normalize(i.wnor) * 0.5 + 0.5, gMetallic);  // [0,1] 인코딩
    o.wpos   = float4(i.wpos, 1.0);
    return o;
}
```

### 9.3 Lighting Pass (Fullscreen Quad)

```hlsl
#define MAX_LIGHTS 8

cbuffer CB_Lights : register(b2) {
    float4 lightPos[MAX_LIGHTS];    // w = radius
    float4 lightColor[MAX_LIGHTS];  // w = intensity
    int    numLights;
}

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gWPosTex   : register(t2);

float4 PS_DeferredLight(FullscreenVS_OUT i) : SV_Target {
    float4 albedoData = gAlbedoTex.Sample(s, i.uv);
    float4 normalData = gNormalTex.Sample(s, i.uv);
    float4 wposData   = gWPosTex.Sample(s, i.uv);

    float3 albedo = albedoData.rgb;
    float3 N = normalize(normalData.xyz * 2.0 - 1.0);  // 디코딩
    float3 wpos  = wposData.xyz;
    float3 V = normalize(gCamPos - wpos);

    float3 totalLight = 0;
    for (int k = 0; k < numLights; k++) {
        float3 L    = lightPos[k].xyz - wpos;
        float  dist = length(L);
        L /= dist;

        float  atten = saturate(1.0 - dist / lightPos[k].w);
        atten *= atten;  // 이차 감쇠

        float  diff = saturate(dot(N, L));
        float3 R    = reflect(-L, N);
        float  spec = pow(saturate(dot(R, V)), 32.0);

        totalLight += (albedo * diff + spec) * lightColor[k].rgb * lightColor[k].w * atten;
    }

    return float4(totalLight, 1.0);
}
```

**시각 구성:** 씬에 8개 점광원, 각각 다른 색상. 광원 위치를 작은 구체로 표시. `F` 키로 Forward vs Deferred 실시간 전환해서 성능 비교. 화면 왼쪽 하단에 G-Buffer 시각화 (썸네일 4개 타일).

---

## 10. SSAO (Scene07 또는 Scene08 서브모드 추가)

### 10.1 알고리즘 개요 (3-Pass)

```
Pass0: Geometry Pass → 월드 노말 + 깊이 렌더
Pass1: SSAO Pass     → 각 픽셀 주변 반구 샘플링, 차폐율 계산
Pass2: Blur Pass     → SSAO 결과를 블러해서 노이즈 제거
Final: 조명 × (1 - AO)
```

### 10.2 SSAO Pixel Shader

```hlsl
#define SSAO_SAMPLE_COUNT 16

cbuffer CB_SSAO : register(b3) {
    float4 gKernel[64];     // 반구 내 랜덤 샘플 (CPU에서 미리 생성)
    float4x4 gProjMatrix;
    float  gRadius;         // 샘플링 반경 (월드 단위, 예: 0.5)
    float  gBias;           // 자기차폐 방지 오프셋 (예: 0.025)
}

Texture2D gNormalTex : register(t0);
Texture2D gDepthTex  : register(t1);
Texture2D gNoiseTex  : register(t2);  // 4×4 랜덤 회전 노이즈

float4 PS_SSAO(FullscreenVS_OUT i) : SV_Target {
    float3 N   = normalize(gNormalTex.Sample(s, i.uv).xyz * 2.0 - 1.0);
    float3 pos = ReconstructWorldPos(gDepthTex, i.uv, gProjMatrix);  // 깊이 → 월드 위치 재구성

    // 노이즈로 랜덤 탄젠트 생성 (4×4 타일링 노이즈)
    float2 noiseScale = gScreenSize / 4.0;
    float3 randomVec = normalize(gNoiseTex.Sample(s, i.uv * noiseScale).xyz * 2.0 - 1.0);

    // Gram-Schmidt로 TBN 구성
    float3 tangent   = normalize(randomVec - N * dot(randomVec, N));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0;
    for (int k = 0; k < SSAO_SAMPLE_COUNT; k++) {
        float3 sampleDir = mul(gKernel[k].xyz, TBN);
        float3 samplePos = pos + sampleDir * gRadius;

        // 샘플 위치를 화면 공간으로 투영
        float4 projected = mul(float4(samplePos, 1.0), gProjMatrix);
        projected.xyz /= projected.w;
        float2 sampleUV = projected.xy * 0.5 + 0.5;
        sampleUV.y = 1.0 - sampleUV.y;  // DX UV 뒤집기

        float sampleDepth = /* gDepthTex에서 sampleUV의 깊이 읽기 */ ...;

        float rangeCheck = smoothstep(0.0, 1.0, gRadius / abs(pos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + gBias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / SSAO_SAMPLE_COUNT);
    return float4(occlusion, occlusion, occlusion, 1.0);
}
```

**CPU에서 커널 생성 (Init 시점):**
```cpp
std::uniform_real_distribution<float> dist(0.0f, 1.0f);
for (int i = 0; i < 64; i++) {
    XMFLOAT4 sample(
        dist(gen) * 2.0f - 1.0f,
        dist(gen) * 2.0f - 1.0f,
        dist(gen),  // z는 0~1 (반구)
        0.0f
    );
    // 정규화 + 가속도 감쇠 (가까운 샘플에 더 큰 가중치)
    float scale = (float)i / 64.0f;
    scale = 0.1f + scale * scale * 0.9f;
    XMStoreFloat4(&sample, XMVector3Normalize(XMLoadFloat4(&sample)));
    sample.x *= scale; sample.y *= scale; sample.z *= scale;
    gKernel[i] = sample;
}
```

---

## 11. 전체화면 쿼드 렌더 유틸리티

포스트 프로세싱 패스 모두에서 사용:

```cpp
// 버텍스 버퍼 없이 버텍스 셰이더에서 직접 생성
// DrawCall: ctx->Draw(3, 0)  — 인덱스 없이 삼각형 1개로 전체화면 커버

// VS_Fullscreen.hlsl
float4 VS_Fullscreen(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id & 2) >> 1, (id & 1));     // (0,0) (2,0) (0,2)
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);          // NDC
}

// 대응 UV를 픽셀 셰이더로 넘길 때
struct FullscreenVS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

FullscreenVS_OUT VS_Fullscreen_UV(uint id : SV_VertexID) {
    FullscreenVS_OUT o;
    float2 uv = float2((id & 2) >> 1, (id & 1));
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(uv.x, 1.0 - uv.y);  // DX UV 시작점 보정
    return o;
}
```

---

## 12. 파일 구조 (권장)

```
src/
├── Demo/
│   ├── DemoSceneManager.h/.cpp
│   ├── Scene01_MathFundamentals.h/.cpp
│   ├── Scene02_CurvesAndSplines.h/.cpp
│   ├── Scene03_TransformProjection.h/.cpp
│   ├── Scene04_PhongAndNormalMap.h/.cpp
│   ├── Scene05_StylizedShading.h/.cpp
│   ├── Scene06_ProceduralNoise.h/.cpp
│   ├── Scene07_PostProcessing.h/.cpp
│   └── Scene08_DeferredShading.h/.cpp
└── Math/
    ├── Collision2D.h/.cpp        (AABB, OBB, SAT)
    ├── Curves.h/.cpp             (Bezier, Hermite, CatmullRom)
    └── NoiseHelper.h             (CPU 측 커널 생성)

shaders/
├── Common.hlsli                  (공통 노이즈 함수)
├── VS_Fullscreen.hlsl
├── PhongVS.hlsl / PhongPS.hlsl
├── NormalMapPS.hlsl
├── ToonPS.hlsl
├── OutlineVS.hlsl / OutlinePS.hlsl
├── SobelPS.hlsl
├── HatchingPS.hlsl
├── NoisePS.hlsl                  (서브모드 gMode cbuffer로 통합 가능)
├── BlurPS.hlsl                   (H/V pass, cbuffer로 방향 전환)
├── ToneMappingPS.hlsl
├── BloomBrightPassPS.hlsl
├── BloomCompositePS.hlsl
├── GBufferVS.hlsl / GBufferPS.hlsl
├── DeferredLightPS.hlsl
└── SSAOPS.hlsl

assets/
├── textures/
│   ├── brick_diffuse.dds
│   ├── brick_normal.dds
│   └── toon_ramp.dds            (1D Ramp 텍스처, 코드로 생성 가능)
```

---

## 13. 씬 관리자 예시 (진입점 연결)

```cpp
// DemoSceneManager.h
class DemoSceneManager {
    std::array<std::unique_ptr<IScene>, 8> mScenes;
    int mCurrentScene = 0;

public:
    void Init(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void OnKeyDown(UINT vk);
    void Update(float dt);
    void Render(ID3D11DeviceContext* ctx);
};

// DemoSceneManager.cpp - OnKeyDown
void DemoSceneManager::OnKeyDown(UINT vk) {
    if (vk >= '1' && vk <= '8') {
        mCurrentScene = vk - '1';
    } else {
        mScenes[mCurrentScene]->OnKeyDown(vk);
    }
}
```

기존 베이스 코드의 `Update(dt)`, `Render()`, `OnKeyDown()` 훅에 `DemoSceneManager`의 동명 메서드를 연결하면 됩니다.

---

## 14. 구현 우선순위 (권장 순서)

| 우선순위 | 씬 | 이유 |
|---------|-----|------|
| 1 | Scene01 (충돌·운동) | 가장 의존성이 없음, 씬 시스템 테스트용 |
| 2 | Scene04 (Phong) | 이후 씬들의 베이스 셰이더 |
| 3 | Scene03 (행렬·투영) | 카메라 시스템 완성 |
| 4 | Scene02 (곡선) | 독립 2D 오버레이 |
| 5 | Scene05 (Toon·Sobel) | Scene04 셰이더 기반 확장 |
| 6 | Scene06 (노이즈) | 전체화면 쿼드 시스템 구축 |
| 7 | Scene07 (포스트) | HDR RT 시스템 필요 |
| 8 | Scene08 (Deferred) | MRT + 가장 복잡 |

---

*이 지침서는 4학기 오규환 교수 게임수학·셰이더 프로그래밍 커리큘럼의 핵심 개념을 D3D11 데모 하나에 집약한 구현 명세입니다. 모든 셰이더 코드는 DX11 HLSL 5.0 기준이며, CPU 사이드 코드는 DirectX Math (XMMATRIX 등)를 사용합니다.*
