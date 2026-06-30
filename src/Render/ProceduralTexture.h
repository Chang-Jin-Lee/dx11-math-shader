//***************************************************************************************
// ProceduralTexture.h
//
// 벽돌 디퓨즈/노말맵을 코드로 생성한다(외부 애셋 불필요).
// 높이장(height field)을 만들고 살짝 블러한 뒤, 그라디언트로 탄젠트 공간 노말맵을 유도한다.
//***************************************************************************************
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tex
{
    inline float Hash(int x, int y)
    {
        int n = x * 73856093 ^ y * 19349663;
        n = (n << 13) ^ n;
        return ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483648.0f;
    }

    // 벽돌 디퓨즈/노말 SRV 생성. 실패 시 false.
    inline bool CreateBrickTextures(ID3D11Device* device,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outDiffuse,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outNormal,
        int size = 256)
    {
        const int W = size, H = size;
        const int brickH = H / 4;      // 세로 4줄
        const int brickW = W / 2;      // 가로 2장(어긋나기 포함)
        const int mortar = std::max(4, size / 40);

        // 1) 높이장 + 벽돌 인덱스
        std::vector<float> height(W * H);
        std::vector<float> tint(W * H, 0.0f);
        for (int y = 0; y < H; ++y)
        {
            int row = y / brickH;
            int offset = (row % 2) ? brickW / 2 : 0;
            for (int x = 0; x < W; ++x)
            {
                int yb = y % brickH;
                int xb = (x + offset) % brickW;
                bool inMortar = (yb < mortar) || (xb < mortar);
                height[y * W + x] = inMortar ? 0.0f : 1.0f;
                // 벽돌마다 약간 다른 색조
                int brickId_x = (x + offset) / brickW;
                tint[y * W + x] = Hash(brickId_x, row);
            }
        }

        // 2) 박스 블러(노말 부드럽게)
        auto blur = [&](std::vector<float>& src) {
            std::vector<float> dst(src.size());
            const int r = std::max(1, mortar / 2);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    float s = 0; int c = 0;
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx)
                        {
                            int sx = (x + dx + W) % W, sy = (y + dy + H) % H;
                            s += src[sy * W + sx]; ++c;
                        }
                    dst[y * W + x] = s / c;
                }
            src.swap(dst);
        };
        blur(height); blur(height);

        // 3) 디퓨즈 + 노말 픽셀 채우기 (RGBA8)
        std::vector<uint32_t> diffuse(W * H), normal(W * H);
        const float strength = 3.0f;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                float h = height[y * W + x];

                // 디퓨즈: 벽돌(붉은 갈색) ↔ 모르타르(회색)
                float t = tint[y * W + x] * 0.25f;
                float br_r = 0.55f + t, br_g = 0.22f + t * 0.5f, br_b = 0.18f + t * 0.4f;
                float mo = 0.55f;
                float rr = mo + (br_r - mo) * h;
                float gg = mo + (br_g - mo) * h;
                float bb = mo + (br_b - mo) * h;
                auto toByte = [](float v) { return (uint32_t)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
                diffuse[y * W + x] = toByte(rr) | (toByte(gg) << 8) | (toByte(bb) << 16) | (0xFFu << 24);

                // 노말: 높이장 그라디언트
                float hl = height[y * W + ((x - 1 + W) % W)];
                float hr = height[y * W + ((x + 1) % W)];
                float hd = height[((y - 1 + H) % H) * W + x];
                float hu = height[((y + 1) % H) * W + x];
                float nx = (hl - hr) * strength;
                float ny = (hd - hu) * strength;
                float nz = 1.0f;
                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= len; ny /= len; nz /= len;
                auto enc = [](float v) { return (uint32_t)((v * 0.5f + 0.5f) * 255.0f + 0.5f); };
                normal[y * W + x] = enc(nx) | (enc(ny) << 8) | (enc(nz) << 16) | (0xFFu << 24);
            }
        }

        // 4) 텍스처 + SRV 생성
        auto makeSRV = [&](const std::vector<uint32_t>& pixels,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out) -> bool
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem = pixels.data();
            sd.SysMemPitch = W * 4;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
            if (FAILED(device->CreateTexture2D(&td, &sd, t.GetAddressOf()))) return false;
            return SUCCEEDED(device->CreateShaderResourceView(t.Get(), nullptr, out.GetAddressOf()));
        };

        return makeSRV(diffuse, outDiffuse) && makeSRV(normal, outNormal);
    }
}
