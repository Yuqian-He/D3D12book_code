#include "Camera.h"
#include <DirectXMath.h>
#include <cmath>  // for cosf, sinf

using namespace DirectX;

template <typename T>
const T& clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : (value > high) ? high : value;
}

Camera::Camera()
    : m_position({ 0.0f, 0.0f, -5.0f }),
      m_target({ 0.0f, 0.0f, 0.0f }),
      m_up({ 0.0f, 1.0f, 0.0f }),
      m_yaw(0.0f),
      m_pitch(0.0f),
      m_speed(0.2f)  // 默认速度
{
    UpdateViewMatrix();
}

Camera::Camera(XMFLOAT3 position, XMFLOAT3 target, XMFLOAT3 up)
    : m_position(position), m_target(target), m_up(up), m_yaw(0.0f), m_pitch(0.0f), m_speed(0.2f)
{
    UpdateViewMatrix();
}

void Camera::SetPosition(const XMFLOAT3& position) {
    m_position = position;
    UpdateViewMatrix();
}

void Camera::SetTarget(const XMFLOAT3& target) {
    m_target = target;
    UpdateViewMatrix();
}

void Camera::SetUp(const XMFLOAT3& up) {
    m_up = up;
    UpdateViewMatrix();
}

void Camera::SetSpeed(float speed) {
    m_speed = speed;
}

void Camera::UpdateViewMatrix() {
    XMVECTOR posVec = XMLoadFloat3(&m_position);
    XMVECTOR targetVec = XMLoadFloat3(&m_target);
    XMVECTOR upVec = XMLoadFloat3(&m_up);

    m_viewMatrix = XMMatrixLookAtLH(posVec, targetVec, upVec);
}

XMMATRIX Camera::GetViewMatrix() const {
    return m_viewMatrix;
}

// 前后移动
void Camera::MoveForward(float distance) {
    XMVECTOR forwardVec = XMVector3Normalize(XMLoadFloat3(&m_target) - XMLoadFloat3(&m_position));
    XMVECTOR posVec = XMLoadFloat3(&m_position);
    posVec += forwardVec * distance * m_speed;
    XMStoreFloat3(&m_position, posVec);
    UpdateViewMatrix();
}

// 左右移动
void Camera::MoveRight(float distance) {
    XMVECTOR forwardVec = XMVector3Normalize(XMLoadFloat3(&m_target) - XMLoadFloat3(&m_position));
    XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&m_up), forwardVec));
    XMVECTOR posVec = XMLoadFloat3(&m_position);
    posVec += rightVec * distance * m_speed;
    XMStoreFloat3(&m_position, posVec);
    UpdateViewMatrix();
}

// 上下移动
void Camera::MoveUp(float distance) {
    XMVECTOR upVec = XMLoadFloat3(&m_up);
    XMVECTOR posVec = XMLoadFloat3(&m_position);
    posVec += upVec * distance * m_speed;
    XMStoreFloat3(&m_position, posVec);
    UpdateViewMatrix();
}

void Camera::Rotate(float yawDelta, float pitchDelta) {
    m_yaw += yawDelta;
    m_pitch += pitchDelta;

    // 限制 pitch 角范围 [-89, 89] 度
    m_pitch = clamp(m_pitch, -89.0f, 89.0f);

    // 计算新的相机位置围绕目标点旋转
    float yawRadians = XMConvertToRadians(m_yaw);
    float pitchRadians = XMConvertToRadians(m_pitch);

    // 假设目标点为世界中心 (0, 0, 0)，你可以改成任何指定点
    XMVECTOR targetVec = XMLoadFloat3(&m_target);

    // 根据 yaw 和 pitch 计算新位置
    XMFLOAT3 newPosition = {
        cosf(yawRadians) * cosf(pitchRadians),
        sinf(pitchRadians),
        sinf(yawRadians) * cosf(pitchRadians)
    };

    // 设置距离目标点的固定距离
    float radius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&m_position) - targetVec));
    XMVECTOR newPosVec = targetVec + XMVectorScale(XMLoadFloat3(&newPosition), radius);

    // 更新相机位置
    XMStoreFloat3(&m_position, newPosVec);

    // 更新视图矩阵
    UpdateViewMatrix();
}

DirectX::XMFLOAT3 Camera::GetPosition() const {
    return m_position; 
}