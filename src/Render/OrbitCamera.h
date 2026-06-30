//***************************************************************************************
// OrbitCamera.h
//
// 대상(target)을 중심으로 공전하는 간단한 궤도 카메라.
// 방위각(azimuth)·고도각(elevation)·거리(radius)로 위치를 정의한다.
// 마우스 좌드래그로 회전, 휠로 줌. Scene03~05 등 3D 씬에서 공용.
//***************************************************************************************
#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

class OrbitCamera
{
public:
    float azimuth = 0.6f;     // 라디안 (y축 기준 좌우 회전)
    float elevation = 0.4f;   // 라디안 (상하)
    float radius = 6.0f;
    DirectX::XMFLOAT3 target = { 0.0f, 0.6f, 0.0f };

    void Rotate(float dAz, float dEl)
    {
        azimuth += dAz;
        elevation += dEl;
        // 짐벌락 방지
        const float lim = DirectX::XM_PIDIV2 - 0.05f;
        elevation = std::clamp(elevation, -lim, lim);
    }
    void Zoom(float delta)
    {
        radius = std::clamp(radius - delta, 2.0f, 20.0f);
    }

    DirectX::XMFLOAT3 Eye() const
    {
        float ce = std::cos(elevation), se = std::sin(elevation);
        float ca = std::cos(azimuth), sa = std::sin(azimuth);
        return {
            target.x + radius * ce * ca,
            target.y + radius * se,
            target.z + radius * ce * sa
        };
    }

    DirectX::XMMATRIX View() const
    {
        using namespace DirectX;
        XMFLOAT3 e = Eye();
        return XMMatrixLookAtLH(
            XMVectorSet(e.x, e.y, e.z, 1.0f),
            XMVectorSet(target.x, target.y, target.z, 1.0f),
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    }

    DirectX::XMMATRIX Proj(float aspect) const
    {
        return DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, aspect, 0.1f, 200.0f);
    }
};
