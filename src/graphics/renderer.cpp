#include <graphics/renderer.h>
#include <resources/Mesh.h>
#include <sokol_app.h>
#include <sokol_imgui.h>
#include <algorithm>
#include <cstring>
#include <cmath>

static const char* s_vsSrc = R"(
#pragma pack_matrix(column_major)

struct vs_in {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

struct vs_out {
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 world_pos : TEXCOORD4;
    float4 light_space_pos : TEXCOORD7;
    float4 color : COLOR;
    float4 light_dir_intensity : TEXCOORD1;
    float4 light_color : TEXCOORD2;
    float4 ambient_color : TEXCOORD3;
    float4 material_flags : TEXCOORD5;
    float4 material_params : TEXCOORD6;
};

cbuffer vs_params : register(b0) {
    float4x4 model;
    float4x4 view_proj;
    float4x4 light_view_proj;
    float4 base_color;
    float4 light_dir_intensity;
    float4 light_color;
    float4 ambient_color;
    float4 material_flags;
    float4 material_params;
};

vs_out vs_main(vs_in input) {
    vs_out output;
    float4 world_pos = mul(model, float4(input.pos, 1.0));
    output.pos = mul(view_proj, world_pos);
    output.normal = mul((float3x3)model, input.normal);
    output.texcoord = input.texcoord;
    output.world_pos = world_pos.xyz;
    output.light_space_pos = mul(light_view_proj, world_pos);
    output.color = base_color;
    output.light_dir_intensity = light_dir_intensity;
    output.light_color = light_color;
    output.ambient_color = ambient_color;
    output.material_flags = material_flags;
    output.material_params = material_params;
    return output;
}
)";

static const char* s_fsSrc = R"(
Texture2D<float4> tex : register(t0);
Texture2D<float4> normal_tex : register(t1);
Texture2D<float4> metallic_tex : register(t2);
Texture2D<float4> roughness_tex : register(t3);
Texture2D<float4> metallic_roughness_tex : register(t4);
Texture2D<float> shadow_tex : register(t5);
SamplerState smp : register(s0);
SamplerState normal_smp : register(s1);
SamplerState metallic_smp : register(s2);
SamplerState roughness_smp : register(s3);
SamplerState metallic_roughness_smp : register(s4);
SamplerComparisonState shadow_smp : register(s5);

struct ps_in {
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 world_pos : TEXCOORD4;
    float4 light_space_pos : TEXCOORD7;
    float4 color : COLOR;
    float4 light_dir_intensity : TEXCOORD1;
    float4 light_color : TEXCOORD2;
    float4 ambient_color : TEXCOORD3;
    float4 material_flags : TEXCOORD5;
    float4 material_params : TEXCOORD6;
};

float shadow_factor(ps_in input, float3 normal, float3 light_dir) {
    if (input.material_params.z < 0.5 || input.light_space_pos.w <= 0.0) {
        return 1.0;
    }

    float3 ndc = input.light_space_pos.xyz / input.light_space_pos.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    float bias = max(0.0015 * (1.0 - dot(normal, light_dir)), 0.0005);
    return shadow_tex.SampleCmpLevelZero(shadow_smp, uv, ndc.z - bias);
}

float3 apply_normal_map(ps_in input, float3 vertex_normal) {
    if (input.material_flags.x < 0.5) {
        return vertex_normal;
    }

    float3 normal_sample = normal_tex.Sample(normal_smp, input.texcoord).xyz * 2.0 - 1.0;

    float3 dp1 = ddx(input.world_pos);
    float3 dp2 = ddy(input.world_pos);
    float2 duv1 = ddx(input.texcoord);
    float2 duv2 = ddy(input.texcoord);

    float3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
    float3 bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
    tangent = normalize(tangent - vertex_normal * dot(vertex_normal, tangent));
    bitangent = normalize(cross(vertex_normal, tangent));

    return normalize(tangent * normal_sample.x + bitangent * normal_sample.y + vertex_normal * normal_sample.z);
}

float4 ps_main(ps_in input) : SV_TARGET {
    float3 light_dir = normalize(input.light_dir_intensity.xyz);
    float3 normal = apply_normal_map(input, normalize(input.normal));
    float3 view_dir = normalize(-input.world_pos);
    float3 half_dir = normalize(light_dir + view_dir);

    float metallic = saturate(input.material_flags.y > 0.5 ? metallic_tex.Sample(metallic_smp, input.texcoord).r : input.material_params.x);
    float roughness = saturate(input.material_flags.z > 0.5 ? roughness_tex.Sample(roughness_smp, input.texcoord).r : input.material_params.y);
    if (input.material_flags.w > 0.5) {
        float4 mr = metallic_roughness_tex.Sample(metallic_roughness_smp, input.texcoord);
        roughness = saturate(mr.g);
        metallic = saturate(mr.b);
    }
    roughness = clamp(roughness, 0.04, 1.0);

    float diff = max(dot(normal, light_dir), 0.0);
    float4 sample_color = tex.Sample(smp, input.texcoord);
    float4 mat_color = input.color * sample_color;
    float spec_power = lerp(96.0, 8.0, roughness);
    float specular = pow(max(dot(normal, half_dir), 0.0), spec_power) * (1.0 - roughness);
    float3 spec_color = lerp(float3(0.04, 0.04, 0.04), mat_color.xyz, metallic);
    float3 diffuse_color = mat_color.xyz * (1.0 - metallic);
    float shadow = shadow_factor(input, normal, light_dir);
    float3 direct = input.light_color.xyz * input.light_dir_intensity.w * shadow * (diffuse_color * diff + spec_color * specular);
    float3 ambient = input.ambient_color.xyz * mat_color.xyz;
    return float4(ambient + direct, mat_color.w);
}
)";

