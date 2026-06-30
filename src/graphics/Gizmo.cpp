#include <graphics/Gizmo.h>
#include <sokol_app.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>

static const char* s_gizmoVsSrc = R"(
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
    float4 color_mult;
};

vs_out vs_main(vs_in input) {
    vs_out output;
    output.pos = mul(view_proj, float4(input.pos, 1.0));
    output.color = input.color * color_mult;
    return output;
}
)";

static const char* s_gizmoFsSrc = R"(
struct ps_in {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 ps_main(ps_in input) : SV_TARGET {
    return input.color;
}
)";

struct GizmoLineVert { float px, py, pz; float r, g, b, a; };

static Vec3 axisDirection(Gizmo::Axis axis) {
    switch (axis) {
        case Gizmo::X_AXIS: return { 1, 0, 0 };
        case Gizmo::Y_AXIS: return { 0, 1, 0 };
        case Gizmo::Z_AXIS: return { 0, 0, 1 };
        default: return { 0, 0, 0 };
    }
}

static void basisForAxis(Gizmo::Axis axis, Vec3& u, Vec3& v) {
    if (axis == Gizmo::X_AXIS) {
        u = { 0, 1, 0 };
        v = { 0, 0, 1 };
    } else if (axis == Gizmo::Y_AXIS) {
        u = { 1, 0, 0 };
        v = { 0, 0, 1 };
    } else {
        u = { 1, 0, 0 };
        v = { 0, 1, 0 };
    }
}

static bool rayPlaneIntersection(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& planePoint, const Vec3& planeNormal, Vec3& outPoint) {
    float denom = planeNormal.dot(rayDir);
    if (std::abs(denom) < 0.00001f) {
        return false;
    }

    float t = (planePoint - rayOrigin).dot(planeNormal) / denom;
    if (t < 0.0f) {
        return false;
    }

    outPoint = rayOrigin + rayDir * t;
    return true;
}

static void addLine(std::vector<GizmoLineVert>& verts, const Vec3& a, const Vec3& b, const float* color) {
    verts.push_back({ a.x, a.y, a.z, color[0], color[1], color[2], color[3] });
    verts.push_back({ b.x, b.y, b.z, color[0], color[1], color[2], color[3] });
}

static Vec3 transformDirection(const Mat4& m, const Vec3& d) {
    Vec3 origin = m.transformPoint({ 0.0f, 0.0f, 0.0f });
    Vec3 point = m.transformPoint(d);
    return (point - origin).normalized();
}

bool Gizmo::init() {
    sg_shader_desc shd = {};
    shd.vertex_func.source = s_gizmoVsSrc;
    shd.fragment_func.source = s_gizmoFsSrc;
    shd.vertex_func.entry = "vs_main";
    shd.fragment_func.entry = "ps_main";
    shd.attrs[0].hlsl_sem_name = "POSITION";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "COLOR";
    shd.attrs[1].hlsl_sem_index = 0;
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(float) * 20; // 16 mvp + 4 color_mult
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    shd.label = "gizmo-shader";
    m_shader = sg_make_shader(&shd);
    if (m_shader.id == SG_INVALID_ID) return false;

    sg_pipeline_desc pip = {};
    pip.layout.buffers[0].stride = 28;
    pip.layout.attrs[0].buffer_index = 0;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[0].offset = 0;
    pip.layout.attrs[1].buffer_index = 0;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.layout.attrs[1].offset = 12;
    pip.shader = m_shader;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.cull_mode = SG_CULLMODE_NONE;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.sample_count = std::max(1, sapp_sample_count());
    pip.colors[0].write_mask = SG_COLORMASK_RGBA;
    pip.label = "gizmo-pipeline";
    m_pipeline = sg_make_pipeline(&pip);
    if (m_pipeline.id == SG_INVALID_ID) return false;

    sg_pipeline_desc linePip = pip;
    linePip.primitive_type = SG_PRIMITIVETYPE_LINES;
    linePip.depth.compare = SG_COMPAREFUNC_ALWAYS;
    linePip.depth.write_enabled = false;
    linePip.label = "editor-gizmo-line-pipeline";
    m_linePipeline = sg_make_pipeline(&linePip);
    if (m_linePipeline.id == SG_INVALID_ID) return false;

    sg_buffer_desc lineBuf = {};
    lineBuf.size = sizeof(GizmoLineVert) * m_maxLineVertices;
    lineBuf.usage.vertex_buffer = true;
    lineBuf.usage.dynamic_update = true;
    lineBuf.label = "editor-gizmo-line-vbo";
    m_lineBuffer = sg_make_buffer(lineBuf);
    if (m_lineBuffer.id == SG_INVALID_ID) return false;

    float colors[3][4] = {
        { 0.9f, 0.15f, 0.15f, 1.0f }, // X - red
        { 0.15f, 0.9f, 0.15f, 1.0f }, // Y - green
        { 0.15f, 0.15f, 0.9f, 1.0f }, // Z - blue
    };

    generateArrow(m_arrows[0], colors[0], 1,0,0, 0,0,0);
    generateArrow(m_arrows[1], colors[1], 0,1,0, 0,0,0);
    generateArrow(m_arrows[2], colors[2], 0,0,1, 0,0,0);
    generateRotationRing(m_rotationRings[0], X_AXIS, colors[0]);
    generateRotationRing(m_rotationRings[1], Y_AXIS, colors[1]);
    generateRotationRing(m_rotationRings[2], Z_AXIS, colors[2]);

    return true;
}

