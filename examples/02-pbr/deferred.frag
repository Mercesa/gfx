float3 g_Eye;

float2 g_TexelSize;

SamplerState g_LinearSampler;
SamplerState g_NearestSampler;

Texture2D g_Albedo;
Texture2D g_Normal_Ao;
Texture2D g_BrdfBuffer;
Texture2D g_World;
TextureCube g_IrradianceBuffer;
TextureCube g_EnvironmentBuffer;

// https://seblagarde.wordpress.com/2011/08/17/hello-world/
float3 FresnelSchlickRoughness(in float n_dot_v, in float3 F0, in float roughness)
{
    return F0 + (max(F0, 1.0f - roughness) - F0) * pow(1.0f - n_dot_v, 5.0f);
}

float4 main(in float4 pos : SV_Position) : SV_Target
{
    float2 uv = pos.xy * g_TexelSize;

    float4 normal_ao = g_Normal_Ao.Sample(g_LinearSampler, uv);
    float4 albedo_metalness = g_Albedo.Sample(g_LinearSampler, uv);
    

    float4 world_roughness      = g_World.Sample(g_LinearSampler, uv);
    float3 view_direction       = normalize(g_Eye - world_roughness.rgb);
    float  n_dot_v              = max(dot(normal_ao.rgb, view_direction), 0.0f);
    float2 brdf_uv              = float2(max(dot(normal_ao.rgb, view_direction), 0.0f), world_roughness.a);
    float3 reflection_direction = reflect(-view_direction, normal_ao.rgb);

    float3 F0 = lerp(0.04f, albedo_metalness.rgb, albedo_metalness.a);
    float3 F  = FresnelSchlickRoughness(n_dot_v, F0, world_roughness.a);
    float3 kD = (1.0f - F) * (1.0f - albedo_metalness.a * (1.0f - world_roughness.a));

    float2 brdf        = g_BrdfBuffer.SampleLevel(g_LinearSampler, brdf_uv, 0.0f).xy;
    float3 irradiance  = g_IrradianceBuffer.SampleLevel(g_LinearSampler, normal_ao.rgb, 0.0f).xyz;
    float3 environment = g_EnvironmentBuffer.SampleLevel(g_LinearSampler, reflection_direction, world_roughness.a * 4.0f).xyz;

    float  so       = saturate(normal_ao.a + (normal_ao.a + n_dot_v) * (normal_ao.a + n_dot_v) - 1.0f);
    float3 diffuse  = kD * normal_ao.a * albedo_metalness.rgb * irradiance;
    float3 specular = (F * brdf.x + brdf.y) * lerp(normal_ao.a * (F * brdf.x + brdf.y) * irradiance, environment, so);

    float3 color = diffuse + specular;
    return float4(color, 1.0);
}