static const char* s_shadowVsSrc = R"(
#pragma pack_matrix(column_major)

struct vs_in {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

cbuffer vs_params : register(b0) {
    float4x4 model;
    float4x4 light_view_proj;
};

float4 vs_main(vs_in input) : SV_POSITION {
    return mul(light_view_proj, mul(model, float4(input.pos, 1.0)));
}
)";

struct ShadowUniforms {
    float model[16];
    float lightViewProj[16];
};

static const char* s_gridVsSrc = R"(
struct vs_in {
    float3 pos : POSITION;
    float4 color : COLOR;
    float follow_origin : TEXCOORD;
};

struct vs_out {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

cbuffer vs_params : register(b0) {
    float4x4 view_proj;
    float4 grid_origin;
};

vs_out vs_main(vs_in input) {
    vs_out output;
    float3 world_pos = input.pos + float3(grid_origin.x, 0.0, grid_origin.z) * input.follow_origin;
    output.pos = mul(view_proj, float4(world_pos, 1.0));
    output.color = input.color;
    return output;
}
)";

struct GridUniforms {
    float viewProj[16];
    float origin[4];
};

static const char* s_gridFsSrc = R"(
struct ps_in {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 ps_main(ps_in input) : SV_TARGET {
    return input.color;
}
)";

static const char* s_outlineVsSrc = R"(
struct vs_in {
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct vs_out {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

cbuffer vs_params : register(b0) {
    float4x4 view_proj;
};

vs_out vs_main(vs_in input) {
    vs_out output;
    output.pos = mul(view_proj, float4(input.pos, 1.0));
    output.color = input.color;
    return output;
}
)";

static const char* s_outlineFsSrc = R"(
struct ps_in {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 ps_main(ps_in input) : SV_TARGET {
    return input.color;
}
)";

bool Renderer::init() {
    m_passAction = {};
    m_passAction.colors[0].load_action = SG_LOADACTION_CLEAR;
    m_passAction.colors[0].clear_value = { 0.42f, 0.42f, 0.44f, 1.0f };
    m_passAction.depth.load_action = SG_LOADACTION_CLEAR;
    m_passAction.depth.clear_value = 1.0f;

    if (!initShaders()) {
        return false;
    }

    if (!initWhiteTexture()) {
        return false;
    }

    if (!initGrid()) {
        return false;
    }

    if (!initSelectionOutline()) {
        return false;
    }

    if (!initShadowPipeline()) {
        return false;
    }

    if (!initShadowMap(1024)) {
        return false;
    }

    return true;
}

bool Renderer::initShaders() {
    sg_shader_desc desc = {};
    desc.vertex_func.source = s_vsSrc;
    desc.fragment_func.source = s_fsSrc;
    desc.vertex_func.entry = "vs_main";
    desc.fragment_func.entry = "ps_main";
    desc.attrs[0].hlsl_sem_name = "POSITION";
    desc.attrs[0].hlsl_sem_index = 0;
    desc.attrs[1].hlsl_sem_name = "NORMAL";
    desc.attrs[1].hlsl_sem_index = 0;
    desc.attrs[2].hlsl_sem_name = "TEXCOORD";
    desc.attrs[2].hlsl_sem_index = 0;
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = sizeof(MeshUniforms);
    desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;

    desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    desc.views[0].texture.hlsl_register_t_n = 0;
    desc.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[1].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    desc.views[1].texture.hlsl_register_t_n = 1;
    desc.views[2].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[2].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[2].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    desc.views[2].texture.hlsl_register_t_n = 2;
    desc.views[3].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[3].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[3].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    desc.views[3].texture.hlsl_register_t_n = 3;
    desc.views[4].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[4].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[4].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    desc.views[4].texture.hlsl_register_t_n = 4;
    desc.views[5].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[5].texture.image_type = SG_IMAGETYPE_2D;
    desc.views[5].texture.sample_type = SG_IMAGESAMPLETYPE_DEPTH;
    desc.views[5].texture.hlsl_register_t_n = 5;

    desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.samplers[0].hlsl_register_s_n = 0;
    desc.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.samplers[1].hlsl_register_s_n = 1;
    desc.samplers[2].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[2].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.samplers[2].hlsl_register_s_n = 2;
    desc.samplers[3].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[3].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.samplers[3].hlsl_register_s_n = 3;
    desc.samplers[4].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[4].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.samplers[4].hlsl_register_s_n = 4;
    desc.samplers[5].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[5].sampler_type = SG_SAMPLERTYPE_COMPARISON;
    desc.samplers[5].hlsl_register_s_n = 5;

    desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[0].view_slot = 0;
    desc.texture_sampler_pairs[0].sampler_slot = 0;
    desc.texture_sampler_pairs[0].glsl_name = "tex";
    desc.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[1].view_slot = 1;
    desc.texture_sampler_pairs[1].sampler_slot = 1;
    desc.texture_sampler_pairs[1].glsl_name = "normal_tex";
    desc.texture_sampler_pairs[2].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[2].view_slot = 2;
    desc.texture_sampler_pairs[2].sampler_slot = 2;
    desc.texture_sampler_pairs[2].glsl_name = "metallic_tex";
    desc.texture_sampler_pairs[3].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[3].view_slot = 3;
    desc.texture_sampler_pairs[3].sampler_slot = 3;
    desc.texture_sampler_pairs[3].glsl_name = "roughness_tex";
    desc.texture_sampler_pairs[4].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[4].view_slot = 4;
    desc.texture_sampler_pairs[4].sampler_slot = 4;
    desc.texture_sampler_pairs[4].glsl_name = "metallic_roughness_tex";
    desc.texture_sampler_pairs[5].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[5].view_slot = 5;
    desc.texture_sampler_pairs[5].sampler_slot = 5;
    desc.texture_sampler_pairs[5].glsl_name = "shadow_tex";

    desc.label = "mesh-shader";
    m_meshShader = sg_make_shader(&desc);
    if (m_meshShader.id == SG_INVALID_ID) {
        return false;
    }

    m_meshPipeline = createMeshPipeline(std::max(1, sapp_sample_count()), "mesh-pipeline");
    if (m_meshPipeline.id == SG_INVALID_ID) {
        return false;
    }

    m_meshPreviewPipeline = createMeshPipeline(1, "mesh-preview-pipeline");
    if (m_meshPreviewPipeline.id == SG_INVALID_ID) {
        return false;
    }

    return true;
}

sg_pipeline Renderer::createMeshPipeline(int sampleCount, const char* label) {
    sg_pipeline_desc pip = {};
    pip.layout.buffers[0].stride = 32;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[0].offset = 0;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].offset = 12;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip.layout.attrs[2].offset = 24;
    pip.shader = m_meshShader;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.cull_mode = SG_CULLMODE_NONE;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.sample_count = sampleCount;
    pip.colors[0].write_mask = SG_COLORMASK_RGBA;
    pip.label = label;
    return sg_make_pipeline(&pip);
}

