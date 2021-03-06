#include <resource/scene.h>
#include <resource/mesh.h>
#include <resource/material.h>
#include <resource/texture.h>
#include <utility/profiler.h>
#include <vk_mem_alloc.h>
#include <unordered_set>
#include <gtx/matrix_decompose.hpp>

namespace helios
{
// -----------------------------------------------------------------------------------------------------------------------------------

enum LightType
{
    LIGHT_DIRECTIONAL,
    LIGHT_SPOT,
    LIGHT_POINT,
    LIGHT_ENVIRONMENT_MAP,
    LIGHT_AREA
};

// -----------------------------------------------------------------------------------------------------------------------------------

struct MaterialData
{
    glm::ivec4 texture_indices0 = glm::ivec4(-1); // x: albedo, y: normals, z: roughness, w: metallic
    glm::ivec4 texture_indices1 = glm::ivec4(-1); // x: emissive, z: roughness_channel, w: metallic_channel
    glm::vec4  albedo;
    glm::vec4  emissive;
    glm::vec4  roughness_metallic;
};

// -----------------------------------------------------------------------------------------------------------------------------------

struct LightData
{
    glm::vec4 light_data0; // x: light type, yzw: color    | x: light_type, y: mesh_id, z: material_id, w: primitive_offset
    glm::vec4 light_data1; // xyz: direction, w: intensity | x: primitive_count
    glm::vec4 light_data2; // xyz: position, w: radius
    glm::vec4 light_data3; // x: cos_inner, y: cos_outer
};

// -----------------------------------------------------------------------------------------------------------------------------------

struct InstanceData
{
    glm::mat4 model_matrix;
    glm::mat4 normal_matrix;
    uint32_t  mesh_index;
    float     padding[3];
};

static uint32_t g_node_counter = 0;

// -----------------------------------------------------------------------------------------------------------------------------------

Node::Node(const NodeType& type, const std::string& name) :
    m_type(type), m_name(name), m_id(g_node_counter++)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

Node::~Node()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Node::mid_frame_cleanup()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Node::add_child(Node::Ptr child)
{
    m_is_heirarchy_dirty = true;
    child->m_parent      = this;
    m_children.push_back(child);
}

// -----------------------------------------------------------------------------------------------------------------------------------

Node::Ptr Node::find_child(const std::string& name)
{
    for (auto child : m_children)
    {
        if (child->m_name == name)
            return child;
        else
        {
            auto found = child->find_child(name);

            if (found)
                return found;
        }
    }

    return nullptr;
}

// -----------------------------------------------------------------------------------------------------------------------------------

Node::Ptr Node::find_child(const NodeType& type)
{
    for (auto child : m_children)
    {
        if (child->type() == type)
            return child;
        else
        {
            auto found = child->find_child(type);

            if (found)
                return found;
        }
    }

    return nullptr;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Node::remove_child(const std::string& name)
{
    m_is_heirarchy_dirty = true;
    int child_to_remove  = -1;

    for (int i = 0; i < m_children.size(); i++)
    {
        if (m_children[i]->m_name == name)
        {
            child_to_remove = i;
            break;
        }
    }

    if (child_to_remove != -1)
    {
        m_children[child_to_remove]->mid_frame_cleanup();
        m_children.erase(m_children.begin() + child_to_remove);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Node::update_children(RenderState& render_state)
{
    if (m_is_heirarchy_dirty)
    {
        render_state.m_scene_state = SCENE_STATE_HIERARCHY_UPDATED;
        m_is_heirarchy_dirty       = false;
    }

    for (auto& child : m_children)
        child->update(render_state);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Node::mark_transforms_as_dirty()
{
    m_is_transform_dirty = true;

    for (auto& child : m_children)
        child->mark_transforms_as_dirty();
}

// -----------------------------------------------------------------------------------------------------------------------------------

TransformNode::TransformNode(const NodeType& type, const std::string& name) :
    Node(type, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

TransformNode::~TransformNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::update(RenderState& render_state)
{
    if (m_is_transform_dirty)
    {
        glm::mat4 R = glm::mat4_cast(m_orientation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), m_scale);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), m_position);

        m_prev_model_matrix          = m_model_matrix;
        m_model_matrix_without_scale = T * R;
        m_model_matrix               = m_model_matrix_without_scale * S;

        render_state.m_scene_state = SCENE_STATE_HIERARCHY_UPDATED;

        m_is_transform_dirty = false;
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::forward()
{
    return m_orientation * glm::vec3(0.0f, 0.0f, 1.0f);
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::up()
{
    return m_orientation * glm::vec3(0.0f, 1.0f, 0.0f);
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::left()
{
    return m_orientation * glm::vec3(1.0f, 0.0f, 0.0f);
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::local_position()
{
    return m_position;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::global_position()
{
    TransformNode* parent_transform = dynamic_cast<TransformNode*>(m_parent);

    if (parent_transform)
        return parent_transform->m_model_matrix_without_scale * glm::vec4(m_position, 1.0f);
    else
        return m_position;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::mat4 TransformNode::global_transform()
{
    TransformNode* parent_transform = dynamic_cast<TransformNode*>(m_parent);

    if (parent_transform)
        return parent_transform->m_model_matrix_without_scale * m_model_matrix;
    else
        return m_model_matrix;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::mat4 TransformNode::global_transform_without_scale()
{
    TransformNode* parent_transform = dynamic_cast<TransformNode*>(m_parent);

    if (parent_transform)
        return parent_transform->m_model_matrix_without_scale * m_model_matrix_without_scale;
    else
        return m_model_matrix_without_scale;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::mat4 TransformNode::local_transform()
{
    return m_model_matrix;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::mat4 TransformNode::normal_matrix()
{
    return global_transform_without_scale();
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::quat TransformNode::orientation()
{
    return m_orientation;
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 TransformNode::scale()
{
    return m_scale;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_from_local_transform(const glm::mat4& transform)
{
    mark_transforms_as_dirty();

    glm::mat4 local_transform = transform;

    glm::vec3 out_skew;
    glm::vec4 out_persp;

    glm::decompose(local_transform, m_scale, m_orientation, m_position, out_skew, out_persp);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_from_global_transform(const glm::mat4& transform)
{
    mark_transforms_as_dirty();

    TransformNode* parent_transform = dynamic_cast<TransformNode*>(m_parent);

    glm::mat4 local_transform = transform;

    if (parent_transform)
    {
        glm::mat4 inverse_parent_transform = glm::inverse(parent_transform->m_model_matrix_without_scale);
        local_transform                    = inverse_parent_transform * transform;
    }

    glm::vec3 out_skew;
    glm::vec4 out_persp;

    glm::decompose(local_transform, m_scale, m_orientation, m_position, out_skew, out_persp);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_orientation(const glm::quat& q)
{
    mark_transforms_as_dirty();

    m_orientation = q;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_orientation_from_euler_yxz(const glm::vec3& e)
{
    mark_transforms_as_dirty();

    glm::quat pitch = glm::quat(glm::vec3(glm::radians(e.x), glm::radians(0.0f), glm::radians(0.0f)));
    glm::quat yaw   = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(e.y), glm::radians(0.0f)));
    glm::quat roll  = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(0.0f), glm::radians(e.z)));

    m_orientation = yaw * pitch * roll;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_orientation_from_euler_xyz(const glm::vec3& e)
{
    mark_transforms_as_dirty();

    glm::quat pitch = glm::quat(glm::vec3(glm::radians(e.x), glm::radians(0.0f), glm::radians(0.0f)));
    glm::quat yaw   = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(e.y), glm::radians(0.0f)));
    glm::quat roll  = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(0.0f), glm::radians(e.z)));

    m_orientation = pitch * yaw * roll;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_position(const glm::vec3& position)
{
    mark_transforms_as_dirty();

    m_position = position;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::set_scale(const glm::vec3& scale)
{
    mark_transforms_as_dirty();

    m_scale = scale;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::move(const glm::vec3& displacement)
{
    mark_transforms_as_dirty();

    m_position += displacement;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::rotate_euler_yxz(const glm::vec3& e)
{
    mark_transforms_as_dirty();

    glm::quat pitch = glm::quat(glm::vec3(glm::radians(e.x), glm::radians(0.0f), glm::radians(0.0f)));
    glm::quat yaw   = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(e.y), glm::radians(0.0f)));
    glm::quat roll  = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(0.0f), glm::radians(e.z)));

    glm::quat delta = yaw * pitch * roll;
    m_orientation   = m_orientation * delta;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void TransformNode::rotate_euler_xyz(const glm::vec3& e)
{
    mark_transforms_as_dirty();

    glm::quat pitch = glm::quat(glm::vec3(glm::radians(e.x), glm::radians(0.0f), glm::radians(0.0f)));
    glm::quat yaw   = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(e.y), glm::radians(0.0f)));
    glm::quat roll  = glm::quat(glm::vec3(glm::radians(0.0f), glm::radians(0.0f), glm::radians(e.z)));

    glm::quat delta = pitch * yaw * roll;
    m_orientation   = m_orientation * delta;
}

// -----------------------------------------------------------------------------------------------------------------------------------

RootNode::RootNode(const std::string& name) :
    TransformNode(NODE_ROOT, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

RootNode::~RootNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void RootNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);
        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

MeshNode::MeshNode(const std::string& name) :
    TransformNode(NODE_MESH, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

MeshNode::~MeshNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);

        if (m_mesh)
            render_state.m_meshes.push_back(this);

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::mid_frame_cleanup()
{
    if (m_mesh)
    {
        auto backend = m_mesh->backend().lock();

        if (backend)
            backend->queue_object_deletion(m_mesh);
    }

    mid_frame_material_cleanup();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::mid_frame_material_cleanup()
{
    if (m_material_override)
    {
        auto backend = m_material_override->backend().lock();
        backend->queue_object_deletion(m_material_override);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::set_mesh(std::shared_ptr<Mesh> mesh)
{
    mid_frame_cleanup();

    m_mesh = mesh;

    create_instance_data_buffer();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::set_material_override(std::shared_ptr<Material> material_override)
{
    mid_frame_material_cleanup();

    m_material_override = material_override;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void MeshNode::create_instance_data_buffer()
{
    if (m_mesh)
    {
        auto backend = m_mesh->backend().lock();

        if (backend)
        {
            backend->queue_object_deletion(m_material_indices_buffer);
            m_material_indices_buffer = vk::Buffer::create(backend, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(glm::uvec2) * (m_mesh->sub_meshes().size()), VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

DirectionalLightNode::DirectionalLightNode(const std::string& name) :
    TransformNode(NODE_DIRECTIONAL_LIGHT, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

DirectionalLightNode::~DirectionalLightNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void DirectionalLightNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);

        render_state.m_directional_lights.push_back(this);

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

SpotLightNode::SpotLightNode(const std::string& name) :
    TransformNode(NODE_SPOT_LIGHT, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

SpotLightNode::~SpotLightNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void SpotLightNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);

        render_state.m_spot_lights.push_back(this);

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

PointLightNode::PointLightNode(const std::string& name) :
    TransformNode(NODE_POINT_LIGHT, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

PointLightNode::~PointLightNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void PointLightNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);

        render_state.m_point_lights.push_back(this);

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

CameraNode::CameraNode(const std::string& name) :
    TransformNode(NODE_CAMERA, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

CameraNode::~CameraNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void CameraNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        TransformNode::update(render_state);

        m_projection_matrix = glm::perspective(glm::radians(m_fov), float(render_state.viewport_width()) / float(render_state.viewport_height()), m_near_plane, m_far_plane);
        m_view_matrix       = glm::inverse(global_transform_without_scale());

        if (!render_state.m_camera)
            render_state.m_camera = this;

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 CameraNode::camera_forward()
{
    return -forward();
}

// -----------------------------------------------------------------------------------------------------------------------------------

glm::vec3 CameraNode::camera_left()
{
    return -left();
}

// -----------------------------------------------------------------------------------------------------------------------------------

IBLNode::IBLNode(const std::string& name) :
    Node(NODE_IBL, name)
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

IBLNode::~IBLNode()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void IBLNode::update(RenderState& render_state)
{
    if (m_is_enabled)
    {
        if (!render_state.m_ibl_environment_map)
            render_state.m_ibl_environment_map = this;

        update_children(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void IBLNode::set_image(std::shared_ptr<TextureCube> image)
{
    mid_frame_cleanup();

    m_image = image;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void IBLNode::mid_frame_cleanup()
{
    if (m_image)
    {
        auto backend = m_image->backend().lock();

        if (backend)
            backend->queue_object_deletion(m_image);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

RenderState::RenderState()
{
    m_meshes.reserve(100000);
    m_directional_lights.reserve(100000);
    m_spot_lights.reserve(100000);
    m_point_lights.reserve(100000);
}

// -----------------------------------------------------------------------------------------------------------------------------------

RenderState::~RenderState()
{
}

// -----------------------------------------------------------------------------------------------------------------------------------

void RenderState::clear()
{
    m_meshes.clear();
    m_directional_lights.clear();
    m_spot_lights.clear();
    m_point_lights.clear();
    m_camera              = nullptr;
    m_ibl_environment_map = nullptr;
    m_read_image_ds       = nullptr;
    m_write_image_ds      = nullptr;
    m_scene_ds            = nullptr;
    m_cmd_buffer          = nullptr;
    m_scene               = nullptr;
    m_vbo_ds              = nullptr;
    m_ibo_ds              = nullptr;
    m_material_indices_ds = nullptr;
    m_texture_ds          = nullptr;
    m_ray_debug_ds        = nullptr;
    m_num_lights          = 0;
    m_scene_state         = SCENE_STATE_READY;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void RenderState::setup(uint32_t width, uint32_t height, vk::CommandBuffer::Ptr cmd_buffer)
{
    clear();
    m_viewport_width  = width;
    m_viewport_height = height;
    m_cmd_buffer      = cmd_buffer;
}

// -----------------------------------------------------------------------------------------------------------------------------------

Scene::Ptr Scene::create(vk::Backend::Ptr backend, const std::string& name, Node::Ptr root, const std::string& path)
{
    return std::shared_ptr<Scene>(new Scene(backend, name, root, path));
}

// -----------------------------------------------------------------------------------------------------------------------------------

Scene::Scene(vk::Backend::Ptr backend, const std::string& name, Node::Ptr root, const std::string& path) :
    m_name(name), m_path(path), m_backend(backend), m_root(root), vk::Object(backend)
{
    // Allocate device instance buffer
    m_tlas.instance_buffer_device = vk::Buffer::create(backend, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, sizeof(VkAccelerationStructureInstanceKHR) * MAX_SCENE_MESH_INSTANCE_COUNT, VMA_MEMORY_USAGE_GPU_ONLY, 0);

    VkDeviceOrHostAddressConstKHR instance_device_address {};
    instance_device_address.deviceAddress = m_tlas.instance_buffer_device->device_address();

    // Allocate host instance buffer
    m_tlas.instance_buffer_host = vk::Buffer::create(backend, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, sizeof(VkAccelerationStructureInstanceKHR) * MAX_SCENE_MESH_INSTANCE_COUNT, VMA_MEMORY_USAGE_CPU_ONLY, VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // Create TLAS
    VkAccelerationStructureGeometryKHR tlas_geometry;
    HELIOS_ZERO_MEMORY(tlas_geometry);

    tlas_geometry.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlas_geometry.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlas_geometry.geometry.instances.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlas_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    tlas_geometry.geometry.instances.data            = instance_device_address;

    vk::AccelerationStructure::Desc desc;

    desc.set_geometry_count(1);
    desc.set_geometries({ tlas_geometry });
    desc.set_max_primitive_counts({ MAX_SCENE_MESH_INSTANCE_COUNT });
    desc.set_type(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
    desc.set_flags(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

    m_tlas.tlas = vk::AccelerationStructure::create(backend, desc);

    // Allocate scratch buffer
    m_tlas.scratch_buffer = vk::Buffer::create(backend, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_tlas.tlas->build_sizes().buildScratchSize, VMA_MEMORY_USAGE_GPU_ONLY, 0);

    vk::DescriptorPool::Desc dp_desc;

    dp_desc.set_max_sets(25)
        .add_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10)
        .add_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SCENE_MATERIAL_TEXTURE_COUNT)
        .add_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * MAX_SCENE_MESH_INSTANCE_COUNT)
        .add_pool_size(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10);

    m_descriptor_pool = vk::DescriptorPool::create(backend, dp_desc);

    // Allocate descriptor set
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_ds_alloc_info;
    HELIOS_ZERO_MEMORY(variable_ds_alloc_info);

    uint32_t variable_desc_count = MAX_SCENE_MESH_INSTANCE_COUNT;

    variable_ds_alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable_ds_alloc_info.descriptorSetCount = 1;
    variable_ds_alloc_info.pDescriptorCounts  = &variable_desc_count;

    m_scene_descriptor_set = vk::DescriptorSet::create(backend, backend->scene_descriptor_set_layout(), m_descriptor_pool);
    m_scene_descriptor_set->set_name("Scene Descriptor Set");

    m_vbo_descriptor_set = vk::DescriptorSet::create(backend, backend->buffer_array_descriptor_set_layout(), m_descriptor_pool, &variable_ds_alloc_info);
    m_vbo_descriptor_set->set_name("VBO Descriptor Set");

    m_ibo_descriptor_set = vk::DescriptorSet::create(backend, backend->buffer_array_descriptor_set_layout(), m_descriptor_pool, &variable_ds_alloc_info);
    m_ibo_descriptor_set->set_name("IBO Descriptor Set");

    m_material_indices_descriptor_set = vk::DescriptorSet::create(backend, backend->buffer_array_descriptor_set_layout(), m_descriptor_pool, &variable_ds_alloc_info);
    m_material_indices_descriptor_set->set_name("Material Indices Descriptor Set");

    variable_desc_count       = MAX_SCENE_MATERIAL_TEXTURE_COUNT;
    m_textures_descriptor_set = vk::DescriptorSet::create(backend, backend->combined_sampler_array_descriptor_set_layout(), m_descriptor_pool, &variable_ds_alloc_info);
    m_textures_descriptor_set->set_name("Textures Descriptor Set");

    // Create light data buffer
    m_light_data_buffer = vk::Buffer::create(backend, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(LightData) * MAX_SCENE_LIGHT_COUNT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // Create material data buffer
    m_material_data_buffer = vk::Buffer::create(backend, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(MaterialData) * MAX_SCENE_MATERIAL_COUNT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // Create instance data buffer
    m_instance_data_buffer = vk::Buffer::create(backend, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(InstanceData) * MAX_SCENE_MESH_INSTANCE_COUNT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);

    update_static_descriptors();

    m_sky_model = std::unique_ptr<HosekWilkieSkyModel>(new HosekWilkieSkyModel(backend));
}

// -----------------------------------------------------------------------------------------------------------------------------------

Scene::~Scene()
{
    m_sky_model.reset();
    m_textures_descriptor_set.reset();
    m_material_indices_descriptor_set.reset();
    m_ibo_descriptor_set.reset();
    m_vbo_descriptor_set.reset();
    m_scene_descriptor_set.reset();
    m_descriptor_pool.reset();
    m_tlas.scratch_buffer.reset();
    m_tlas.instance_buffer_host.reset();
    m_tlas.tlas.reset();
    m_light_data_buffer.reset();
    m_material_data_buffer.reset();
    m_root.reset();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Scene::update(RenderState& render_state)
{
    auto backend = m_backend.lock();

    render_state.m_scene_ds            = m_scene_descriptor_set;
    render_state.m_vbo_ds              = m_vbo_descriptor_set;
    render_state.m_ibo_ds              = m_ibo_descriptor_set;
    render_state.m_material_indices_ds = m_material_indices_descriptor_set;
    render_state.m_texture_ds          = m_textures_descriptor_set;
    render_state.m_scene               = this;

    {
        HELIOS_SCOPED_SAMPLE("Gather Render State");
        m_root->update(render_state);
    }

    render_state.m_num_lights = m_num_area_lights + render_state.m_directional_lights.size() + render_state.m_spot_lights.size() + render_state.m_point_lights.size();

    if (render_state.ibl_environment_map() && render_state.ibl_environment_map()->image())
        render_state.m_num_lights++;
    else if (render_state.m_directional_lights.size() > 0)
    {
        render_state.m_num_lights++;
        m_sky_model->update(render_state.cmd_buffer(), -render_state.m_directional_lights[0]->forward());
    }

    if (m_force_update)
    {
        render_state.m_scene_state = SCENE_STATE_HIERARCHY_UPDATED;
        m_force_update             = false;
    }

    {
        HELIOS_SCOPED_SAMPLE("Upload GPU Resources");
        create_gpu_resources(render_state);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Scene::create_gpu_resources(RenderState& render_state)
{
    if (render_state.m_scene_state != SCENE_STATE_READY)
    {
        // Copy lights
        uint32_t   gpu_light_counter = 0;
        LightData* light_buffer      = (LightData*)m_light_data_buffer->mapped_ptr();

        if (render_state.m_scene_state == SCENE_STATE_HIERARCHY_UPDATED)
        {
            m_num_area_lights = 0;

            auto backend = m_backend.lock();

            backend->wait_idle();

            std::unordered_set<uint32_t>           processed_meshes;
            std::unordered_set<uint32_t>           processed_materials;
            std::unordered_set<uint32_t>           processed_textures;
            std::unordered_map<uint32_t, uint32_t> global_material_indices;
            uint32_t                               mesh_index_counter = 0;

            std::vector<VkDescriptorBufferInfo> vbo_descriptors;
            std::vector<VkDescriptorBufferInfo> ibo_descriptors;
            std::vector<VkDescriptorImageInfo>  image_descriptors;
            std::vector<VkDescriptorBufferInfo> material_indices_descriptors;
            uint32_t                            gpu_material_counter = 0;
            MaterialData*                       material_buffer      = (MaterialData*)m_material_data_buffer->mapped_ptr();

            for (int mesh_node_idx = 0; mesh_node_idx < render_state.m_meshes.size(); mesh_node_idx++)
            {
                auto&       mesh_node = render_state.m_meshes[mesh_node_idx];
                auto&       mesh      = mesh_node->mesh();
                const auto& materials = mesh->materials();
                const auto& submeshes = mesh->sub_meshes();

                if (processed_meshes.find(mesh->id()) == processed_meshes.end())
                {
                    processed_meshes.insert(mesh->id());

                    m_global_mesh_indices[mesh->id()] = mesh_index_counter++;

                    VkDescriptorBufferInfo ibo_info;

                    ibo_info.buffer = mesh->index_buffer()->handle();
                    ibo_info.offset = 0;
                    ibo_info.range  = VK_WHOLE_SIZE;

                    ibo_descriptors.push_back(ibo_info);

                    VkDescriptorBufferInfo vbo_info;

                    vbo_info.buffer = mesh->vertex_buffer()->handle();
                    vbo_info.offset = 0;
                    vbo_info.range  = VK_WHOLE_SIZE;

                    vbo_descriptors.push_back(vbo_info);

                    for (uint32_t i = 0; i < submeshes.size(); i++)
                    {
                        const SubMesh& submesh  = submeshes[i];
                        auto           material = materials[submesh.mat_idx];

                        if (mesh_node->material_override())
                            material = mesh_node->material_override();

                        if (processed_materials.find(material->id()) == processed_materials.end())
                        {
                            processed_materials.insert(material->id());

                            MaterialData& material_data = material_buffer[gpu_material_counter++];

                            material_data.texture_indices0   = glm::ivec4(-1);
                            material_data.texture_indices1   = glm::ivec4(-1);
                            material_data.albedo             = glm::vec4(0.0f);
                            material_data.emissive           = glm::vec4(0.0f);
                            material_data.roughness_metallic = glm::vec4(0.0f);

                            // Fill GPUMaterial
                            if (material->albedo_texture())
                            {
                                auto texture = material->albedo_texture();

                                if (processed_textures.find(texture->id()) == processed_textures.end())
                                {
                                    processed_textures.insert(texture->id());

                                    VkDescriptorImageInfo image_info;

                                    image_info.sampler     = backend->trilinear_sampler()->handle();
                                    image_info.imageView   = texture->image_view()->handle();
                                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                                    material_data.texture_indices0.x = image_descriptors.size();

                                    image_descriptors.push_back(image_info);
                                }
                            }
                            else
                            {
                                material_data.albedo = material->albedo_value();
                                // Covert from sRGB to Linear
                                material_data.albedo = glm::vec4(glm::pow(glm::vec3(material_data.albedo[0], material_data.albedo[1], material_data.albedo[2]), glm::vec3(2.2f)), material_data.albedo.a);
                            }

                            if (material->normal_texture())
                            {
                                auto texture = material->normal_texture();

                                if (processed_textures.find(texture->id()) == processed_textures.end())
                                {
                                    processed_textures.insert(texture->id());

                                    VkDescriptorImageInfo image_info;

                                    image_info.sampler     = backend->trilinear_sampler()->handle();
                                    image_info.imageView   = texture->image_view()->handle();
                                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                                    material_data.texture_indices0.y = image_descriptors.size();

                                    image_descriptors.push_back(image_info);
                                }
                            }

                            if (material->roughness_texture())
                            {
                                auto texture = material->roughness_texture();

                                if (processed_textures.find(texture->id()) == processed_textures.end())
                                {
                                    processed_textures.insert(texture->id());

                                    VkDescriptorImageInfo image_info;

                                    image_info.sampler     = backend->trilinear_sampler()->handle();
                                    image_info.imageView   = texture->image_view()->handle();
                                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                                    material_data.texture_indices0.z = image_descriptors.size();
                                    material_data.texture_indices1.z = material->roughness_texture_info().array_index;

                                    image_descriptors.push_back(image_info);
                                }
                            }
                            else
                                material_data.roughness_metallic.x = material->roughness_value();

                            if (material->metallic_texture())
                            {
                                auto texture = material->metallic_texture();

                                if (processed_textures.find(texture->id()) == processed_textures.end())
                                {
                                    processed_textures.insert(texture->id());

                                    VkDescriptorImageInfo image_info;

                                    image_info.sampler     = backend->trilinear_sampler()->handle();
                                    image_info.imageView   = texture->image_view()->handle();
                                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                                    material_data.texture_indices0.w = image_descriptors.size();
                                    material_data.texture_indices1.w = material->metallic_texture_info().array_index;

                                    image_descriptors.push_back(image_info);
                                }
                            }
                            else
                                material_data.roughness_metallic.y = material->metallic_value();

                            if (material->emissive_texture())
                            {
                                auto texture = material->emissive_texture();

                                if (processed_textures.find(texture->id()) == processed_textures.end())
                                {
                                    processed_textures.insert(texture->id());

                                    VkDescriptorImageInfo image_info;

                                    image_info.sampler     = backend->trilinear_sampler()->handle();
                                    image_info.imageView   = texture->image_view()->handle();
                                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                                    material_data.texture_indices1.x = image_descriptors.size();

                                    image_descriptors.push_back(image_info);
                                }
                            }
                            else
                                material_data.emissive = material->emissive_value();

                            global_material_indices[material->id()] = gpu_material_counter - 1;
                        }

                        if (material->is_emissive())
                        {
                            m_num_area_lights++;

                            LightData& light_data = light_buffer[gpu_light_counter++];

                            light_data.light_data0 = glm::vec4(float(LIGHT_AREA), float(mesh_node_idx), float(global_material_indices[material->id()]), float(submesh.base_index / 3));
                            light_data.light_data1 = glm::vec4(float(submesh.index_count / 3), 0.0f, 0.0f, 0.0f);
                        }
                    }
                }

                VkDescriptorBufferInfo material_indice_info;

                material_indice_info.buffer = mesh_node->material_indices_buffer()->handle();
                material_indice_info.offset = 0;
                material_indice_info.range  = VK_WHOLE_SIZE;

                material_indices_descriptors.push_back(material_indice_info);

                glm::uvec2* primitive_offsets_material_indices = (glm::uvec2*)mesh_node->material_indices_buffer()->mapped_ptr();

                // Set submesh materials
                for (uint32_t i = 0; i < submeshes.size(); i++)
                {
                    const auto& submesh = submeshes[i];

                    auto material = materials[submeshes[i].mat_idx];

                    if (mesh_node->material_override())
                        material = mesh_node->material_override();

                    glm::uvec2 pair = glm::uvec2(submesh.base_index / 3, global_material_indices[material->id()]);

                    primitive_offsets_material_indices[i] = pair;
                }
            }

            VkDescriptorImageInfo environment_map_info;

            environment_map_info.sampler = backend->bilinear_sampler()->handle();

            if (render_state.ibl_environment_map() && render_state.ibl_environment_map()->image())
                environment_map_info.imageView = render_state.ibl_environment_map()->image()->image_view()->handle();
            else
            {
                if (render_state.m_directional_lights.size() > 0)
                    environment_map_info.imageView = m_sky_model->cubemap()->handle();
                else
                    environment_map_info.imageView = backend->default_cubemap()->handle();
            }

            environment_map_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::vector<VkWriteDescriptorSet> write_datas;

            VkWriteDescriptorSet write_data;
            HELIOS_ZERO_MEMORY(write_data);

            write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_data.descriptorCount = 1;
            write_data.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write_data.pImageInfo      = &environment_map_info;
            write_data.dstBinding      = 4;
            write_data.dstSet          = m_scene_descriptor_set->handle();

            write_datas.push_back(write_data);

            HELIOS_ZERO_MEMORY(write_data);

            if (vbo_descriptors.size() > 0)
            {
                write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_data.descriptorCount = vbo_descriptors.size();
                write_data.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write_data.pBufferInfo     = vbo_descriptors.data();
                write_data.dstBinding      = 0;
                write_data.dstSet          = m_vbo_descriptor_set->handle();

                write_datas.push_back(write_data);
            }

            if (ibo_descriptors.size() > 0)
            {
                HELIOS_ZERO_MEMORY(write_data);

                write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_data.descriptorCount = ibo_descriptors.size();
                write_data.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write_data.pBufferInfo     = ibo_descriptors.data();
                write_data.dstBinding      = 0;
                write_data.dstSet          = m_ibo_descriptor_set->handle();

                write_datas.push_back(write_data);
            }

            if (material_indices_descriptors.size() > 0)
            {
                HELIOS_ZERO_MEMORY(write_data);

                write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_data.descriptorCount = material_indices_descriptors.size();
                write_data.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write_data.pBufferInfo     = material_indices_descriptors.data();
                write_data.dstBinding      = 0;
                write_data.dstSet          = m_material_indices_descriptor_set->handle();

                write_datas.push_back(write_data);
            }

            if (image_descriptors.size() > 0)
            {
                HELIOS_ZERO_MEMORY(write_data);

                write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_data.descriptorCount = image_descriptors.size();
                write_data.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write_data.pImageInfo      = image_descriptors.data();
                write_data.dstBinding      = 0;
                write_data.dstSet          = m_textures_descriptor_set->handle();

                write_datas.push_back(write_data);
            }

            if (write_datas.size() > 0)
                vkUpdateDescriptorSets(backend->device(), write_datas.size(), write_datas.data(), 0, nullptr);
        }

        InstanceData*                       instance_buffer          = (InstanceData*)m_instance_data_buffer->mapped_ptr();
        VkAccelerationStructureInstanceKHR* geometry_instance_buffer = (VkAccelerationStructureInstanceKHR*)m_tlas.instance_buffer_host->mapped_ptr();

        for (int mesh_node_idx = 0; mesh_node_idx < render_state.m_meshes.size(); mesh_node_idx++)
        {
            auto& mesh_node = render_state.m_meshes[mesh_node_idx];
            auto& mesh      = mesh_node->mesh();

            // Copy geometry instance data
            VkAccelerationStructureInstanceKHR& rt_instance = geometry_instance_buffer[mesh_node_idx];

            glm::mat3x4 transform = glm::mat3x4(glm::transpose(mesh_node->global_transform()));

            memcpy(&rt_instance.transform, &transform, sizeof(rt_instance.transform));

            rt_instance.instanceCustomIndex                    = mesh_node_idx;
            rt_instance.mask                                   = 0xFF;
            rt_instance.instanceShaderBindingTableRecordOffset = 0;
            rt_instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            rt_instance.accelerationStructureReference         = mesh->acceleration_structure()->device_address();

            // Update instance data
            InstanceData& instance_data = instance_buffer[mesh_node_idx];

            // Set mesh data index
            instance_data.mesh_index    = m_global_mesh_indices[mesh->id()];
            instance_data.model_matrix  = mesh_node->global_transform();
            instance_data.normal_matrix = mesh_node->normal_matrix();
        }

        if ((render_state.ibl_environment_map() && render_state.ibl_environment_map()->image()) || render_state.m_directional_lights.size() > 0)
        {
            LightData& light_data = light_buffer[gpu_light_counter++];

            light_data.light_data0 = glm::vec4(float(LIGHT_ENVIRONMENT_MAP), 0.0f, 0.0f, 0.0f);
        }

        for (int i = 0; i < render_state.m_directional_lights.size(); i++)
        {
            auto light = render_state.m_directional_lights[i];

            LightData& light_data = light_buffer[gpu_light_counter++];

            light_data.light_data0 = glm::vec4(float(LIGHT_DIRECTIONAL), light->color());
            light_data.light_data1 = glm::vec4(light->forward(), light->intensity());
            light_data.light_data2 = glm::vec4(0.0f, 0.0f, 0.0f, light->radius());
        }

        for (int i = 0; i < render_state.m_point_lights.size(); i++)
        {
            auto light = render_state.m_point_lights[i];

            LightData& light_data = light_buffer[gpu_light_counter++];

            light_data.light_data0 = glm::vec4(float(LIGHT_POINT), light->color());
            light_data.light_data1 = glm::vec4(0.0f, 0.0f, 0.0f, light->intensity());
            light_data.light_data2 = glm::vec4(light->global_position(), light->radius());
            light_data.light_data3 = glm::vec4(0.0f);
        }

        for (int i = 0; i < render_state.m_spot_lights.size(); i++)
        {
            auto light = render_state.m_spot_lights[i];

            LightData& light_data = light_buffer[gpu_light_counter++];

            light_data.light_data0 = glm::vec4(float(LIGHT_SPOT), light->color());
            light_data.light_data1 = glm::vec4(light->forward(), light->intensity());
            light_data.light_data2 = glm::vec4(light->global_position(), light->radius());
            light_data.light_data3 = glm::vec4(cosf(glm::radians(light->inner_cone_angle())), cosf(glm::radians(light->outer_cone_angle())), 0.0f, 0.0f);
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Scene::update_static_descriptors()
{
    auto backend = m_backend.lock();

    VkDescriptorBufferInfo material_buffer_info;

    material_buffer_info.buffer = m_material_data_buffer->handle();
    material_buffer_info.offset = 0;
    material_buffer_info.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo instance_buffer_info;

    instance_buffer_info.buffer = m_instance_data_buffer->handle();
    instance_buffer_info.offset = 0;
    instance_buffer_info.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo light_buffer_info;

    light_buffer_info.buffer = m_light_data_buffer->handle();
    light_buffer_info.offset = 0;
    light_buffer_info.range  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write_data[4];

    HELIOS_ZERO_MEMORY(write_data[0]);
    HELIOS_ZERO_MEMORY(write_data[1]);
    HELIOS_ZERO_MEMORY(write_data[2]);
    HELIOS_ZERO_MEMORY(write_data[3]);

    write_data[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_data[0].descriptorCount = 1;
    write_data[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_data[0].pBufferInfo     = &material_buffer_info;
    write_data[0].dstBinding      = 0;
    write_data[0].dstSet          = m_scene_descriptor_set->handle();

    write_data[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_data[1].descriptorCount = 1;
    write_data[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_data[1].pBufferInfo     = &instance_buffer_info;
    write_data[1].dstBinding      = 1;
    write_data[1].dstSet          = m_scene_descriptor_set->handle();

    write_data[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_data[2].descriptorCount = 1;
    write_data[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_data[2].pBufferInfo     = &light_buffer_info;
    write_data[2].dstBinding      = 2;
    write_data[2].dstSet          = m_scene_descriptor_set->handle();

    VkWriteDescriptorSetAccelerationStructureKHR descriptor_as;

    descriptor_as.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    descriptor_as.pNext                      = nullptr;
    descriptor_as.accelerationStructureCount = 1;
    descriptor_as.pAccelerationStructures    = &m_tlas.tlas->handle();

    write_data[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_data[3].pNext           = &descriptor_as;
    write_data[3].descriptorCount = 1;
    write_data[3].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write_data[3].dstBinding      = 3;
    write_data[3].dstSet          = m_scene_descriptor_set->handle();

    vkUpdateDescriptorSets(backend->device(), 4, write_data, 0, nullptr);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Scene::set_root_node(Node::Ptr node)
{
    if (m_root)
        m_root->mid_frame_cleanup();

    m_root = node;
}

// -----------------------------------------------------------------------------------------------------------------------------------

Node::Ptr Scene::root_node()
{
    return m_root;
}

// -----------------------------------------------------------------------------------------------------------------------------------

Node::Ptr Scene::find_node(const std::string& name)
{
    if (m_root->name() == name)
        return m_root;
    else
        return m_root->find_child(name);
}

// -----------------------------------------------------------------------------------------------------------------------------------

CameraNode::Ptr Scene::find_camera()
{
    if (m_root->type() == NODE_CAMERA)
        return std::dynamic_pointer_cast<CameraNode>(m_root);
    else
        return std::dynamic_pointer_cast<CameraNode>(m_root->find_child(NODE_CAMERA));
}

// -----------------------------------------------------------------------------------------------------------------------------------
} // namespace helios