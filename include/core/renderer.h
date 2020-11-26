#pragma once

#include <resource/scene.h>

namespace lumen
{
#define MAX_DEBUG_RAY_DRAW_COUNT 1024

class Integrator;
class ResourceManager;

class Renderer
{
private:
    struct RayDebugView
    {
        glm::ivec2 pixel_coord;
        glm::mat4 view;
        glm::mat4 projection;
    };

    std::vector<RayDebugView>  m_ray_debug_views;
    std::weak_ptr<vk::Backend> m_backend;
    vk::Buffer::Ptr            m_tlas_instance_buffer_device;
    vk::Image::Ptr             m_output_images[2];
    vk::ImageView::Ptr         m_output_image_views[2];
    vk::DescriptorSet::Ptr     m_output_storage_image_ds[2];
    vk::DescriptorSet::Ptr     m_input_combined_sampler_ds[2];
    vk::GraphicsPipeline::Ptr  m_tone_map_pipeline;
    vk::PipelineLayout::Ptr    m_tone_map_pipeline_layout;
    vk::GraphicsPipeline::Ptr  m_ray_debug_pipeline;
    vk::PipelineLayout::Ptr    m_ray_debug_pipeline_layout;
    vk::Buffer::Ptr            m_ray_debug_vbo;
    vk::Buffer::Ptr            m_ray_debug_draw_cmd;
    vk::Buffer::Ptr            m_ray_debug_draw_count;
    bool                       m_output_ping_pong = false;
    bool                       m_ray_debug_view_added = false;

public:
    Renderer(vk::Backend::Ptr backend);
    ~Renderer();

    void render(RenderState& render_state, std::shared_ptr<Integrator> integrator);
    void on_window_resize();
    void add_ray_debug_view(const glm::ivec2& pixel_coord, const glm::mat4& view, const glm::mat4& projection);
    void clear_ray_debug_views();

private:
    void tone_map(vk::CommandBuffer::Ptr cmd_buf, vk::DescriptorSet::Ptr read_image);
    void render_ray_debug_views(RenderState& render_state);
    void create_output_images();
    void create_tone_map_pipeline();
    void create_ray_debug_pipeline();
    void create_ray_debug_buffers();
    void create_buffers();
    void create_descriptor_sets();
};
} // namespace lumen