bool Renderer::initShadowPipeline() {
    sg_shader_desc shd = {};
    shd.vertex_func.source = s_shadowVsSrc;
    shd.vertex_func.entry = "vs_main";
    shd.attrs[0].hlsl_sem_name = "POSITION";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "NORMAL";
    shd.attrs[1].hlsl_sem_index = 0;
    shd.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd.attrs[2].hlsl_sem_index = 0;
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(ShadowUniforms);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    shd.label = "shadow-depth-shader";
    m_shadowShader = sg_make_shader(&shd);
    if (m_shadowShader.id == SG_INVALID_ID) {
        return false;
    }

    sg_pipeline_desc pip = {};
    pip.layout.buffers[0].stride = 32;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[0].offset = 0;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].offset = 12;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip.layout.attrs[2].offset = 24;
    pip.shader = m_shadowShader;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.depth.bias = 0.0005f;
    pip.depth.bias_slope_scale = 1.5f;
    pip.color_count = 0;
    pip.cull_mode = SG_CULLMODE_FRONT;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.sample_count = 1;
    pip.label = "shadow-depth-pipeline";
    m_shadowPipeline = sg_make_pipeline(&pip);
    return m_shadowPipeline.id != SG_INVALID_ID;
}

void Renderer::render() {
}

bool Renderer::initWhiteTexture() {
    uint32_t white = 0xFFFFFFFF;
    sg_image_desc imgDesc = {};
    imgDesc.type = SG_IMAGETYPE_2D;
    imgDesc.width = 1;
    imgDesc.height = 1;
    imgDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imgDesc.data.mip_levels[0].ptr = &white;
    imgDesc.data.mip_levels[0].size = 4;
    imgDesc.label = "white-tex";
    m_whiteImage = sg_make_image(&imgDesc);
    if (m_whiteImage.id == SG_INVALID_ID) return false;

    sg_view_desc vd = {};
    vd.texture.image = m_whiteImage;
    vd.label = "white-tex-view";
    m_whiteView = sg_make_view(&vd);
    if (m_whiteView.id == SG_INVALID_ID) return false;

    sg_sampler_desc sd = {};
    sd.min_filter = SG_FILTER_LINEAR;
    sd.mag_filter = SG_FILTER_LINEAR;
    sd.wrap_u = SG_WRAP_REPEAT;
    sd.wrap_v = SG_WRAP_REPEAT;
    sd.label = "white-tex-sampler";
    m_whiteSampler = sg_make_sampler(&sd);
    if (m_whiteSampler.id == SG_INVALID_ID) return false;

    return true;
}

