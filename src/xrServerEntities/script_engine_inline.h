////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine_inline.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC void CScriptEngine::add_script_process(const EScriptProcessors& process_id, CScriptProcess* script_process)
{
	//	CScriptProcessStorage::const_iterator	I = m_script_processes.find(process_id);
	//	VERIFY									(I == m_script_processes.end());
	m_script_processes.insert(std::make_pair(process_id, script_process));
}

CScriptProcess* CScriptEngine::script_process(const EScriptProcessors& process_id) const
{
	CScriptProcessStorage::const_iterator I = m_script_processes.find(process_id);
	if ((I != m_script_processes.end()))
		return ((*I).second);
	return (0);
}

IC void CScriptEngine::parse_script_namespace(LPCSTR function_to_call, LPSTR name_space, u32 const namespace_size,
                                              LPSTR function, u32 const function_size)
{
	LPCSTR I = function_to_call, J = 0;
	for (; ; J = I, ++I)
	{
		I = strchr(I, '.');
		if (!I)
			break;
	}
	xr_strcpy(name_space, namespace_size, "_G");
	if (!J)
		xr_strcpy(function, function_size, function_to_call);
	else
	{
		CopyMemory(name_space, function_to_call, u32(J - function_to_call)*sizeof(char));
		name_space[u32(J - function_to_call)] = 0;
		xr_strcpy(function, function_size, J + 1);
	}
}

#ifdef USE_LUA_FUNCTOR_CACHE
extern BOOL lua_use_functor_cache;
extern BOOL g_bootComplete;
IC void CScriptEngine::invalidate_functor_cache()
{
	m_functor_cache.clear();
	m_cache_valid = false;
}
#endif // USE_LUA_FUNCTOR_CACHE

template <typename _result_type>
IC bool CScriptEngine::functor(LPCSTR function_to_call, ::luabind::functor<_result_type>& lua_function)
{
	
#ifdef USE_LUA_FUNCTOR_CACHE
	// Check if cache is valid
	if (!m_cache_valid)
	{
		invalidate_functor_cache();
	}
	else if (lua_use_functor_cache && g_bootComplete)
	{
		// PROF_EVENT("CScriptEngine::functor cached");

		// Create cache key
		FunctorCacheKey key{ function_to_call, typeid(_result_type).hash_code() };

		// Try to find in cache
		auto it = m_functor_cache.find(key);
		if (it != m_functor_cache.end())
		{
			lua_function = ::luabind::object_cast<::luabind::functor<_result_type>>(it->second);
			return true;
		}
	}
#endif
	
	// PROF_EVENT("CScriptEngine::functor");

	// Not in cache or invalid, create new entry
	::luabind::object object;
	if (!function_object(function_to_call, object))
		return (false);

	lua_function = ::luabind::object_cast<::luabind::functor<_result_type>>(object);

#ifdef USE_LUA_FUNCTOR_CACHE
	// Store in cache
	if (lua_use_functor_cache && m_cache_valid && g_bootComplete)
	{
		FunctorCacheKey key{ function_to_call, typeid(_result_type).hash_code() };
		m_functor_cache.insert({ key, object });
	}
#endif

	return (true);
}

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		IC CScriptDebugger *CScriptEngine::debugger	()
		{
			return			(m_scriptDebugger);
		}
#	else // ifndef USE_LUA_STUDIO
#	endif // ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
