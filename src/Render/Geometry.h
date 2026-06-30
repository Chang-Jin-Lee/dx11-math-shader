//***************************************************************************************
// Geometry.h
//
// 3D 씬(Scene04~)용 메시 생성 유틸. 위치/법선/UV/탄젠트를 가진 정점으로
// 구(UV sphere)와 평면을 만든다. 탄젠트는 Normal Mapping에서 TBN 구성에 쓰인다.
//***************************************************************************************
#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cmath>

// Pos(3) + Normal(3) + UV(2) + Tangent(4, w=bitangent 부호)
struct VertexPNUT
{
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT4 tangent;
};

struct MeshData
{
    std::vector<VertexPNUT> vertices;
    std::vector<unsigned int> indices;
};

namespace geo
{
    // UV 구: slices(경도 분할), stacks(위도 분할)
    inline MeshData MakeSphere(float radius = 1.0f, int slices = 48, int stacks = 32)
    {
        using namespace DirectX;
        MeshData m;
        for (int i = 0; i <= stacks; ++i)
        {
            float phi = XM_PI * static_cast<float>(i) / stacks;     // 0..pi (위도)
            float sp = std::sin(phi), cp = std::cos(phi);
            for (int j = 0; j <= slices; ++j)
            {
                float theta = XM_2PI * static_cast<float>(j) / slices; // 0..2pi (경도)
                float st = std::sin(theta), ct = std::cos(theta);

                XMFLOAT3 n = { sp * ct, cp, sp * st };
                VertexPNUT v;
                v.pos = { radius * n.x, radius * n.y, radius * n.z };
                v.normal = n;
                v.uv = { static_cast<float>(j) / slices, static_cast<float>(i) / stacks };
                // 경도 방향 탄젠트
                v.tangent = { -st, 0.0f, ct, 1.0f };
                m.vertices.push_back(v);
            }
        }
        int ring = slices + 1;
        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                unsigned int a = i * ring + j;
                unsigned int b = (i + 1) * ring + j;
                m.indices.push_back(a);
                m.indices.push_back(a + 1);
                m.indices.push_back(b);
                m.indices.push_back(b);
                m.indices.push_back(a + 1);
                m.indices.push_back(b + 1);
            }
        }
        return m;
    }

    // XZ 평면(y=0), 법선 +Y, 탄젠트 +X. uvTiles로 UV 반복
    inline MeshData MakePlane(float halfSize = 6.0f, float uvTiles = 6.0f)
    {
        using namespace DirectX;
        MeshData m;
        XMFLOAT3 n = { 0, 1, 0 };
        XMFLOAT4 t = { 1, 0, 0, 1 };
        m.vertices = {
            { { -halfSize, 0, -halfSize }, n, { 0, uvTiles }, t },
            { { -halfSize, 0,  halfSize }, n, { 0, 0 },       t },
            { {  halfSize, 0,  halfSize }, n, { uvTiles, 0 }, t },
            { {  halfSize, 0, -halfSize }, n, { uvTiles, uvTiles }, t },
        };
        m.indices = { 0, 1, 2, 0, 2, 3 };
        return m;
    }
}