bool Renderer::initGrid() {
    sg_shader_desc shd = {};
    shd.vertex_func.source = s_gridVsSrc;
    shd.fragment_func.source = s_gridFsSrc;
    shd.vertex_func.entry = "vs_main";
    shd.fragment_func.entry = "ps_main";
    shd.attrs[0].hlsl_sem_name = "POSITION";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "COLOR";
    shd.attrs[1].hlsl_sem_index = 0;
    shd.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd.attrs[2].hlsl_sem_index = 0;
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(GridUniforms);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    shd.label = "grid-shader";
    m_gridShader = sg_make_shader(&shd);
    if (m_gridShader.id == SG_INVALID_ID) return false;

    sg_pipeline_desc pip = {};
    pip.layout.buffers[0].stride = 32; // 3 floats pos + 4 floats color + 1 float follow flag
    pip.layout.attrs[0].buffer_index = 0;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[0].offset = 0;
    pip.layout.attrs[1].buffer_index = 0;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.layout.attrs[1].offset = 12;
    pip.layout.attrs[2].buffer_index = 0;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
    pip.layout.attrs[2].offset = 28;
    pip.shader = m_gridShader;
    pip.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.cull_mode = SG_CULLMODE_NONE;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.sample_count = std::max(1, sapp_sample_count());
    pip.colors[0].write_mask = SG_COLORMASK_RGBA;
    pip.label = "grid-pipeline";
    m_gridPipeline = sg_make_pipeline(&pip);
    if (m_gridPipeline.id == SG_INVALID_ID) return false;

    const float halfSize = 1000.0f;
    const float minorStep = 1.0f;
    const float majorStep = 10.0f;
    const int minorCount = (int)(halfSize * 2.0f / minorStep);
    const int majorCount = (int)(halfSize * 2.0f / majorStep);

    const float aColor[4] = { 0.8f, 0.2f, 0.2f, 0.6f }; // X axis
    const float bColor[4] = { 0.2f, 0.2f, 0.8f, 0.6f }; // Z axis
    const float majorColor[4] = { 0.35f, 0.35f, 0.40f, 0.4f };
    const float minorColor[4] = { 0.20f, 0.20f, 0.22f, 0.25f };

    // minor lines along X: for each Z step from -halfSize to halfSize
    int minorXLines = minorCount; // skip Z boundary lines, those will be major
    int minorZLines = minorCount;
    // major lines: axis + every majorStep (excluding axis)
    int majorXLines = majorCount;
    int majorZLines = majorCount;

    int maxGridVertexCount = (minorXLines + minorZLines + majorXLines + majorZLines + 2 + 2) * 2;
    struct GridVertex { float px, py, pz; float r, g, b, a; float follow; };
    GridVertex* verts = new GridVertex[maxGridVertexCount];
    int idx = 0;

    auto addLine = [&](float x1, float z1, float x2, float z2, const float* col, float followOrigin) {
        verts[idx++] = { x1, 0.0f, z1, col[0], col[1], col[2], col[3], followOrigin };
        verts[idx++] = { x2, 0.0f, z2, col[0], col[1], col[2], col[3], followOrigin };
    };

    // minor lines along X (constant Z, not on major steps)
    for (int i = 1; i < minorCount; i++) {
        float z = -halfSize + i * minorStep;
        float rem = std::abs(std::fmod(z, majorStep));
        if (rem < 0.001f || rem > majorStep - 0.001f) continue;
        addLine(-halfSize, z, halfSize, z, minorColor, 1.0f);
    }

    // minor lines along Z (constant X, not on major steps)
    for (int i = 1; i < minorCount; i++) {
        float x = -halfSize + i * minorStep;
        float rem = std::abs(std::fmod(x, majorStep));
        if (rem < 0.001f || rem > majorStep - 0.001f) continue;
        addLine(x, -halfSize, x, halfSize, minorColor, 1.0f);
    }

    // major lines along X (constant Z, every majorStep)
    for (int i = 0; i <= majorCount; i++) {
        float z = -halfSize + i * majorStep;
        if (std::abs(z) < 0.001f) continue; // axis line drawn separately
        addLine(-halfSize, z, halfSize, z, majorColor, 1.0f);
    }

    // major lines along Z (constant X, every majorStep)
    for (int i = 0; i <= majorCount; i++) {
        float x = -halfSize + i * majorStep;
        if (std::abs(x) < 0.001f) continue; // axis line drawn separately
        addLine(x, -halfSize, x, halfSize, majorColor, 1.0f);
    }

    // X axis (red)
    addLine(-halfSize, 0.0f, halfSize, 0.0f, aColor, 0.0f);
    // Z axis (blue)
    addLine(0.0f, -halfSize, 0.0f, halfSize, bColor, 0.0f);

    m_gridVertexCount = idx;

    sg_buffer_desc buf = {};
    buf.size = m_gridVertexCount * sizeof(GridVertex);
    buf.data.ptr = verts;
    buf.data.size = m_gridVertexCount * sizeof(GridVertex);
    buf.usage.vertex_buffer = true;
    buf.usage.immutable = true;
    buf.label = "grid-vbo";
    m_gridBuffer = sg_make_buffer(buf);
    delete[] verts;
    if (m_gridBuffer.id == SG_INVALID_ID) return false;

    return true;
}

