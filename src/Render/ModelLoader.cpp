#define _CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#pragma warning(push, 0)
#include "cgltf.h"
#include "stb_image.h"
#pragma warning(pop)

#include "ModelLoader.h"
#include <DirectXMath.h>
#include <unordered_map>

using namespace DirectX;

namespace
{
    XMMATRIX NodeWorld(const cgltf_node* node)
    {
        float m[16];
        cgltf_node_transform_world(node, m);   // column-major
        XMMATRIX cm = XMMATRIX(
            m[0], m[1], m[2], m[3],
            m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11],
            m[12], m[13], m[14], m[15]);
        return XMMatrixTranspose(cm);           // → 행우선
    }
}

namespace model
{
    bool LoadGLB(const char* path, Model& out)
    {
        cgltf_options options = {};
        cgltf_data* data = nullptr;
        if (cgltf_parse_file(&options, path, &data) != cgltf_result_success)
            return false;
        if (cgltf_load_buffers(&options, data, path) != cgltf_result_success)
        {
            cgltf_free(data);
            return false;
        }

        out.images.clear();
        out.subs.clear();
        XMVECTOR vmin = XMVectorReplicate(1e9f), vmax = XMVectorReplicate(-1e9f);

        // cgltf_image* → out.images 인덱스 (중복 디코드 방지)
        std::unordered_map<const cgltf_image*, int> imageMap;

        auto decodeImage = [&](const cgltf_image* img) -> int
        {
            if (!img) return -1;
            auto it = imageMap.find(img);
            if (it != imageMap.end()) return it->second;

            int index = -1;
            if (img->buffer_view)   // glb 임베드
            {
                const cgltf_buffer_view* bv = img->buffer_view;
                const uint8_t* src = (const uint8_t*)bv->buffer->data + bv->offset;
                int w = 0, h = 0, comp = 0;
                stbi_uc* px = stbi_load_from_memory(src, (int)bv->size, &w, &h, &comp, 4);
                if (px)
                {
                    Image im; im.w = w; im.h = h; im.rgba.assign(px, px + (size_t)w * h * 4);
                    stbi_image_free(px);
                    index = (int)out.images.size();
                    out.images.push_back(std::move(im));
                }
            }
            imageMap[img] = index;
            return index;
        };

        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
        {
            cgltf_node* node = &data->nodes[ni];
            if (!node->mesh) continue;
            XMMATRIX world = NodeWorld(node);
            XMMATRIX worldIT = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
            {
                cgltf_primitive* prim = &node->mesh->primitives[pi];
                if (prim->type != cgltf_primitive_type_triangles) continue;

                const cgltf_accessor* posA = nullptr;
                const cgltf_accessor* norA = nullptr;
                const cgltf_accessor* uvA = nullptr;
                for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai)
                {
                    cgltf_attribute* a = &prim->attributes[ai];
                    if (a->type == cgltf_attribute_type_position) posA = a->data;
                    else if (a->type == cgltf_attribute_type_normal) norA = a->data;
                    else if (a->type == cgltf_attribute_type_texcoord && a->index == 0) uvA = a->data;
                }
                if (!posA) continue;

                SubMesh sub;

                // --- 머티리얼 ---
                if (prim->material)
                {
                    cgltf_material* mat = prim->material;
                    sub.doubleSided = mat->double_sided != 0;
                    if (mat->alpha_mode != cgltf_alpha_mode_opaque)
                    {
                        sub.alphaTest = true;
                        sub.alphaCutoff = (mat->alpha_mode == cgltf_alpha_mode_mask) ? mat->alpha_cutoff : 0.5f;
                    }
                    if (mat->has_pbr_metallic_roughness)
                    {
                        const cgltf_pbr_metallic_roughness& pbr = mat->pbr_metallic_roughness;
                        sub.baseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1],
                                          pbr.base_color_factor[2], pbr.base_color_factor[3] };
                        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                            sub.imageIndex = decodeImage(pbr.base_color_texture.texture->image);
                    }
                }

                // --- 정점 ---
                cgltf_size vcount = posA->count;
                sub.vertices.reserve(vcount);
                for (cgltf_size vi = 0; vi < vcount; ++vi)
                {
                    VertexPNUT v{};
                    float p[3] = { 0,0,0 }, n[3] = { 0,1,0 }, uv[2] = { 0,0 };
                    cgltf_accessor_read_float(posA, vi, p, 3);
                    if (norA) cgltf_accessor_read_float(norA, vi, n, 3);
                    if (uvA)  cgltf_accessor_read_float(uvA, vi, uv, 2);

                    XMVECTOR wp = XMVector3Transform(XMVectorSet(p[0], p[1], p[2], 1.0f), world);
                    XMVECTOR wn = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(n[0], n[1], n[2], 0.0f), worldIT));
                    XMStoreFloat3(&v.pos, wp);
                    XMStoreFloat3(&v.normal, wn);
                    v.uv = { uv[0], uv[1] };
                    v.tangent = { 1, 0, 0, 1 };
                    sub.vertices.push_back(v);
                    vmin = XMVectorMin(vmin, wp);
                    vmax = XMVectorMax(vmax, wp);
                }

                // --- 인덱스 ---
                if (prim->indices)
                {
                    cgltf_size ic = prim->indices->count;
                    sub.indices.reserve(ic);
                    for (cgltf_size ii = 0; ii < ic; ++ii)
                        sub.indices.push_back((unsigned int)cgltf_accessor_read_index(prim->indices, ii));
                }
                else
                {
                    for (cgltf_size ii = 0; ii < vcount; ++ii)
                        sub.indices.push_back((unsigned int)ii);
                }

                if (!sub.vertices.empty())
                    out.subs.push_back(std::move(sub));
            }
        }

        cgltf_free(data);
        XMStoreFloat3(&out.min, vmin);
        XMStoreFloat3(&out.max, vmax);
        return !out.subs.empty();
    }
}
