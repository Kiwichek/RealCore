#pragma once
#include <core/math.h>
#include <sokol_gfx.h>

class Gizmo {
public:
    enum Axis { NONE, X_AXIS, Y_AXIS, Z_AXIS };
    enum Mode { MOVE, ROTATE };

    bool init();
    void shutdown();
    void draw(const Mat4& view, const Mat4& proj, const Vec3& position);
    void drawCameraFrustum(const Mat4& view, const Mat4& proj, const Mat4& cameraWorld, float fovY, float aspect, float nearPlane, float farPlane);
    void drawLightDirection(const Mat4& view, const Mat4& proj, const Vec3& position, const Vec3& lightDirection);

    Axis pick(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& position);
    bool isHovered() const { return m_hoverAxis != NONE; }

    void beginDrag(Axis axis, const Vec3& position, const Vec3& rayOrigin, const Vec3& rayDir);
    Vec3 drag(const Vec3& rayOrigin, const Vec3& rayDir);
    float dragRotation(const Vec3& rayOrigin, const Vec3& rayDir);
    void endDrag();

    Axis activeAxis() const { return m_activeAxis; }
    Axis hoverAxis() const { return m_hoverAxis; }
    void setHoverAxis(Axis a) { m_hoverAxis = a; }
    Mode activeMode() const { return m_activeMode; }
    Mode hoverMode() const { return m_hoverMode; }
    void setHover(Mode mode, Axis axis) { m_hoverMode = mode; m_hoverAxis = axis; }

    float arrowLength() const { return m_arrowLength; }
    float arrowHeadLength() const { return m_headLength; }

private:
    struct ArrowMesh {
        sg_buffer vertexBuffer;
        sg_buffer indexBuffer;
        int indexCount;
    };

    void generateArrow(ArrowMesh& mesh, const float* color,
                       float ax, float ay, float az,
                       float bx, float by, float bz);
    void generateRotationRing(ArrowMesh& mesh, Axis axis, const float* color);

    ArrowMesh m_arrows[3];
    ArrowMesh m_rotationRings[3];
    sg_shader m_shader = {};
    sg_pipeline m_pipeline = {};
    sg_pipeline m_linePipeline = {};
    sg_buffer m_lineBuffer = {};

    static constexpr float m_shaftRadius = 0.025f;
    static constexpr float m_shaftLength = 0.8f;
    static constexpr float m_headRadius = 0.07f;
    static constexpr float m_headLength = 0.2f;
    static constexpr float m_arrowLength = 1.0f;
    static constexpr float m_rotationRadius = 1.25f;
    static constexpr int m_segments = 16;
    static constexpr int m_maxLineVertices = 512;

    Axis m_hoverAxis = NONE;
    Axis m_activeAxis = NONE;
    Mode m_hoverMode = MOVE;
    Mode m_activeMode = MOVE;
    Vec3 m_origin;
    Vec3 m_axisDir;
    float m_dragOffsetS = 0.0f;
    float m_startAngle = 0.0f;
};
