#define _CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)

#include "ModelLoader.h"
#include <DirectXMath.h>
#include <algorithm>

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
        // cgltf는 열우선 → XMMATRIX(행우선)로 쓰려면 전치
        return XMMatrixTranspose(cm);
    }
}

namespace model
{
    bool LoadGLB(const char* path, MeshData& out, XMFLOAT3& outMin, XMFLOAT3& outMax)
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

        out.vertices.clear();
        out.indices.clear();
        XMVECTOR vmin = XMVectorReplicate(1e9f), vmax = XMVectorReplicate(-1e9f);

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

                unsigned int base = (unsigned int)out.vertices.size();
                cgltf_size vcount = posA->count;
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
                    out.vertices.push_back(v);

                    vmin = XMVectorMin(vmin, wp);
                    vmax = XMVectorMax(vmax, wp);
                }

                if (prim->indices)
                {
                    cgltf_size ic = prim->indices->count;
                    for (cgltf_size ii = 0; ii < ic; ++ii)
                        out.indices.push_back(base + (unsigned int)cgltf_accessor_read_index(prim->indices, ii));
                }
                else
                {
                    for (cgltf_size ii = 0; ii < vcount; ++ii)
                        out.indices.push_back(base + (unsigned int)ii);
                }
            }
        }

        cgltf_free(data);

        XMStoreFloat3(&outMin, vmin);
        XMStoreFloat3(&outMax, vmax);
        return !out.vertices.empty();
    }
}
