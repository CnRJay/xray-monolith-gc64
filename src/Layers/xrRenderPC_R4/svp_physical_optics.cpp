#include "svp_physical_optics.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace SvpPhysicalOptics
{
namespace
{
constexpr float kMagnificationKnots[] = { 1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 20.f };
constexpr std::size_t kMagnificationKnotCount = sizeof(kMagnificationKnots) / sizeof(kMagnificationKnots[0]);
}

float SampleMagnificationResponse(const MagnificationResponse& response, float magnification)
{
	if (magnification <= kMagnificationKnots[0])
		return response.value[0];

	for (std::size_t i = 1; i < kMagnificationKnotCount; ++i)
	{
		if (magnification > kMagnificationKnots[i])
			continue;
		const float blend = (magnification - kMagnificationKnots[i - 1]) /
			(kMagnificationKnots[i] - kMagnificationKnots[i - 1]);
		return response.value[i - 1] + (response.value[i] - response.value[i - 1]) * blend;
	}
	return response.value[kMagnificationKnotCount - 1];
}

float ApplyMagnificationResponse(const MagnificationResponse& response, float magnification, float scale, float offset)
{
	return std::max(SampleMagnificationResponse(response, magnification) * scale + offset, 0.f);
}

float MagnificationFraction(float magnification, float minimum, float maximum)
{
	if (maximum <= minimum)
		return 0.f;
	return std::clamp((magnification - minimum) / (maximum - minimum), 0.f, 1.f);
}

float InterpolateMagnification(float low, float high, float magnification, float minimum, float maximum)
{
	return low + (high - low) * MagnificationFraction(magnification, minimum, maximum);
}

float InterpolateReciprocalMagnification(float low, float high, float magnification, float minimum, float maximum)
{
	if (minimum <= 0.f || maximum <= minimum)
		return low;
	magnification = std::clamp(magnification, minimum, maximum);
	const float fraction = (1.f / minimum - 1.f / magnification) / (1.f / minimum - 1.f / maximum);
	return low + (high - low) * fraction;
}

Vec2 LimitEyeOffset(const Vec2& offset, float limit_mm)
{
	Vec2 limited = offset;
	limit_mm = std::max(limit_mm, 0.f);
	const float length_squared = limited.x * limited.x + limited.y * limited.y;
	if (length_squared > limit_mm * limit_mm && length_squared > 0.00001f)
	{
		const float scale = limit_mm / std::sqrt(length_squared);
		limited.x *= scale;
		limited.y *= scale;
	}
	return limited;
}

ObjectiveRegistration MapObjectiveAxisToEyepiece(const Vec3& eye_local,
	const Vec3& objective_local, const Vec2& lens_radius)
{
	ObjectiveRegistration registration;
	if (!std::isfinite(eye_local.x) || !std::isfinite(eye_local.y)
		|| !std::isfinite(eye_local.z) || !std::isfinite(objective_local.x)
		|| !std::isfinite(objective_local.y) || !std::isfinite(objective_local.z)
		|| !std::isfinite(lens_radius.x) || lens_radius.x <= 0.00001f
		|| !std::isfinite(lens_radius.y) || lens_radius.y <= 0.00001f
		|| eye_local.z >= -0.00001f || objective_local.z <= 0.00001f)
		return registration;

	const float span = objective_local.z - eye_local.z;
	if (!std::isfinite(span) || span <= 0.00001f)
		return registration;

	registration.fraction = -eye_local.z / span;
	if (!std::isfinite(registration.fraction)
		|| registration.fraction < 0.f || registration.fraction > 1.f)
		return registration;

	registration.hit.x = eye_local.x
		+ (objective_local.x - eye_local.x) * registration.fraction;
	registration.hit.y = eye_local.y
		+ (objective_local.y - eye_local.y) * registration.fraction;
	registration.principal.x = registration.hit.x / lens_radius.x;
	registration.principal.y = registration.hit.y / lens_radius.y;
	registration.valid = std::isfinite(registration.hit.x)
		&& std::isfinite(registration.hit.y)
		&& std::isfinite(registration.principal.x)
		&& std::isfinite(registration.principal.y);
	registration.inside_aperture = registration.valid
		&& registration.principal.x * registration.principal.x
			+ registration.principal.y * registration.principal.y <= 1.f;
	return registration;
}

void AccelerateEye(Vec2& velocity, const Vec2& desired_velocity, float max_delta)
{
	max_delta = std::max(max_delta, 0.f);
	const float delta_x = desired_velocity.x - velocity.x;
	const float delta_y = desired_velocity.y - velocity.y;
	const float delta_squared = delta_x * delta_x + delta_y * delta_y;
	if (delta_squared <= max_delta * max_delta || delta_squared <= 0.00001f)
	{
		velocity = desired_velocity;
		return;
	}

	const float scale = max_delta / std::sqrt(delta_squared);
	velocity.x += delta_x * scale;
	velocity.y += delta_y * scale;
}

void UpdateEyeTracking(EyeTrackingState& state, const Vec2& target, bool suspended, std::uint32_t epoch,
	std::uint32_t frame, float dt, float tracking_speed, float acceleration_mm_s2)
{
	if (!state.valid || state.epoch != epoch)
	{
		state.offset = suspended ? Vec2{} : target;
		state.velocity = {};
		state.epoch = epoch;
		state.frame = frame;
		state.valid = true;
		return;
	}
	if (state.frame == frame)
		return;

	state.frame = frame;
	if (suspended)
	{
		state.velocity = {};
		return;
	}

	dt = std::clamp(dt, 0.f, 0.05f);
	const Vec2 error = { target.x - state.offset.x, target.y - state.offset.y };
	const Vec2 desired_velocity = { error.x * tracking_speed, error.y * tracking_speed };
	AccelerateEye(state.velocity, desired_velocity, acceleration_mm_s2 * dt);

	const Vec2 step = { state.velocity.x * dt, state.velocity.y * dt };
	const float error_squared = error.x * error.x + error.y * error.y;
	const float step_squared = step.x * step.x + step.y * step.y;
	if (step.x * error.x + step.y * error.y > 0.f && step_squared >= error_squared)
	{
		state.offset = target;
		state.velocity = {};
		return;
	}
	state.offset.x += step.x;
	state.offset.y += step.y;
}
} // namespace SvpPhysicalOptics
