//***************************************************************************************
// ModelLoader.h
//
// cgltf(단일 헤더, MIT)로 .glb를 읽어 하나의 메시(Pos+Normal+UV)로 합친다.
// 노드 계층의 월드 변환을 정점에 베이크한다. (재질/텍스처는 사용하지 않음 — 형상만)
//***************************************************************************************
#pragma once
#include "Geometry.h"
#include <string>

namespace model
{
    // 성공 시 out에 합쳐진 메시를 채운다. outAABBmin/max로 모델 경계도 반환.
    bool LoadGLB(const char* path, MeshData& out,
                 DirectX::XMFLOAT3& outMin, DirectX::XMFLOAT3& outMax);
}
