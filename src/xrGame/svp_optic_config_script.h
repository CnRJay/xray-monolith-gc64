#pragma once

namespace luabind
{
	class object;
}

int svp_optic_api_version();
luabind::object svp_optic_api_info();
bool svp_optic_api_has_capability(LPCSTR capability);
luabind::object svp_optic_api_connect(double api, double schema);
luabind::object svp_optic_api_describe();
luabind::object svp_validate_optic_fields(const luabind::object& table);
luabind::object svp_validate_optic_profile(const luabind::object& table);
u32 svp_optic_route_epoch();
u32 svp_begin_optic_context(LPCSTR context, LPCSTR weapon, double weapon_id,
	LPCSTR scope, double zoom_type,
	LPCSTR identity_source, LPCSTR diagnostic_scope);
luabind::object svp_apply_optic_profile(double context_token, const luabind::object& table);
bool svp_clear_optic_profile(double context_token);
luabind::object svp_current_optic_profile();

#if defined(SVP_TEST_CLIENT)
struct lua_State;
class CSecondVPParams;
void svp_test_client_attach(lua_State* state);
void svp_test_client_set_scope_mode(int mode);
CSecondVPParams& svp_test_client_viewport();
#endif
