#pragma once
#include "BaseUtils/BUAABB.h"

class OrbitCamera
{
public:
    static constexpr float kFieldOfView = 0.785398f;
    static constexpr float kNearPlane = 0.05f;

    OrbitCamera();
    BUQuaternion rotation{0, 0, 0, 1};
    BUVector3 target;
    float distance = 3.0f;

    void Frame(const BUAABB& bounds, float aspect = 1.0f);
    void ResetRotation();
    void BeginOrbit(const BUVector2& mouse, const BUVector2& viewport);
    void Orbit(const BUVector2& mouse, const BUVector2& viewport);
    void Pan(const BUVector2& delta, const BUVector2& viewport);
    void Zoom(float amount, const BUVector2& mouse, const BUVector2& viewport);

    BUVector3 Eye() const;
    BUVector3 Up() const;
    BUVector3 Right() const;

private:
    BUQuaternion dragRotation{0, 0, 0, 1};
    BUVector3 dragStart;
};
