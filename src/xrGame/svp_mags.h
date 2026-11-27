#ifndef svp_magsH
#define svp_magsH
#pragma once

enum svp_mag_mode { svp_mag_none, svp_mag_fixed, svp_mag_continuous, svp_mag_detent };

struct svp_mags_data
{
	svp_mag_mode mode = svp_mag_none;
	u8 count = 0;
	float values[16] = {};
	u64 fingerprint = 0;
};

float svp_magnification_to_runtime_factor(float magnification);
float svp_runtime_factor_to_magnification(float runtime_factor);
float svp_magnification_scroll_multiplier(float scroll_multiplier);
float svp_magnification_to_weapon_factor(float magnification, float scroll_multiplier);
float svp_weapon_factor_to_magnification(float weapon_factor, float scroll_multiplier);
bool svp_magnification_ladder_valid(const svp_mags_data& ladder);
u64 svp_magnification_fingerprint(const svp_mags_data& ladder);
int svp_magnification_exact_index(const svp_mags_data& ladder, float magnification);
int svp_magnification_nearest_index(const svp_mags_data& ladder, float magnification);
float svp_magnification_adjacent(const svp_mags_data& ladder, float magnification, int direction);

#endif
