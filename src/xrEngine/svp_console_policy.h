#pragma once

#include "xr_ioc_cmd.h"

class CCC_SvpFixedInteger final : public CCC_Integer
{
public:
	CCC_SvpFixedInteger(LPCSTR name, int* value, int minimum, int maximum)
		: CCC_Integer(name, value, minimum, maximum), canonical(*value)
	{
	}

	void Execute(LPCSTR args) override
	{
		const int requested = atoi(args);
		if (requested < min || requested > max)
			InvalidSyntax();
		*value = canonical;
	}

	void Info(TInfo& info) override
	{
		xr_sprintf(info, sizeof(info), "fixed value %d", canonical);
	}

	void Save(IWriter*) override
	{
	}

private:
	const int canonical;
};

class CCC_SvpProfileFloat final : public CCC_Float
{
public:
	CCC_SvpProfileFloat(LPCSTR name, float* value, float minimum, float maximum)
		: CCC_Float(name, value, minimum, maximum)
	{
	}

	void Execute(LPCSTR args) override
	{
		const float requested = float(atof(args));
		if (requested < min - EPS || requested > max + EPS)
			InvalidSyntax();
	}

	void Info(TInfo& info) override
	{
		xr_strcpy(info, "read only active optic profile value");
	}

	void Save(IWriter*) override
	{
	}
};

// settable for the session, never written to user.ltx
class CCC_SvpVolatileFloat final : public CCC_Float
{
public:
	CCC_SvpVolatileFloat(LPCSTR name, float* value, float minimum, float maximum)
		: CCC_Float(name, value, minimum, maximum)
	{
	}

	void Save(IWriter*) override
	{
	}
};

class CCC_SvpInternalInteger final : public CCC_Integer
{
public:
	CCC_SvpInternalInteger(LPCSTR name, int* value, int minimum, int maximum)
		: CCC_Integer(name, value, minimum, maximum)
	{
	}

	void Execute(LPCSTR args) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Integer::Execute(args);
#else
		const int requested = atoi(args);
		if (requested < min || requested > max)
			InvalidSyntax();
#endif
	}

	void Info(TInfo& info) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Integer::Info(info);
#else
		xr_strcpy(info, "read only tuning value");
#endif
	}

	void Save(IWriter*) override
	{
	}
};

class CCC_SvpInternalFloat final : public CCC_Float
{
public:
	CCC_SvpInternalFloat(LPCSTR name, float* value, float minimum, float maximum)
		: CCC_Float(name, value, minimum, maximum)
	{
	}

	void Execute(LPCSTR args) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Float::Execute(args);
#else
		const float requested = float(atof(args));
		if (requested < min - EPS || requested > max + EPS)
			InvalidSyntax();
#endif
	}

	void Info(TInfo& info) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Float::Info(info);
#else
		xr_strcpy(info, "read only tuning value");
#endif
	}

	void Save(IWriter*) override
	{
	}
};

class CCC_SvpInternalVector4 final : public CCC_Vector4
{
public:
	CCC_SvpInternalVector4(LPCSTR name, Fvector4* value,
		const Fvector4 minimum, const Fvector4 maximum)
		: CCC_Vector4(name, value, minimum, maximum)
	{
	}

	void Execute(LPCSTR args) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Vector4::Execute(args);
#else
		Fvector4 requested;
		if (4 != sscanf(args, "%f,%f,%f,%f", &requested.x, &requested.y,
			&requested.z, &requested.w) &&
			4 != sscanf(args, "(%f,%f,%f,%f)", &requested.x, &requested.y,
				&requested.z, &requested.w))
		{
			InvalidSyntax();
			return;
		}
		if (requested.x < min.x || requested.y < min.y || requested.z < min.z ||
			requested.w < min.w || requested.x > max.x || requested.y > max.y ||
			requested.z > max.z || requested.w > max.w)
			InvalidSyntax();
#endif
	}

	void Info(TInfo& info) override
	{
#if defined(SVP_INTERNAL_TUNING)
		CCC_Vector4::Info(info);
#else
		xr_strcpy(info, "read only tuning value");
#endif
	}

	void Save(IWriter*) override
	{
	}
};