void Gizmo::generateArrow(ArrowMesh& mesh, const float* color,
                          float ax, float ay, float az,
                          float bx, float by, float bz)
{
    struct Vert { float px, py, pz; float r, g, b, a; };

    float dx = ax - bx, dy = ay - by, dz = az - bz;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) { mesh = {}; return; }
    float invLen = 1.0f / len;
    dx *= invLen; dy *= invLen; dz *= invLen;

    float shaftLen = m_shaftLength;
    float headLen = m_headLength;
    float shaftR = m_shaftRadius;
    float headR = m_headRadius;
    int segs = m_segments;

    struct TempVert { float x, y, z; };
    std::vector<TempVert> ringVec(segs * 2);
    TempVert* ring = ringVec.data();

    float upx = 0, upy = 1, upz = 0;
    if (std::abs(dy) > 0.99f) { upx = 1; upy = 0; upz = 0; }

    float rx = dy * upz - dz * upy;
    float ry = dz * upx - dx * upz;
    float rz = dx * upy - dy * upx;
    float rl = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (rl > 0.0001f) { float ir = 1.0f / rl; rx *= ir; ry *= ir; rz *= ir; }

    float ux = ry * dz - rz * dy;
    float uy = rz * dx - rx * dz;
    float uz = rx * dy - ry * dx;

    auto buildRing = [&](TempVert* out, float radius, float along) {
        for (int i = 0; i < segs; i++) {
            float a = (float)i / (float)segs * 2.0f * 3.14159265f;
            float ca = std::cos(a), sa = std::sin(a);
            float px = (rx * ca + ux * sa) * radius;
            float py = (ry * ca + uy * sa) * radius;
            float pz = (rz * ca + uz * sa) * radius;
            out[i] = { bx + dx * along + px, by + dy * along + py, bz + dz * along + pz };
        }
    };

    int cylVerts = segs * 6;
    int coneVerts = segs * 3;
    int totalVerts = cylVerts + coneVerts;
    std::vector<Vert> vertsVec(totalVerts);
    Vert* verts = vertsVec.data();
    int idx = 0;

    // Cylinder shaft
    buildRing(ring, shaftR, 0.0f);
    buildRing(ring + segs, shaftR, shaftLen);

    for (int i = 0; i < segs; i++) {
        int ni = (i + 1) % segs;
        // two triangles per quad
        verts[idx++] = { ring[i].x, ring[i].y, ring[i].z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { ring[ni].x, ring[ni].y, ring[ni].z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { ring[segs + i].x, ring[segs + i].y, ring[segs + i].z, color[0], color[1], color[2], color[3] };

        verts[idx++] = { ring[ni].x, ring[ni].y, ring[ni].z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { ring[segs + ni].x, ring[segs + ni].y, ring[segs + ni].z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { ring[segs + i].x, ring[segs + i].y, ring[segs + i].z, color[0], color[1], color[2], color[3] };
    }

    // Cone head
    buildRing(ring, headR, shaftLen);
    float tipX = bx + dx * (shaftLen + headLen);
    float tipY = by + dy * (shaftLen + headLen);
    float tipZ = bz + dz * (shaftLen + headLen);

    // Brighten color for the head
    float hc[4] = {
        std::min(color[0] * 1.3f, 1.0f),
        std::min(color[1] * 1.3f, 1.0f),
        std::min(color[2] * 1.3f, 1.0f),
        1.0f
    };

    for (int i = 0; i < segs; i++) {
        int ni = (i + 1) % segs;
        verts[idx++] = { tipX, tipY, tipZ, hc[0], hc[1], hc[2], hc[3] };
        verts[idx++] = { ring[i].x, ring[i].y, ring[i].z, hc[0], hc[1], hc[2], hc[3] };
        verts[idx++] = { ring[ni].x, ring[ni].y, ring[ni].z, hc[0], hc[1], hc[2], hc[3] };
    }

    sg_buffer_desc buf = {};
    buf.size = totalVerts * sizeof(Vert);
    buf.data.ptr = verts;
    buf.data.size = totalVerts * sizeof(Vert);
    buf.usage.vertex_buffer = true;
    buf.usage.immutable = true;
    buf.label = "gizmo-arrow-vbo";
    mesh.vertexBuffer = sg_make_buffer(buf);
    mesh.indexBuffer.id = SG_INVALID_ID;
    mesh.indexCount = totalVerts;
}

void Gizmo::generateRotationRing(ArrowMesh& mesh, Axis axis, const float* color) {
    constexpr int ringSegments = 96;
    constexpr float thickness = 0.025f;
    std::vector<GizmoLineVert> verts(ringSegments * 6);
    Vec3 u, v;
    basisForAxis(axis, u, v);

    int idx = 0;
    for (int i = 0; i < ringSegments; i++) {
        float a0 = (float)i / (float)ringSegments * 2.0f * 3.14159265f;
        float a1 = (float)(i + 1) / (float)ringSegments * 2.0f * 3.14159265f;
        Vec3 d0 = (u * std::cos(a0) + v * std::sin(a0));
        Vec3 d1 = (u * std::cos(a1) + v * std::sin(a1));
        Vec3 p0i = d0 * (m_rotationRadius - thickness);
        Vec3 p0o = d0 * (m_rotationRadius + thickness);
        Vec3 p1i = d1 * (m_rotationRadius - thickness);
        Vec3 p1o = d1 * (m_rotationRadius + thickness);

        verts[idx++] = { p0i.x, p0i.y, p0i.z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { p0o.x, p0o.y, p0o.z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { p1o.x, p1o.y, p1o.z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { p0i.x, p0i.y, p0i.z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { p1o.x, p1o.y, p1o.z, color[0], color[1], color[2], color[3] };
        verts[idx++] = { p1i.x, p1i.y, p1i.z, color[0], color[1], color[2], color[3] };
    }

    sg_buffer_desc buf = {};
    buf.size = verts.size() * sizeof(GizmoLineVert);
    buf.data.ptr = verts.data();
    buf.data.size = verts.size() * sizeof(GizmoLineVert);
    buf.usage.vertex_buffer = true;
    buf.usage.immutable = true;
    buf.label = "gizmo-rotation-ring-vbo";
    mesh.vertexBuffer = sg_make_buffer(buf);
    mesh.indexBuffer.id = SG_INVALID_ID;
    mesh.indexCount = (int)verts.size();
}

void Gizmo::shutdown() {
    for (int i = 0; i < 3; i++) {
        if (m_arrows[i].vertexBuffer.id != SG_INVALID_ID) {
            sg_destroy_buffer(m_arrows[i].vertexBuffer);
            m_arrows[i].vertexBuffer = {};
        }
        m_arrows[i].indexBuffer = {};
        m_arrows[i].indexCount = 0;
        if (m_rotationRings[i].vertexBuffer.id != SG_INVALID_ID) {
            sg_destroy_buffer(m_rotationRings[i].vertexBuffer);
            m_rotationRings[i].vertexBuffer = {};
        }
        m_rotationRings[i].indexBuffer = {};
        m_rotationRings[i].indexCount = 0;
    }
    if (m_pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_pipeline);
        m_pipeline = {};
    }
    if (m_linePipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_linePipeline);
        m_linePipeline = {};
    }
    if (m_lineBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_lineBuffer);
        m_lineBuffer = {};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {};
    }
    m_activeAxis = NONE;
    m_hoverAxis = NONE;
}

void Gizmo::draw(const Mat4& view, const Mat4& proj, const Vec3& position) {
    if (m_pipeline.id == SG_INVALID_ID) return;

    Mat4 model = Mat4::translate(position);
    Mat4 mvp = proj * view * model;

    for (int i = 0; i < 3; i++) {
        if (m_arrows[i].indexCount == 0) continue;

        Axis axis = (Axis)(X_AXIS + i);
        bool hovered = (axis == m_hoverAxis);
        float mult = hovered ? 1.8f : 1.0f;

        float uniformData[20];
        std::memcpy(uniformData, mvp.m, sizeof(float) * 16);
        uniformData[16] = mult;
        uniformData[17] = mult;
        uniformData[18] = mult;
        uniformData[19] = 1.0f;

        sg_apply_pipeline(m_pipeline);
        sg_bindings bind = {};
        bind.vertex_buffers[0] = m_arrows[i].vertexBuffer;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, { uniformData, sizeof(uniformData) });
        sg_draw(0, m_arrows[i].indexCount, 1);
    }

    for (int axisIndex = 0; axisIndex < 3; axisIndex++) {
        Axis axis = (Axis)(X_AXIS + axisIndex);
        bool hovered = (axis == m_hoverAxis && m_hoverMode == ROTATE);
        float mult = hovered ? 1.8f : 1.0f;

        float uniformData[20];
        Mat4 ringMvp = proj * view * model;
        std::memcpy(uniformData, ringMvp.m, sizeof(float) * 16);
        uniformData[16] = mult;
        uniformData[17] = mult;
        uniformData[18] = mult;
        uniformData[19] = 1.0f;

        sg_apply_pipeline(m_pipeline);
        sg_bindings bind = {};
        bind.vertex_buffers[0] = m_rotationRings[axisIndex].vertexBuffer;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, { uniformData, sizeof(uniformData) });
        sg_draw(0, m_rotationRings[axisIndex].indexCount, 1);
    }
}

void Gizmo::drawCameraFrustum(const Mat4& view, const Mat4& proj, const Mat4& cameraWorld, float fovY, float aspect, float nearPlane, float farPlane) {
    if (m_linePipeline.id == SG_INVALID_ID || m_lineBuffer.id == SG_INVALID_ID) return;

    float displayFar = std::min(std::max(farPlane * 0.02f, 2.5f), 6.0f);
    float displayNear = std::min(std::max(nearPlane, 0.08f), displayFar * 0.2f);
    float tanHalf = std::tan(fovY * 0.5f);

    Vec3 pos = cameraWorld.transformPoint({ 0.0f, 0.0f, 0.0f });
    Vec3 right = transformDirection(cameraWorld, { 1.0f, 0.0f, 0.0f });
    Vec3 up = transformDirection(cameraWorld, { 0.0f, 1.0f, 0.0f });
    Vec3 forward = transformDirection(cameraWorld, { 0.0f, 0.0f, 1.0f });

    float nh = displayNear * tanHalf;
    float nw = nh * aspect;
    float fh = displayFar * tanHalf;
    float fw = fh * aspect;

    Vec3 nc = pos + forward * displayNear;
    Vec3 fc = pos + forward * displayFar;

    Vec3 ntl = nc + up * nh - right * nw;
    Vec3 ntr = nc + up * nh + right * nw;
    Vec3 nbl = nc - up * nh - right * nw;
    Vec3 nbr = nc - up * nh + right * nw;
    Vec3 ftl = fc + up * fh - right * fw;
    Vec3 ftr = fc + up * fh + right * fw;
    Vec3 fbl = fc - up * fh - right * fw;
    Vec3 fbr = fc - up * fh + right * fw;

    const float frustumColor[4] = { 0.25f, 0.70f, 1.0f, 1.0f };
    const float bodyColor[4] = { 0.85f, 0.92f, 1.0f, 1.0f };
    const float forwardColor[4] = { 0.1f, 0.95f, 1.0f, 1.0f };

    std::vector<GizmoLineVert> verts;
    verts.reserve(64);

    addLine(verts, ntl, ntr, frustumColor);
    addLine(verts, ntr, nbr, frustumColor);
    addLine(verts, nbr, nbl, frustumColor);
    addLine(verts, nbl, ntl, frustumColor);
    addLine(verts, ftl, ftr, frustumColor);
    addLine(verts, ftr, fbr, frustumColor);
    addLine(verts, fbr, fbl, frustumColor);
    addLine(verts, fbl, ftl, frustumColor);
    addLine(verts, ntl, ftl, frustumColor);
    addLine(verts, ntr, ftr, frustumColor);
    addLine(verts, nbl, fbl, frustumColor);
    addLine(verts, nbr, fbr, frustumColor);

    float bodyW = 0.32f;
    float bodyH = 0.20f;
    float bodyD = 0.22f;
    Vec3 b0 = pos - right * bodyW + up * bodyH - forward * bodyD;
    Vec3 b1 = pos + right * bodyW + up * bodyH - forward * bodyD;
    Vec3 b2 = pos + right * bodyW - up * bodyH - forward * bodyD;
    Vec3 b3 = pos - right * bodyW - up * bodyH - forward * bodyD;
    Vec3 lens = pos + forward * 0.35f;
    addLine(verts, b0, b1, bodyColor);
    addLine(verts, b1, b2, bodyColor);
    addLine(verts, b2, b3, bodyColor);
    addLine(verts, b3, b0, bodyColor);
    addLine(verts, b0, lens, bodyColor);
    addLine(verts, b1, lens, bodyColor);
    addLine(verts, b2, lens, bodyColor);
    addLine(verts, b3, lens, bodyColor);
    addLine(verts, pos, fc, forwardColor);

    sg_update_buffer(m_lineBuffer, { verts.data(), verts.size() * sizeof(GizmoLineVert) });

    float uniformData[20];
    Mat4 viewProj = proj * view;
    std::memcpy(uniformData, viewProj.m, sizeof(float) * 16);
    uniformData[16] = 1.0f;
    uniformData[17] = 1.0f;
    uniformData[18] = 1.0f;
    uniformData[19] = 1.0f;

    sg_apply_pipeline(m_linePipeline);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_lineBuffer;
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, { uniformData, sizeof(uniformData) });
    sg_draw(0, (int)verts.size(), 1);
}

void Gizmo::drawLightDirection(const Mat4& view, const Mat4& proj, const Vec3& position, const Vec3& lightDirection) {
    if (m_linePipeline.id == SG_INVALID_ID || m_lineBuffer.id == SG_INVALID_ID) return;

    Vec3 rayDir = (lightDirection * -1.0f).normalized();
    if (rayDir.length() <= 0.0001f) {
        rayDir = { -0.45f, -0.85f, -0.30f };
    }

    Vec3 up = std::abs(rayDir.y) > 0.95f ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ 0.0f, 1.0f, 0.0f };
    Vec3 right = rayDir.cross(up).normalized();
    Vec3 sideUp = right.cross(rayDir).normalized();

    float length = 5.0f;
    Vec3 end = position + rayDir * length;
    float head = 0.45f;
    float spread = 0.20f;

    const float rayColor[4] = { 1.0f, 0.82f, 0.15f, 1.0f };
    const float bulbColor[4] = { 1.0f, 0.95f, 0.45f, 1.0f };

    std::vector<GizmoLineVert> verts;
    verts.reserve(48);
    addLine(verts, position, end, rayColor);
    addLine(verts, end, end - rayDir * head + right * spread, rayColor);
    addLine(verts, end, end - rayDir * head - right * spread, rayColor);
    addLine(verts, end, end - rayDir * head + sideUp * spread, rayColor);
    addLine(verts, end, end - rayDir * head - sideUp * spread, rayColor);

    float r = 0.24f;
    addLine(verts, position - right * r, position + right * r, bulbColor);
    addLine(verts, position - sideUp * r, position + sideUp * r, bulbColor);
    addLine(verts, position - rayDir * r, position + rayDir * r, bulbColor);

    Vec3 p1 = position + right * r;
    Vec3 p2 = position + sideUp * r;
    Vec3 p3 = position - right * r;
    Vec3 p4 = position - sideUp * r;
    addLine(verts, p1, p2, bulbColor);
    addLine(verts, p2, p3, bulbColor);
    addLine(verts, p3, p4, bulbColor);
    addLine(verts, p4, p1, bulbColor);

    sg_update_buffer(m_lineBuffer, { verts.data(), verts.size() * sizeof(GizmoLineVert) });

    float uniformData[20];
    Mat4 viewProj = proj * view;
    std::memcpy(uniformData, viewProj.m, sizeof(float) * 16);
    uniformData[16] = 1.0f;
    uniformData[17] = 1.0f;
    uniformData[18] = 1.0f;
    uniformData[19] = 1.0f;

    sg_apply_pipeline(m_linePipeline);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_lineBuffer;
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, { uniformData, sizeof(uniformData) });
    sg_draw(0, (int)verts.size(), 1);
}