bool Renderer::initSelectionOutline() {
    sg_shader_desc shd = {};
    shd.vertex_func.source = s_outlineVsSrc;
    shd.fragment_func.source = s_outlineFsSrc;
    shd.vertex_func.entry = "vs_main";
    shd.fragment_func.entry = "ps_main";
    shd.attrs[0].hlsl_sem_name = "POSITION";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "COLOR";
    shd.attrs[1].hlsl_sem_index = 0;
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(float) * 16;
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    shd.label = "selection-outline-shader";
    m_outlineShader = sg_make_shader(&shd);
    if (m_outlineShader.id == SG_INVALID_ID) return false;

    sg_pipeline_desc pip = {};
    pip.layout.buffers[0].stride = 28;
    pip.layout.attrs[0].buffer_index = 0;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[0].offset = 0;
    pip.layout.attrs[1].buffer_index = 0;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.layout.attrs[1].offset = 12;
    pip.shader = m_outlineShader;
    pip.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip.depth.write_enabled = false;
    pip.cull_mode = SG_CULLMODE_NONE;
    pip.sample_count = std::max(1, sapp_sample_count());
    pip.colors[0].write_mask = SG_COLORMASK_RGBA;
    pip.label = "selection-outline-pipeline";
    m_outlinePipeline = sg_make_pipeline(&pip);
    if (m_outlinePipeline.id == SG_INVALID_ID) return false;

    struct OutlineVertex { float px, py, pz; float r, g, b, a; };
    sg_buffer_desc buf = {};
    buf.size = sizeof(OutlineVertex) * 24;
    buf.usage.vertex_buffer = true;
    buf.usage.dynamic_update = true;
    buf.label = "selection-outline-vbo";
    m_outlineBuffer = sg_make_buffer(buf);
    return m_outlineBuffer.id != SG_INVALID_ID;
}

void Renderer::drawGrid(const Mat4& view, const Mat4& proj, const Vec3& cameraPosition) {
    if (m_gridPipeline.id == SG_INVALID_ID || m_gridVertexCount == 0) {
        return;
    }

    sg_apply_pipeline(m_gridPipeline);

    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_gridBuffer;
    sg_apply_bindings(&bind);

    Mat4 viewProj = proj * view;
    const float minorStep = 1.0f;
    GridUniforms uniforms = {};
    std::memcpy(uniforms.viewProj, viewProj.m, sizeof(uniforms.viewProj));
    uniforms.origin[0] = std::floor(cameraPosition.x / minorStep) * minorStep;
    uniforms.origin[1] = 0.0f;
    uniforms.origin[2] = std::floor(cameraPosition.z / minorStep) * minorStep;
    uniforms.origin[3] = 0.0f;
    sg_apply_uniforms(0, { &uniforms, sizeof(uniforms) });

    sg_draw(0, m_gridVertexCount, 1);
}

void Renderer::shutdown() {
    shutdownShadowMap();
    shutdownSelectionOutline();
    shutdownGrid();
    shutdownPreview();
    if (m_whiteSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_whiteSampler);
        m_whiteSampler = {};
    }
    if (m_whiteView.id != SG_INVALID_ID) {
        sg_destroy_view(m_whiteView);
        m_whiteView = {};
    }
    if (m_whiteImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_whiteImage);
        m_whiteImage = {};
    }
    if (m_meshPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_meshPipeline);
        m_meshPipeline = {};
    }
    if (m_meshPreviewPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_meshPreviewPipeline);
        m_meshPreviewPipeline = {};
    }
    if (m_shadowPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_shadowPipeline);
        m_shadowPipeline = {};
    }
    if (m_shadowShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shadowShader);
        m_shadowShader = {};
    }
    if (m_meshShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_meshShader);
        m_meshShader = {};
    }
}

void Renderer::shutdownSelectionOutline() {
    if (m_outlineBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_outlineBuffer);
        m_outlineBuffer = {};
    }
    if (m_outlinePipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_outlinePipeline);
        m_outlinePipeline = {};
    }
    if (m_outlineShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_outlineShader);
        m_outlineShader = {};
    }
}

void Renderer::shutdownGrid() {
    if (m_gridBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_gridBuffer);
        m_gridBuffer = {};
    }
    if (m_gridPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_gridPipeline);
        m_gridPipeline = {};
    }
    if (m_gridShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_gridShader);
        m_gridShader = {};
    }
    m_gridVertexCount = 0;
}

void Renderer::drawMesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj) {
    drawMeshWithPipeline(m_meshPipeline, mesh, model, view, proj);
}

void Renderer::drawMeshPreview(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj) {
    drawMeshWithPipeline(m_meshPreviewPipeline, mesh, model, view, proj);
}

