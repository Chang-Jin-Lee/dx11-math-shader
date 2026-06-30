//***************************************************************************************
// ModelLoader.h
//
// cgltf(MIT) + stb_image(public domain)로 .glb를 읽는다.
// 머티리얼별 서브메시로 나누고, base color 텍스처(임베드 PNG/JPG)를 RGBA로 디코드한다.
// 노드 계층의 월드 변환을 정점에 베이크한다.
//***************************************************************************************
#pragma once
#include "Geometry.h"
#include <vector>
#include <cstdint>

namespace model
{
    struct Image
    {
        std::vector<uint8_t> rgba;   // 8bit RGBA
        int w = 0, h = 0;
    };

    struct SubMesh
    {
        std::vector<VertexPNUT>   vertices;
        std::vector<unsigned int> indices;
        DirectX::XMFLOAT4 baseColor = { 1, 1, 1, 1 };  // baseColorFactor
        int   imageIndex = -1;       // images[] 인덱스, 없으면 -1
        bool  alphaTest = false;
        float alphaCutoff = 0.5f;
        bool  doubleSided = false;
    };

    struct Model
    {
        std::vector<Image>   images;
        std::vector<SubMesh> subs;
        DirectX::XMFLOAT3 min{}, max{};
    };

    bool LoadGLB(const char* path, Model& out);
}