Gizmo::Axis Gizmo::pick(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& position) {
    Vec3 D = rayDir.normalized();
    float bestDist = 20.0f;
    Axis bestAxis = NONE;
    m_hoverMode = MOVE;

    for (int axisIndex = 0; axisIndex < 3; axisIndex++) {
        Axis axis = (Axis)(X_AXIS + axisIndex);
        Vec3 normal = axisDirection(axis);
        Vec3 hit;
        if (!rayPlaneIntersection(rayOrigin, D, position, normal, hit)) {
            continue;
        }

        float radius = (hit - position).length();
        float distToRing = std::abs(radius - m_rotationRadius);
        if (distToRing < 0.08f) {
            float distFromEye = (hit - rayOrigin).length();
            if (distFromEye < bestDist) {
                bestDist = distFromEye;
                bestAxis = axis;
                m_hoverMode = ROTATE;
            }
        }
    }

    static const Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };

    for (int i = 0; i < 3; i++) {
        Vec3 A = position;
        Vec3 B = position + dirs[i] * m_arrowLength;

        // ray vs line segment closest point
        Vec3 AB = B - A;
        Vec3 AO = rayOrigin - A;

        float abDot = AB.dot(AB);
        float dDotAB = D.dot(AB);
        float aoDotAB = AO.dot(AB);
        float dDotD = D.dot(D);
        float aoDotD = AO.dot(D);

        float denom = abDot * dDotD - dDotAB * dDotAB;
        if (std::abs(denom) < 0.000001f) continue;

        float s = (aoDotAB * dDotD - aoDotD * dDotAB) / denom;
        float t = (aoDotAB * dDotAB - aoDotD * abDot) / denom;

        if (s < -0.1f || s > 1.1f) continue;

        Vec3 closestRay = rayOrigin + D * t;
        Vec3 closestAxis = A + AB * s;
        float dist = (closestRay - closestAxis).length();

        float threshold = 0.08f + (s > m_shaftLength / m_arrowLength ? 0.06f : 0.0f);
        if (dist < threshold && t > 0) {
            float distFromEye = (closestRay - rayOrigin).length();
            if (distFromEye < bestDist) {
                bestDist = distFromEye;
                bestAxis = (Axis)(X_AXIS + i);
                m_hoverMode = MOVE;
            }
        }
    }

    return bestAxis;
}