void Renderer::drawMeshShadow(const Mesh& mesh, const Mat4& model, const Mat4& lightViewProj) {
    if (m_shadowPipeline.id == SG_INVALID_ID || mesh.indexCount() <= 0) {
        return;
    }

    sg_apply_pipeline(m_shadowPipeline);

    sg_bindings bind = {};
    bind.vertex_buffers[0] = mesh.vertexBuffer();
    bind.index_buffer = mesh.indexBuffer();
    sg_apply_bindings(&bind);

    ShadowUniforms uniforms = {};
    std::memcpy(uniforms.model, model.m, sizeof(float) * 16);
    std::memcpy(uniforms.lightViewProj, lightViewProj.m, sizeof(float) * 16);
    sg_apply_uniforms(0, { &uniforms, sizeof(uniforms) });

    for (const auto& sm : mesh.submeshes()) {
        sg_draw((int)sm.indexOffset, (int)sm.indexCount, 1);
    }
}

void Renderer::drawSelectionOutline(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj) {
    if (m_outlinePipeline.id == SG_INVALID_ID || m_outlineBuffer.id == SG_INVALID_ID || !mesh.bounds().valid) {
        return;
    }

    struct OutlineVertex { float px, py, pz; float r, g, b, a; };
    const MeshBounds& b = mesh.bounds();
    Vec3 corners[8] = {
        { b.min.x, b.min.y, b.min.z },
        { b.max.x, b.min.y, b.min.z },
        { b.max.x, b.max.y, b.min.z },
        { b.min.x, b.max.y, b.min.z },
        { b.min.x, b.min.y, b.max.z },
        { b.max.x, b.min.y, b.max.z },
        { b.max.x, b.max.y, b.max.z },
        { b.min.x, b.max.y, b.max.z }
    };

    for (Vec3& corner : corners) {
        corner = model.transformPoint(corner);
    }

    static constexpr int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };

    OutlineVertex verts[24];
    int idx = 0;
    for (const auto& edge : edges) {
        const Vec3& a = corners[edge[0]];
        const Vec3& c = corners[edge[1]];
        verts[idx++] = { a.x, a.y, a.z, 1.0f, 0.702f, 0.0f, 1.0f };
        verts[idx++] = { c.x, c.y, c.z, 1.0f, 0.702f, 0.0f, 1.0f };
    }

    sg_update_buffer(m_outlineBuffer, { verts, sizeof(verts) });

    sg_apply_pipeline(m_outlinePipeline);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_outlineBuffer;
    sg_apply_bindings(&bind);

    Mat4 viewProj = proj * view;
    sg_apply_uniforms(0, { viewProj.m, sizeof(float) * 16 });
    sg_draw(0, 24, 1);
}

void Renderer::drawMeshWithPipeline(sg_pipeline pipeline, const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj, float alphaMultiplier) {
    if (pipeline.id == SG_INVALID_ID || mesh.indexCount() <= 0) {
        return;
    }

    sg_apply_pipeline(pipeline);

    sg_bindings bind = {};
    bind.vertex_buffers[0] = mesh.vertexBuffer();
    bind.index_buffer = mesh.indexBuffer();

    const auto& materials = mesh.materials();

    for (const auto& sm : mesh.submeshes()) {
        MeshUniforms uniforms;
        std::memcpy(uniforms.model, model.m, sizeof(float) * 16);
        Mat4 viewProj = proj * view;
        std::memcpy(uniforms.view_proj, viewProj.m, sizeof(float) * 16);
        std::memcpy(uniforms.light_view_proj, m_lightViewProj.m, sizeof(float) * 16);

        Vec3 lightDir = m_lighting.direction.normalized();
        uniforms.light_dir_intensity[0] = lightDir.x;
        uniforms.light_dir_intensity[1] = lightDir.y;
        uniforms.light_dir_intensity[2] = lightDir.z;
        uniforms.light_dir_intensity[3] = m_lighting.intensity;
        uniforms.light_color[0] = m_lighting.color.x;
        uniforms.light_color[1] = m_lighting.color.y;
        uniforms.light_color[2] = m_lighting.color.z;
        uniforms.light_color[3] = 1.0f;
        uniforms.ambient_color[0] = m_lighting.ambient;
        uniforms.ambient_color[1] = m_lighting.ambient;
        uniforms.ambient_color[2] = m_lighting.ambient;
        uniforms.ambient_color[3] = 1.0f;
        uniforms.material_flags[0] = 0.0f;
        uniforms.material_flags[1] = 0.0f;
        uniforms.material_flags[2] = 0.0f;
        uniforms.material_flags[3] = 0.0f;
        uniforms.material_params[0] = 0.0f;
        uniforms.material_params[1] = 0.5f;
        uniforms.material_params[2] = m_shadowsEnabled ? 1.0f : 0.0f;
        uniforms.material_params[3] = 0.0f;

        if (sm.materialIndex < materials.size()) {
            const auto& mat = materials[sm.materialIndex];
            std::memcpy(uniforms.base_color, mat.baseColor, sizeof(float) * 4);
            uniforms.base_color[3] *= alphaMultiplier;
            bind.views[0] = mat.hasTexture ? mat.textureView : m_whiteView;
            bind.samplers[0] = mat.hasTexture ? mat.sampler : m_whiteSampler;
            bind.views[1] = mat.hasNormalTexture ? mat.normalTextureView : m_whiteView;
            bind.samplers[1] = mat.hasNormalTexture ? mat.normalSampler : m_whiteSampler;
            bind.views[2] = mat.hasMetallicTexture ? mat.metallicTextureView : m_whiteView;
            bind.samplers[2] = mat.hasMetallicTexture ? mat.metallicSampler : m_whiteSampler;
            bind.views[3] = mat.hasRoughnessTexture ? mat.roughnessTextureView : m_whiteView;
            bind.samplers[3] = mat.hasRoughnessTexture ? mat.roughnessSampler : m_whiteSampler;
            bind.views[4] = mat.hasMetallicRoughnessTexture ? mat.metallicRoughnessTextureView : m_whiteView;
            bind.samplers[4] = mat.hasMetallicRoughnessTexture ? mat.metallicRoughnessSampler : m_whiteSampler;
            bind.views[5] = m_shadowDepthTexView;
            bind.samplers[5] = m_shadowSampler;
            uniforms.material_flags[0] = mat.hasNormalTexture ? 1.0f : 0.0f;
            uniforms.material_flags[1] = mat.hasMetallicTexture ? 1.0f : 0.0f;
            uniforms.material_flags[2] = mat.hasRoughnessTexture ? 1.0f : 0.0f;
            uniforms.material_flags[3] = mat.hasMetallicRoughnessTexture ? 1.0f : 0.0f;
            uniforms.material_params[0] = mat.metallicFactor;
            uniforms.material_params[1] = mat.roughnessFactor;
        } else {
            uniforms.base_color[0] = 0.8f;
            uniforms.base_color[1] = 0.8f;
            uniforms.base_color[2] = 0.8f;
            uniforms.base_color[3] = alphaMultiplier;
            bind.views[0] = m_whiteView;
            bind.samplers[0] = m_whiteSampler;
            bind.views[1] = m_whiteView;
            bind.samplers[1] = m_whiteSampler;
            bind.views[2] = m_whiteView;
            bind.samplers[2] = m_whiteSampler;
            bind.views[3] = m_whiteView;
            bind.samplers[3] = m_whiteSampler;
            bind.views[4] = m_whiteView;
            bind.samplers[4] = m_whiteSampler;
            bind.views[5] = m_shadowDepthTexView;
            bind.samplers[5] = m_shadowSampler;
        }

        sg_apply_uniforms(0, { &uniforms, sizeof(uniforms) });
        sg_apply_bindings(&bind);
        sg_draw((int)sm.indexOffset, (int)sm.indexCount, 1);
    }
}

