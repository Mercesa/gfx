/****************************************************************************
MIT License

Copyright (c) 2022 Guillaume Boissé

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
****************************************************************************/
#include "../common/gpu_scene.hlsli"

float3 g_Eye;
uint   g_InstanceId;

Texture2D   g_BrdfBuffer;
TextureCube g_IrradianceBuffer;
TextureCube g_EnvironmentBuffer;

SamplerState g_LinearSampler;

struct Params
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 world    : POSITION0;
    float4 current  : POSITION1;
    float4 previous : POSITION2;
};

struct Result
{
    float4 world_pos        : SV_Target0;
    float2 velocity         : SV_Target1;
    float4 albedo           : SV_Target2;
    float4 normal_ao        : SV_Target3;
};

// https://seblagarde.wordpress.com/2011/08/17/hello-world/
float3 FresnelSchlickRoughness(in float n_dot_v, in float3 F0, in float roughness)
{
    return F0 + (max(F0, 1.0f - roughness) - F0) * pow(1.0f - n_dot_v, 5.0f);
}

// Calculates motion vectors in UV-space (i.e., normalized [0, 1] coordinates)
float2 CalculateVelocity(in Params params)
{
    float2 ndc_velocity = params.current.xy / params.current.w
                        - params.previous.xy / params.previous.w;
    float2 uv_velocity  = ndc_velocity * float2(0.5f, -0.5f);

    return uv_velocity;
}

Result main(in Params params)
{
    // Load our material
    Instance instance = g_InstanceBuffer[g_InstanceId];
    Mesh     mesh     = g_MeshBuffer[instance.mesh_id];
    Material material = g_MaterialBuffer[mesh.material_id];

    // Load and sample our texture maps
    uint albedo_map      = asuint(material.albedo.w);
    uint roughness_map   = asuint(material.metallicity_roughness.w);
    uint metallicity_map = asuint(material.metallicity_roughness.y);
    uint normal_map      = asuint(material.ao_normal_emissivity.y);
    uint ao_map          = asuint(material.ao_normal_emissivity.x);

    if(albedo_map != uint(-1))
    {
        material.albedo.xyz *= g_Textures[albedo_map].Sample(g_TextureSampler, params.uv).xyz;
    }

    if(roughness_map != uint(-1))
    {
        material.metallicity_roughness.z *= g_Textures[roughness_map].Sample(g_TextureSampler, params.uv).x;
    }

    if(metallicity_map != uint(-1))
    {
        material.metallicity_roughness.x *= g_Textures[metallicity_map].Sample(g_TextureSampler, params.uv).x;
    }

    if(normal_map != uint(-1))
    {
        float3 normal         = normalize(params.normal);
        float3 view_direction = normalize(g_Eye - params.world);

        float3 dp1  = ddx(-view_direction);
        float3 dp2  = ddy(-view_direction);
        float2 duv1 = ddx(params.uv);
        float2 duv2 = ddy(params.uv);

        float3 dp2perp   = normalize(cross(dp2, normal));
        float3 dp1perp   = normalize(cross(normal, dp1));
        float3 tangent   = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;

        float    invmax  = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
        float3x3 tbn     = transpose(float3x3(tangent * invmax, bitangent * invmax, normal));
        float3   disturb = 2.0f * g_Textures[normal_map].Sample(g_TextureSampler, params.uv).xyz - 1.0f;

        params.normal = mul(tbn, disturb);
    }

    if(ao_map != uint(-1))
    {
        material.ao_normal_emissivity.x = g_Textures[ao_map].Sample(g_TextureSampler, params.uv).x;
    }
    else
    {
        material.ao_normal_emissivity.x = 1.0f;
    }

    // Post-process our material properties
    material.albedo.xyz              = sqrt(material.albedo.xyz);
    material.metallicity_roughness.x = saturate(material.metallicity_roughness.x);
    material.metallicity_roughness.z = clamp(material.metallicity_roughness.z * material.metallicity_roughness.z, 0.01f, 1.0f);

    // And shade :)
    float3 albedo      = material.albedo.xyz;
    float3 normal      = normalize(params.normal);
    float  roughness   = material.metallicity_roughness.z;
    float  metallicity = material.metallicity_roughness.x;
    float  ao          = material.ao_normal_emissivity.x;

    // Populate our multiple render targets (i.e., MRT)
    Result result;
    result.world_pos    = float4(params.world, roughness);
    result.velocity = CalculateVelocity(params);
    result.albedo = float4(albedo, metallicity);
    result.normal_ao = float4(normal, ao);

    return result;
}
