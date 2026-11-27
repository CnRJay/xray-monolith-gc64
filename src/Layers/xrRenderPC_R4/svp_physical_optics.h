#pragma once

#include <cstdint>

namespace SvpPhysicalOptics
{
struct Vec2
{
	float x = 0.f;
	float y = 0.f;
};

struct Vec3
{
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
};

struct MagnificationResponse
{
	float value[8] = {};
};

struct EyeTrackingState
{
	Vec2 offset;
	Vec2 velocity;
	std::uint32_t epoch = 0;
	std::uint32_t frame = 0;
	bool valid = false;
};

struct ObjectiveRegistration
{
	Vec2 hit;
	Vec2 principal;
	float fraction = 0.f;
	bool inside_aperture = false;
	bool valid = false;
};

float SampleMagnificationResponse(const MagnificationResponse& response, float magnification);
float ApplyMagnificationResponse(const MagnificationResponse& response, float magnification, float scale, float offset);
float MagnificationFraction(float magnification, float minimum, float maximum);
float InterpolateMagnification(float low, float high, float magnification, float minimum, float maximum);
float InterpolateReciprocalMagnification(float low, float high, float magnification, float minimum, float maximum);
Vec2 LimitEyeOffset(const Vec2& offset, float limit_mm);
ObjectiveRegistration MapObjectiveAxisToEyepiece(const Vec3& eye_local,
	const Vec3& objective_local, const Vec2& lens_radius);
void AccelerateEye(Vec2& velocity, const Vec2& desired_velocity, float max_delta);
void UpdateEyeTracking(EyeTrackingState& state, const Vec2& target, bool suspended, std::uint32_t epoch,
	std::uint32_t frame, float dt, float tracking_speed, float acceleration_mm_s2);
} // namespace SvpPhysicalOptics