sg_buffer Renderer::createBuffer(const sg_buffer_desc& desc) {
    return sg_make_buffer(&desc);
}

void Renderer::destroyBuffer(sg_buffer buf) {
    sg_destroy_buffer(buf);
}

void Renderer::updateBuffer(sg_buffer buf, const sg_range& data) {
    sg_update_buffer(buf, &data);
}

sg_image Renderer::createTexture(const sg_image_desc& desc) {
    return sg_make_image(&desc);
}

void Renderer::destroyTexture(sg_image img) {
    sg_destroy_image(img);
}

sg_shader Renderer::createShader(const sg_shader_desc& desc) {
    return sg_make_shader(&desc);
}

sg_pipeline Renderer::createPipeline(const sg_pipeline_desc& desc) {
    return sg_make_pipeline(&desc);
}

void Renderer::applyViewport(int x, int y, int w, int h, bool origin_top_left) {
    sg_apply_viewport(x, y, w, h, origin_top_left);
}

void Renderer::applyPipeline(sg_pipeline pip) {
    sg_apply_pipeline(pip);
}

void Renderer::applyBindings(const sg_bindings& bindings) {
    sg_apply_bindings(&bindings);
}

void Renderer::draw(int base_element, int num_elements, int num_instances) {
    sg_draw(base_element, num_elements, num_instances);
}

// --- Preview (offscreen) support ---

bool Renderer::initPreview(int width, int height) {
    if (m_previewColorImg.id != SG_INVALID_ID) {
        shutdownPreview();
    }
    m_previewWidth = width;
    m_previewHeight = height;

    // color attachment image
    sg_image_desc colorDesc = {};
    colorDesc.type = SG_IMAGETYPE_2D;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    colorDesc.sample_count = 1;
    colorDesc.usage.color_attachment = true;
    colorDesc.label = "preview-color";
    m_previewColorImg = sg_make_image(&colorDesc);
    if (m_previewColorImg.id == SG_INVALID_ID) {
        return false;
    }

    // depth attachment image
    sg_image_desc depthDesc = {};
    depthDesc.type = SG_IMAGETYPE_2D;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depthDesc.sample_count = 1;
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.label = "preview-depth";
    m_previewDepthImg = sg_make_image(&depthDesc);
    if (m_previewDepthImg.id == SG_INVALID_ID) {
        return false;
    }

    // color attachment view
    sg_view_desc cvDesc = {};
    cvDesc.color_attachment.image = m_previewColorImg;
    cvDesc.label = "preview-color-attach-view";
    m_previewColorAttachView = sg_make_view(&cvDesc);
    if (m_previewColorAttachView.id == SG_INVALID_ID) {
        return false;
    }

    // depth attachment view
    sg_view_desc dvDesc = {};
    dvDesc.depth_stencil_attachment.image = m_previewDepthImg;
    dvDesc.label = "preview-depth-attach-view";
    m_previewDepthAttachView = sg_make_view(&dvDesc);
    if (m_previewDepthAttachView.id == SG_INVALID_ID) {
        return false;
    }

    // texture view for ImGui sampling
    sg_view_desc tvDesc = {};
    tvDesc.texture.image = m_previewColorImg;
    tvDesc.label = "preview-tex-view";
    m_previewColorTexView = sg_make_view(&tvDesc);
    if (m_previewColorTexView.id == SG_INVALID_ID) {
        return false;
    }

    // build attachments desc
    m_previewAtts = {};
    m_previewAtts.colors[0] = m_previewColorAttachView;
    m_previewAtts.depth_stencil = m_previewDepthAttachView;

    // cache ImTextureID
    m_previewImTexID = simgui_imtextureid(m_previewColorTexView);

    return true;
}

