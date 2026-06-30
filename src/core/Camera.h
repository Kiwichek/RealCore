#pragma once
#include <core/math.h>

class Camera {
public:
    Camera();

    void setPerspective(float fov, float aspect, float znear, float zfar);

    void update(float deltaTime);

    void onKeyDown(uint32_t key);
    void onKeyUp(uint32_t key);
    void onMouseMove(float dx, float dy);
    void onMouseScroll(float dy);

    void captureMouse(bool captured);
    bool isMouseCaptured() const { return m_mouseCaptured; }

    const Mat4& viewMatrix() const { return m_view; }
    const Mat4& projMatrix() const { return m_proj; }
    Vec3 position() const { return m_position; }
    float fovY() const { return m_fovY; }
    float zNear() const { return m_zNear; }
    float zFar() const { return m_zFar; }

    void lookAt(const Vec3& target);
    void focusOn(const Vec3& target, float radius);

    float moveSpeed = 5.0f;
    float sprintMultiplier = 4.0f;
    float sensitivity = 0.002f;
    float scrollSensitivity = 0.5f;

private:
    Vec3 forwardVector() const;

    Vec3 m_position{ 0, 0, -5 };
    float m_yaw = 0;
    float m_pitch = 0;
    float m_fovY = 60.0f * 3.14159f / 180.0f;
    float m_zNear = 0.01f;
    float m_zFar = 1000.0f;

    Mat4 m_view;
    Mat4 m_proj;

    float m_mouseDx = 0;
    float m_mouseDy = 0;
    bool m_mouseCaptured = false;

    bool m_forward = false;
    bool m_backward = false;
    bool m_left = false;
    bool m_right = false;
    bool m_up = false;
    bool m_down = false;
    bool m_sprint = false;
};
