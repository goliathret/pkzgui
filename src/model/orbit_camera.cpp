#include "model/orbit_camera.h"
#include <algorithm>
#include <cmath>

namespace
{
    BUQuaternion Normalize(BUQuaternion rotation)
    {
        const float length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                                       rotation.z * rotation.z + rotation.w * rotation.w);
        return rotation * (1.0f / length);
    }

    BUVector3 Rotate(const BUQuaternion& rotation, const BUVector3& vector)
    {
        BUVector3 result;
        rotation.Rotate(result, vector);
        return result;
    }

    BUVector3 TrackballPoint(const BUVector2& mouse, const BUVector2& viewport)
    {
        const float radius = std::max(1.0f, std::min(viewport.x, viewport.y) * 0.4f);
        const float x = (mouse.x - viewport.x * 0.5f) / radius;
        const float y = (viewport.y * 0.5f - mouse.y) / radius;
        const float squared = x * x + y * y;
        // A hyperbolic rim keeps rotation smooth when a drag leaves the virtual sphere.
        const float z = squared <= 0.5f ? std::sqrt(1.0f - squared) : 0.5f / std::sqrt(squared);
        BUVector3 point(x, y, z);
        point.Normalize();
        return point;
    }

    BUQuaternion RotationBetween(const BUVector3& from, const BUVector3& to)
    {
        const float dot = std::clamp(from.Dot(to), -1.0f, 1.0f);
        BUVector3 axis = from.Cross(to);
        if (dot < -0.999999f)
        {
            axis = from.Cross(BUVector3(1, 0, 0));
            if (axis.LengthSquared() < 0.000001f)
                axis = from.Cross(BUVector3(0, 1, 0));
            axis.Normalize();
            return {axis.x, axis.y, axis.z, 0};
        }
        return Normalize({axis.x, axis.y, axis.z, 1.0f + dot});
    }
} // namespace

OrbitCamera::OrbitCamera()
{
    ResetRotation();
}

void OrbitCamera::ResetRotation()
{
    const BUQuaternion yaw{0, std::sin(0.3f), 0, std::cos(0.3f)};
    const BUQuaternion pitch{-std::sin(0.175f), 0, 0, std::cos(0.175f)};
    rotation = Normalize(yaw * pitch);
}

void OrbitCamera::Frame(const BUAABB& bounds, float aspect)
{
    if (bounds.mMin.x > bounds.mMax.x)
        return;
    target = (bounds.mMin + bounds.mMax) * 0.5f;
    const float radius = (bounds.mMax - bounds.mMin).Length() * 0.5f;
    const float halfAngle = std::atan(std::tan(kFieldOfView * 0.5f) * std::clamp(aspect, 0.01f, 1.0f));
    distance = std::max(radius * 1.08f / std::sin(halfAngle), 0.2f);
}

void OrbitCamera::BeginOrbit(const BUVector2& mouse, const BUVector2& viewport)
{
    dragStart = TrackballPoint(mouse, viewport);
    dragRotation = rotation;
}

void OrbitCamera::Orbit(const BUVector2& mouse, const BUVector2& viewport)
{
    auto delta = RotationBetween(dragStart, TrackballPoint(mouse, viewport));
    delta.Conjugate();
    rotation = Normalize(dragRotation * delta);
}

void OrbitCamera::Pan(const BUVector2& delta, const BUVector2& viewport)
{
    const float scale = 2.0f * distance * std::tan(kFieldOfView * 0.5f) / std::max(1.0f, viewport.y);
    target += (Up() * delta.y - Right() * delta.x) * scale;
}

void OrbitCamera::Zoom(float amount, const BUVector2& mouse, const BUVector2& viewport)
{
    if (amount == 0)
        return;
    const float previous = distance;
    distance = std::clamp(distance * std::exp(-0.12f * std::clamp(amount, -50.0f, 50.0f)), 0.1f, 1000000.0f);
    const float height = std::max(1.0f, viewport.y);
    const float x = (2.0f * mouse.x - viewport.x) / height;
    const float y = (viewport.y - 2.0f * mouse.y) / height;
    const BUVector3 offset = (Right() * x + Up() * y) * std::tan(kFieldOfView * 0.5f);
    target += offset * (previous - distance);
}

BUVector3 OrbitCamera::Eye() const
{
    return target + Rotate(rotation, BUVector3(0, 0, 1)) * distance;
}

BUVector3 OrbitCamera::Up() const
{
    return Rotate(rotation, BUVector3(0, 1, 0));
}

BUVector3 OrbitCamera::Right() const
{
    return Rotate(rotation, BUVector3(1, 0, 0));
}
