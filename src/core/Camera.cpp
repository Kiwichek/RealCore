#include <core/Camera.h>
#include <sokol_app.h>
#include <algorithm>

Camera::Camera() {
    setPerspective(m_fovY, 1280.0f / 720.0f, m_zNear, m_zFar);
}

void Camera::setPerspective(float fov, float aspect, float znear, float zfar) {
    m_fovY = fov;
    m_zNear = znear;
    m_zFar = zfar;
    m_proj = Mat4::perspective(fov, aspect, znear, zfar);
}

Vec3 Camera::forwardVector() const {
    return Vec3{
        std::sin(m_yaw) * std::cos(m_pitch),
        std::sin(m_pitch),
        std::cos(m_yaw) * std::cos(m_pitch)
    }.normalized();
}

void Camera::update(float deltaTime) {
    if (m_mouseCaptured) {
        m_yaw -= m_mouseDx * sensitivity;
        m_pitch -= m_mouseDy * sensitivity;
        m_pitch = std::clamp(m_pitch, -1.55f, 1.55f);
    }
    m_mouseDx = 0;
    m_mouseDy = 0;

    Vec3 forward = forwardVector();
    Vec3 worldUp{ 0, 1, 0 };
    Vec3 right = forward.cross(worldUp).normalized();

    Vec3 moveDir{ 0,0,0 };
    if (m_forward)  moveDir += forward;
    if (m_backward) moveDir -= forward;
    if (m_left)     moveDir -= right;
    if (m_right)    moveDir += right;
    if (m_up)       moveDir += worldUp;
    if (m_down)     moveDir -= worldUp;

    float len = moveDir.length();
    if (len > 0) {
        moveDir = moveDir / len;
        float speed = moveSpeed * (m_sprint ? sprintMultiplier : 1.0f);
        m_position += moveDir * speed * deltaTime;
    }

    m_view = Mat4::lookAt(m_position, m_position + forward, worldUp);
}

void Camera::lookAt(const Vec3& target) {
    Vec3 dir = (target - m_position).normalized();
    // compute yaw/pitch from direction
    m_yaw = std::atan2(dir.x, dir.z);
    m_pitch = std::asin(dir.y);
    m_view = Mat4::lookAt(m_position, target, { 0, 1, 0 });
}

void Camera::focusOn(const Vec3& target, float radius) {
    radius = std::max(radius, 0.25f);
    float distance = std::max(radius * 2.8f, 1.5f);
    m_position = {
        target.x,
        target.y + radius * 0.35f,
        target.z - distance
    };
    lookAt(target);
}

void Camera::onKeyDown(uint32_t key) {
    switch (key) {
        case SAPP_KEYCODE_W: m_forward = true; break;
        case SAPP_KEYCODE_S: m_backward = true; break;
        case SAPP_KEYCODE_A: m_left = true; break;
        case SAPP_KEYCODE_D: m_right = true; break;
        case SAPP_KEYCODE_E: m_up = true; break;
        case SAPP_KEYCODE_Q: m_down = true; break;
        case SAPP_KEYCODE_LEFT_SHIFT:
        case SAPP_KEYCODE_RIGHT_SHIFT:
            m_sprint = true;
            break;
    }
}

void Camera::onKeyUp(uint32_t key) {
    switch (key) {
        case SAPP_KEYCODE_W: m_forward = false; break;
        case SAPP_KEYCODE_S: m_backward = false; break;
        case SAPP_KEYCODE_A: m_left = false; break;
        case SAPP_KEYCODE_D: m_right = false; break;
        case SAPP_KEYCODE_E: m_up = false; break;
        case SAPP_KEYCODE_Q: m_down = false; break;
        case SAPP_KEYCODE_LEFT_SHIFT:
        case SAPP_KEYCODE_RIGHT_SHIFT:
            m_sprint = false;
            break;
    }
}

void Camera::onMouseMove(float dx, float dy) {
    if (m_mouseCaptured) {
        m_mouseDx += dx;
        m_mouseDy += dy;
    }
}

void Camera::onMouseScroll(float dy) {
    moveSpeed += dy * scrollSensitivity;
    moveSpeed = std::max(0.1f, moveSpeed);
}

void Camera::captureMouse(bool captured) {
    m_mouseCaptured = captured;
    m_mouseDx = 0;
    m_mouseDy = 0;
}