float closestPointParam(const Vec3& P, const Vec3& A, const Vec3& O, const Vec3& D) {
    Vec3 delta = O - P;
    float aDotA = A.dot(A);
    float aDotD = A.dot(D);
    float dDotD = D.dot(D);
    float deltaDotA = delta.dot(A);
    float deltaDotD = delta.dot(D);

    float denom = aDotA * dDotD - aDotD * aDotD;
    if (std::abs(denom) < 0.0001f) return 0.0f;

    return (deltaDotA * dDotD - aDotD * deltaDotD) / denom;
}

void Gizmo::beginDrag(Axis axis, const Vec3& position, const Vec3& rayOrigin, const Vec3& rayDir) {
    m_activeAxis = axis;
    m_activeMode = m_hoverMode;
    m_origin = position;
    static const Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    int idx = (int)axis - (int)X_AXIS;
    m_axisDir = dirs[idx];
    if (m_activeMode == ROTATE) {
        Vec3 hit;
        if (rayPlaneIntersection(rayOrigin, rayDir.normalized(), position, m_axisDir, hit)) {
            Vec3 u, v;
            basisForAxis(axis, u, v);
            Vec3 r = (hit - position).normalized();
            m_startAngle = std::atan2(r.dot(v), r.dot(u));
        } else {
            m_startAngle = 0.0f;
        }
    } else {
        m_dragOffsetS = closestPointParam(position, m_axisDir, rayOrigin, rayDir.normalized());
    }
}

Vec3 Gizmo::drag(const Vec3& rayOrigin, const Vec3& rayDir) {
    Vec3 D = rayDir.normalized();
    float s = closestPointParam(m_origin, m_axisDir, rayOrigin, D);
    return m_origin + m_axisDir * (s - m_dragOffsetS);
}

float Gizmo::dragRotation(const Vec3& rayOrigin, const Vec3& rayDir) {
    if (m_activeAxis == NONE || m_activeMode != ROTATE) {
        return 0.0f;
    }

    Vec3 hit;
    if (!rayPlaneIntersection(rayOrigin, rayDir.normalized(), m_origin, m_axisDir, hit)) {
        return 0.0f;
    }

    Vec3 u, v;
    basisForAxis(m_activeAxis, u, v);
    Vec3 r = (hit - m_origin).normalized();
    float angle = std::atan2(r.dot(v), r.dot(u));
    return angle - m_startAngle;
}

void Gizmo::endDrag() {
    m_activeAxis = NONE;
}