void Renderer::shutdownPreview() {
    if (m_previewColorTexView.id != SG_INVALID_ID) {
        sg_destroy_view(m_previewColorTexView);
        m_previewColorTexView = {};
    }
    if (m_previewDepthAttachView.id != SG_INVALID_ID) {
        sg_destroy_view(m_previewDepthAttachView);
        m_previewDepthAttachView = {};
    }
    if (m_previewColorAttachView.id != SG_INVALID_ID) {
        sg_destroy_view(m_previewColorAttachView);
        m_previewColorAttachView = {};
    }
    if (m_previewDepthImg.id != SG_INVALID_ID) {
        sg_destroy_image(m_previewDepthImg);
        m_previewDepthImg = {};
    }
    if (m_previewColorImg.id != SG_INVALID_ID) {
        sg_destroy_image(m_previewColorImg);
        m_previewColorImg = {};
    }
    m_previewAtts = {};
    m_previewImTexID = 0;
    m_previewWidth = 0;
    m_previewHeight = 0;
}

void Renderer::beginPreviewPass() {
    sg_pass_action action = {};
    action.colors[0] = {
        .load_action = SG_LOADACTION_CLEAR,
        .clear_value = { 0.1176f, 0.1176f, 0.1176f, 1.0f },
    };
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;

    sg_pass pass = {};
    pass.action = action;
    pass.attachments = m_previewAtts;
    sg_begin_pass(&pass);
}

void Renderer::endPreviewPass() {
    sg_end_pass();
}

bool Renderer::initShadowMap(int size) {
    if (m_shadowDepthImg.id != SG_INVALID_ID) {
        shutdownShadowMap();
    }

    m_shadowMapSize = size;

    sg_image_desc depthDesc = {};
    depthDesc.type = SG_IMAGETYPE_2D;
    depthDesc.width = size;
    depthDesc.height = size;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH;
    depthDesc.sample_count = 1;
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.label = "shadow-depth";
    m_shadowDepthImg = sg_make_image(&depthDesc);
    if (m_shadowDepthImg.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc attachViewDesc = {};
    attachViewDesc.depth_stencil_attachment.image = m_shadowDepthImg;
    attachViewDesc.label = "shadow-depth-attach-view";
    m_shadowDepthAttachView = sg_make_view(&attachViewDesc);
    if (m_shadowDepthAttachView.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc texViewDesc = {};
    texViewDesc.texture.image = m_shadowDepthImg;
    texViewDesc.label = "shadow-depth-tex-view";
    m_shadowDepthTexView = sg_make_view(&texViewDesc);
    if (m_shadowDepthTexView.id == SG_INVALID_ID) {
        return false;
    }

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.compare = SG_COMPAREFUNC_LESS_EQUAL;
    samplerDesc.label = "shadow-sampler";
    m_shadowSampler = sg_make_sampler(&samplerDesc);
    if (m_shadowSampler.id == SG_INVALID_ID) {
        return false;
    }

    m_shadowAtts = {};
    m_shadowAtts.depth_stencil = m_shadowDepthAttachView;
    return true;
}

void Renderer::shutdownShadowMap() {
    if (m_shadowSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_shadowSampler);
        m_shadowSampler = {};
    }
    if (m_shadowDepthTexView.id != SG_INVALID_ID) {
        sg_destroy_view(m_shadowDepthTexView);
        m_shadowDepthTexView = {};
    }
    if (m_shadowDepthAttachView.id != SG_INVALID_ID) {
        sg_destroy_view(m_shadowDepthAttachView);
        m_shadowDepthAttachView = {};
    }
    if (m_shadowDepthImg.id != SG_INVALID_ID) {
        sg_destroy_image(m_shadowDepthImg);
        m_shadowDepthImg = {};
    }
    m_shadowAtts = {};
    m_shadowMapSize = 0;
}

void Renderer::beginShadowPass() {
    sg_pass_action action = {};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;

    sg_pass pass = {};
    pass.action = action;
    pass.attachments = m_shadowAtts;
    sg_begin_pass(&pass);
    sg_apply_viewport(0, 0, m_shadowMapSize, m_shadowMapSize, true);
}

void Renderer::endShadowPass() {
    sg_end_pass();
}
