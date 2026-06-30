#pragma once
#include <sokol_gfx.h>
#include <core/math.h>
#include <cstdint>

struct MeshUniforms {
    float model[16];
    float view_proj[16];
    float light_view_proj[16];
    float base_color[4];
    float light_dir_intensity[4];
    float light_color[4];
    float ambient_color[4];
    float material_flags[4];
    float material_params[4];
};

struct SceneLighting {
    Vec3 direction{ 0.5f, 0.8f, 0.6f };
    Vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float ambient = 0.3f;
};

class Mesh;

class Renderer {
public:
    bool init();
    void render();
    void shutdown();

    const sg_pass_action& passAction() const { return m_passAction; }

    sg_buffer createBuffer(const sg_buffer_desc& desc);
    void destroyBuffer(sg_buffer buf);
    void updateBuffer(sg_buffer buf, const sg_range& data);

    sg_image createTexture(const sg_image_desc& desc);
    void destroyTexture(sg_image img);

    sg_shader createShader(const sg_shader_desc& desc);
    sg_pipeline createPipeline(const sg_pipeline_desc& desc);

    void applyViewport(int x, int y, int w, int h, bool origin_top_left);
    void applyPipeline(sg_pipeline pip);
    void applyBindings(const sg_bindings& bindings);
    void draw(int base_element, int num_elements, int num_instances);

    void drawMesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj);
    void drawMeshPreview(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj);
    void drawMeshShadow(const Mesh& mesh, const Mat4& model, const Mat4& lightViewProj);
    void drawSelectionOutline(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj);
    void setLighting(const SceneLighting& lighting) { m_lighting = lighting; }
    void setLightViewProj(const Mat4& lightViewProj) { m_lightViewProj = lightViewProj; }
    void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }

    // grid rendering
    bool initGrid();
    void drawGrid(const Mat4& view, const Mat4& proj, const Vec3& cameraPosition);
    void shutdownGrid();

    // preview (offscreen) support
    bool initPreview(int width, int height);
    void shutdownPreview();
    void beginPreviewPass();
    void endPreviewPass();
    int previewWidth() const { return m_previewWidth; }
    int previewHeight() const { return m_previewHeight; }
    uint64_t previewImTextureID() const { return m_previewImTexID; }

    bool initShadowMap(int size = 1024);
    void shutdownShadowMap();
    void beginShadowPass();
    void endShadowPass();
    int shadowMapSize() const { return m_shadowMapSize; }

private:
    bool initShaders();
    bool initWhiteTexture();
    bool initSelectionOutline();
    bool initShadowPipeline();
    void shutdownSelectionOutline();
    sg_pipeline createMeshPipeline(int sampleCount, const char* label);
    void drawMeshWithPipeline(sg_pipeline pipeline, const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj, float alphaMultiplier = 1.0f);

    sg_pass_action m_passAction;
    sg_shader m_meshShader = {};
    sg_pipeline m_meshPipeline = {};
    sg_pipeline m_meshPreviewPipeline = {};
    sg_shader m_shadowShader = {};
    sg_pipeline m_shadowPipeline = {};
    sg_shader m_outlineShader = {};
    sg_pipeline m_outlinePipeline = {};
    sg_buffer m_outlineBuffer = {};
    SceneLighting m_lighting;
    Mat4 m_lightViewProj;
    bool m_shadowsEnabled = false;

    // grid resources
    sg_shader m_gridShader = {};
    sg_pipeline m_gridPipeline = {};
    sg_buffer m_gridBuffer = {};
    int m_gridVertexCount = 0;

    // white fallback texture for untextured materials
    sg_image m_whiteImage = {};
    sg_view m_whiteView = {};
    sg_sampler m_whiteSampler = {};

    // preview resources
    sg_image m_previewColorImg = {};
    sg_image m_previewDepthImg = {};
    sg_view m_previewColorAttachView = {};
    sg_view m_previewDepthAttachView = {};
    sg_view m_previewColorTexView = {};
    sg_attachments m_previewAtts = {};
    int m_previewWidth = 0;
    int m_previewHeight = 0;
    uint64_t m_previewImTexID = 0;

    sg_image m_shadowDepthImg = {};
    sg_view m_shadowDepthAttachView = {};
    sg_view m_shadowDepthTexView = {};
    sg_sampler m_shadowSampler = {};
    sg_attachments m_shadowAtts = {};
    int m_shadowMapSize = 0;
};
