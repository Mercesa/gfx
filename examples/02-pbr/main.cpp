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
#include "gfx_imgui.h"

#include "ibl.h"
#include "gpu_scene.h"
#include "fly_camera.h"
#include "glm/gtc/matrix_transform.hpp"
#include "math_helper.h"

#include <iostream>
#include <chrono>


namespace
{
char const *environment_map_path = "data/kiara_1_dawn_4k.hdr";
char const *scene_path           = "data/SciFiHelmet/glTF/SciFiHelmet.gltf";

struct Light {
    glm::vec3 position;
    float pad0;
    glm::vec3 color;
    float pad1;
};

} //! unnamed namespace


    
int main()
{
    GfxWindow  window = gfxCreateWindow(1920, 1080, "Niels Gfx");
    GfxContext gfx    = gfxCreateContext(window);
    GfxScene   scene  = gfxCreateScene();
    gfxImGuiInitialize(gfx);

    // Import the scene data
    gfxSceneImport(scene, scene_path);
    gfxSceneImport(scene, environment_map_path);
    GpuScene gpu_scene = UploadSceneToGpuMemory(gfx, scene);

    // Process the environment for image-based lighting (i.e., IBL)
    GfxConstRef<GfxImage> environment_image = gfxSceneFindObjectByAssetFile<GfxImage>(scene, environment_map_path);

    GfxTexture environment_map = (environment_image ? gpu_scene.textures[(uint32_t)environment_image] : GfxTexture());
    
    IBL const ibl = ConvolveIBL(gfx, environment_map);

    // Create our color (i.e., HDR) and depth buffers
    GfxTexture color_buffer    = gfxCreateTexture2D(gfx, DXGI_FORMAT_R32G32B32A32_FLOAT);
    GfxTexture history_buffer  = gfxCreateTexture2D(gfx, DXGI_FORMAT_R16G16B16A16_FLOAT);
    GfxTexture resolve_buffer  = gfxCreateTexture2D(gfx, DXGI_FORMAT_R16G16B16A16_FLOAT);
    GfxTexture velocity_buffer = gfxCreateTexture2D(gfx, DXGI_FORMAT_R16G16_FLOAT);
    GfxTexture depth_buffer    = gfxCreateTexture2D(gfx, DXGI_FORMAT_D32_FLOAT);


    GfxTexture normal_ao_buffer = gfxCreateTexture2D(gfx, DXGI_FORMAT_R16G16B16A16_FLOAT);
    GfxTexture albedo_metal_roughness_packed_buffer = gfxCreateTexture2D(gfx, DXGI_FORMAT_R32G32B32A32_FLOAT);
    GfxTexture world_roughness_buffer = gfxCreateTexture2D(gfx, DXGI_FORMAT_R32G32B32A32_FLOAT);


    // Create our PBR programs and kernels
    GfxProgram sky_program = gfxCreateProgram(gfx, "sky");
    GfxProgram taa_program = gfxCreateProgram(gfx, "taa");
    GfxProgram post_program = gfxCreateProgram(gfx, "post");
    GfxProgram deferred_program = gfxCreateProgram(gfx, "deferred");
    GfxProgram pbr_program = gfxCreateProgram(gfx, "pbr");

    // Create our sampler states
    GfxSamplerState linear_sampler  = gfxCreateSamplerState(gfx, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    GfxSamplerState nearest_sampler = gfxCreateSamplerState(gfx, D3D12_FILTER_MIN_MAG_MIP_POINT);

    // PBR
    ////////
    GfxDrawState pbr_draw_state;
    gfxDrawStateSetColorTarget(pbr_draw_state, 0, world_roughness_buffer);
    gfxDrawStateSetColorTarget(pbr_draw_state, 1, velocity_buffer);
    gfxDrawStateSetColorTarget(pbr_draw_state, 2, albedo_metal_roughness_packed_buffer);
    gfxDrawStateSetColorTarget(pbr_draw_state, 3, normal_ao_buffer);
    //gfxDrawStateSetColorTarget(pbr_draw_state, 4, metal_roughness_buffer);
    gfxDrawStateSetDepthStencilTarget(pbr_draw_state, depth_buffer);


    GfxKernel pbr_kernel = gfxCreateGraphicsKernel(gfx, pbr_program, pbr_draw_state);

    gfxProgramSetParameter(gfx, pbr_program, "g_BrdfBuffer", ibl.brdf_buffer);
    gfxProgramSetParameter(gfx, pbr_program, "g_IrradianceBuffer", ibl.irradiance_buffer);
    gfxProgramSetParameter(gfx, pbr_program, "g_EnvironmentBuffer", ibl.environment_buffer);

    gfxProgramSetParameter(gfx, sky_program, "g_EnvironmentBuffer", ibl.environment_buffer);

    // Bind a bunch of shader parameters
    BindGpuScene(gfx, pbr_program, gpu_scene);
    
    // TAA
    ///////
    GfxDrawState reproject_draw_state;
    gfxDrawStateSetColorTarget(reproject_draw_state, 0, resolve_buffer);

    GfxKernel reproject_kernel = gfxCreateGraphicsKernel(gfx, taa_program, reproject_draw_state, "Reproject");
    GfxKernel resolve_kernel = gfxCreateGraphicsKernel(gfx, taa_program, reproject_draw_state, "Resolve");

    gfxProgramSetParameter(gfx, taa_program, "g_ColorBuffer", color_buffer);
    gfxProgramSetParameter(gfx, taa_program, "g_HistoryBuffer", history_buffer);
    gfxProgramSetParameter(gfx, taa_program, "g_ResolveBuffer", resolve_buffer);
    gfxProgramSetParameter(gfx, taa_program, "g_VelocityBuffer", velocity_buffer);
    gfxProgramSetParameter(gfx, taa_program, "g_DepthBuffer", depth_buffer);

    // DEFERRED
    ///////
    GfxDrawState deferred_draw_state;
    
    gfxDrawStateSetColorTarget(deferred_draw_state, 0, color_buffer);
    GfxKernel deferred_kernel = gfxCreateGraphicsKernel(gfx, deferred_program, deferred_draw_state);

    gfxProgramSetParameter(gfx, deferred_program, "g_BrdfBuffer", ibl.brdf_buffer);
    gfxProgramSetParameter(gfx, deferred_program, "g_EnvironmentBuffer", ibl.environment_buffer);
    gfxProgramSetParameter(gfx, deferred_program, "g_IrradianceBuffer", ibl.irradiance_buffer);
    gfxProgramSetParameter(gfx, deferred_program, "g_Albedo", albedo_metal_roughness_packed_buffer);
    gfxProgramSetParameter(gfx, deferred_program, "g_Normal_Ao", normal_ao_buffer);
    gfxProgramSetParameter(gfx, deferred_program, "g_World", world_roughness_buffer);

    // POST
    ////////
    GfxKernel post_kernel = gfxCreateComputeKernel(gfx, post_program);

    gfxProgramSetParameter(gfx, post_program, "g_Output", resolve_buffer);

    // Set samplers
    gfxProgramSetParameter(gfx, pbr_program, "g_LinearSampler", linear_sampler);
    gfxProgramSetParameter(gfx, sky_program, "g_LinearSampler", linear_sampler);
    gfxProgramSetParameter(gfx, taa_program, "g_LinearSampler", linear_sampler);
    gfxProgramSetParameter(gfx, taa_program, "g_NearestSampler", nearest_sampler);
    gfxProgramSetParameter(gfx, deferred_program, "g_NearestSampler", nearest_sampler);


    // SKY
    //////
    GfxDrawState sky_draw_state;
    gfxDrawStateSetColorTarget(sky_draw_state, 0, color_buffer);
    gfxDrawStateSetDepthStencilTarget(sky_draw_state, depth_buffer);

    GfxKernel sky_kernel = gfxCreateGraphicsKernel(gfx, sky_program, sky_draw_state);

    // Run the application loop
    FlyCamera fly_camera = CreateFlyCamera(gfx, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f));

    float delta_time = 0.0f;
    auto last_frame = std::chrono::high_resolution_clock::now();

    float yaw = -90.0f;
    float pitch = 0.0f;
    while(!gfxWindowIsCloseRequested(window))
    {
        auto current_frame = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::centi> elapsed = current_frame - last_frame;
        delta_time = elapsed.count();
        last_frame = current_frame;

        gfxWindowPumpEvents(window);

        float cursor_speed = 0.3f;
        yaw += gfxWindowXMouseOffset(window) * cursor_speed;
        pitch += gfxWindowYMouseOffset(window) * cursor_speed;

        glm::vec3 direction;
        direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        direction.y = sin(glm::radians(pitch));
        direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        glm::vec3 camera_front = glm::normalize(direction);

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 camera_right = glm::normalize(glm::cross(up, camera_front));
        glm::vec3 camera_up = glm::normalize(glm::cross(camera_front, camera_right));


        float speed = 1.0f;
        if (gfxWindowIsKeyDown(window, 0x57)) {
            fly_camera.eye += camera_front * delta_time;
        }
        if (gfxWindowIsKeyDown(window, 0x41)) {
            fly_camera.eye += camera_right * delta_time;
        }
        if (gfxWindowIsKeyDown(window, 0x53)) {
            fly_camera.eye -= camera_front * delta_time;
        }
        if (gfxWindowIsKeyDown(window, 0x44)) {
            fly_camera.eye -= camera_right * delta_time;
        }

        if (gfxWindowIsKeyDown(window, 0x1B)) {
            break;
        }

        fly_camera.center = fly_camera.eye + camera_front;
        fly_camera.up = camera_up;

        UpdateFlyCameraMatrix(gfx, fly_camera);


        // Update our GPU scene and camera
        UpdateGpuScene(gfx, scene, gpu_scene);

        UpdateFlyCamera(gfx, window, fly_camera);

        gfxProgramSetParameter(gfx, pbr_program, "g_Eye", fly_camera.eye);
        gfxProgramSetParameter(gfx, sky_program, "g_Eye", fly_camera.eye);
        gfxProgramSetParameter(gfx, deferred_program, "g_Eye", fly_camera.eye);

        gfxProgramSetParameter(gfx, sky_program, "g_ViewProjectionInverse", glm::inverse(fly_camera.view_proj));

        // Update texel size (can change if window is resized)
        float const texel_size[] =
        {
            1.0f / gfxGetBackBufferWidth(gfx),
            1.0f / gfxGetBackBufferHeight(gfx)
        };

        gfxProgramSetParameter(gfx, sky_program, "g_TexelSize", texel_size);
        gfxProgramSetParameter(gfx, taa_program, "g_TexelSize", texel_size);
        gfxProgramSetParameter(gfx, deferred_program, "g_TexelSize", texel_size);


        // Clear our render targets
        gfxCommandClearTexture(gfx, color_buffer);
        gfxCommandClearTexture(gfx, velocity_buffer);
        gfxCommandClearTexture(gfx, depth_buffer);
        gfxCommandClearTexture(gfx, normal_ao_buffer);
        gfxCommandClearTexture(gfx, albedo_metal_roughness_packed_buffer);
        gfxCommandClearTexture(gfx, world_roughness_buffer);


        // Draw all the meshes in the scene
        uint32_t const instance_count = gfxSceneGetInstanceCount(scene);

        gfxCommandBindKernel(gfx, pbr_kernel);
        gfxCommandBindIndexBuffer(gfx, gpu_scene.index_buffer);
        gfxCommandBindVertexBuffer(gfx, gpu_scene.vertex_buffer);

        for(uint32_t i = 0; i < instance_count; ++i)
        {
            GfxConstRef<GfxInstance> const instance_ref = gfxSceneGetInstanceHandle(scene, i);

            uint32_t const instance_id = (uint32_t)instance_ref;
            uint32_t const mesh_id     = (uint32_t)instance_ref->mesh;

            Mesh const mesh = gpu_scene.meshes[mesh_id];

            gfxProgramSetParameter(gfx, pbr_program, "g_InstanceId", instance_id);
            gfxProgramSetParameter(gfx, pbr_program, "g_ViewProjection", fly_camera.view_proj);
            gfxProgramSetParameter(gfx, pbr_program, "g_PreviousViewProjection", fly_camera.prev_view_proj);

            gfxCommandDrawIndexed(gfx, mesh.count, 1, mesh.first_index, mesh.base_vertex);
        }

        gfxCommandBindKernel(gfx, deferred_kernel);
        gfxCommandDraw(gfx, 3);
        
        // Draw our skybox
        gfxCommandBindKernel(gfx, sky_kernel);
        gfxCommandDraw(gfx, 3);

        // Reproject the temporal history (a.k.a., TAA)
        gfxCommandBindKernel(gfx, reproject_kernel);
        gfxCommandDraw(gfx, 3);

        // Update the temporal history with the new anti-aliased frame
        gfxCommandCopyTexture(gfx, history_buffer, resolve_buffer);

        // Resolve into the backbuffer
        gfxCommandBindKernel(gfx, resolve_kernel);
        gfxCommandDraw(gfx, 3);

        // Post kernel
        gfxCommandBindKernel(gfx, post_kernel);
        gfxCommandDispatch(gfx, 
            divide_up(static_cast<float>(gfxGetBackBufferWidth(gfx)), 8.0f), 
            divide_up(static_cast<float>(gfxGetBackBufferHeight(gfx)),8.0f), 
            1);

        gfxCommandCopyTextureToBackBuffer(gfx, resolve_buffer);

        // And submit the frame
        gfxImGuiRender();
        gfxFrame(gfx);
    }

    // Release our resources
    gfxDestroyTexture(gfx, color_buffer);
    gfxDestroyTexture(gfx, depth_buffer);
    gfxDestroyTexture(gfx, history_buffer);
    gfxDestroyTexture(gfx, resolve_buffer);
    gfxDestroyTexture(gfx, velocity_buffer);

    gfxDestroyTexture(gfx, normal_ao_buffer);
    gfxDestroyTexture(gfx, albedo_metal_roughness_packed_buffer);
    gfxDestroyTexture(gfx, world_roughness_buffer);


    ReleaseIBL(gfx, ibl);
    gfxDestroySamplerState(gfx, linear_sampler);
    gfxDestroySamplerState(gfx, nearest_sampler);


    gfxDestroyKernel(gfx, post_kernel);
    gfxDestroyProgram(gfx, post_program);
    gfxDestroyKernel(gfx, pbr_kernel);
    gfxDestroyProgram(gfx, pbr_program);
    gfxDestroyKernel(gfx, sky_kernel);
    gfxDestroyProgram(gfx, sky_program);
    gfxDestroyKernel(gfx, resolve_kernel);
    gfxDestroyKernel(gfx, reproject_kernel);
    gfxDestroyProgram(gfx, taa_program);
    gfxDestroyKernel(gfx, deferred_kernel);
    gfxDestroyProgram(gfx, deferred_program);

    gfxImGuiTerminate();
    gfxDestroyScene(scene);
    ReleaseGpuScene(gfx, gpu_scene);

    gfxDestroyContext(gfx);
    gfxDestroyWindow(window);

    return 0;
}
