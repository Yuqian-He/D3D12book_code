#pragma once

#include <DirectXMath.h>

class Camera {
public:
    Camera();
    Camera(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target, DirectX::XMFLOAT3 up);

    // 设置摄像机参数
    void SetPosition(const DirectX::XMFLOAT3& position);
    void SetTarget(const DirectX::XMFLOAT3& target);
    void SetUp(const DirectX::XMFLOAT3& up);

    // 更新视图矩阵
    void UpdateViewMatrix();

    // 获取视图矩阵
    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const;

    // 控制摄像机移动
    void MoveForward(float distance);
    void MoveRight(float distance);
    void MoveUp(float distance);  // 新增：控制上下平移

    // 控制摄像机旋转
    void Rotate(float yawDelta, float pitchDelta);

    // 更新摄像机的速度（控制移动速度）
    void SetSpeed(float speed);


private:
    DirectX::XMFLOAT3 m_position;  // 摄像机位置
    DirectX::XMFLOAT3 m_target;    // 目标点
    DirectX::XMFLOAT3 m_up;        // 上方向
    DirectX::XMMATRIX m_viewMatrix; // 视图矩阵

    float m_yaw;   // 偏航角
    float m_pitch; // 俯仰角

    float m_speed; // 移动速度
};