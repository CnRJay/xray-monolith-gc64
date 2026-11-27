#ifndef dxEnvironmentRender_included
#define dxEnvironmentRender_included
#pragma once

#include "..\..\Include\xrRender\EnvironmentRender.h"

#include "blenders\blender.h"

class CBlender_skybox : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: combiner"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C)
	{
		switch (C.iElement)
		{
		case 0:
			C.r_Pass("sky2", "sky2", FALSE, TRUE, FALSE);
#if defined(USE_DX10) || defined(USE_DX11)
			//C.r_Sampler_clf		("s_sky0",		"$null"			);
			//C.r_Sampler_clf		("s_sky1",		"$null"			);
			C.r_dx10Texture("s_sky0", "$null");
			C.r_dx10Texture("s_sky1", "$null");
			C.r_dx10Sampler("smp_rtlinear");
			//C.r_Sampler_rtf		("s_tonemap",	"$user$tonemap"	);	//. hack
			C.r_dx10Texture("s_tonemap", "$user$tonemap"); //. hack
			C.PassSET_ZB(FALSE, FALSE);
#else	//	USE_DX10
			C.r_Sampler_clf("s_sky0", "$null");
			C.r_Sampler_clf("s_sky1", "$null");
			C.r_Sampler_rtf("s_tonemap", "$user$tonemap"); //. hack
#endif	//	USE_DX10
			C.r_End();
			break;

		case 1:
#if defined(USE_DX10) || defined(USE_DX11)
			C.r_Pass("ssfx_sky_mv", "ssfx_sky_mv", FALSE, TRUE, FALSE);
			C.PassSET_ZB(FALSE, FALSE);
			C.r_End();
#endif
			break;
		}
	}
};

class dxEnvDescriptorRender : public IEnvDescriptorRender
{
	friend class dxEnvDescriptorMixerRender;
public:
	virtual void OnDeviceCreate(CEnvDescriptor& owner);
	virtual void OnDeviceDestroy();

	virtual void Copy(IEnvDescriptorRender& _in);
private:
	ref_texture sky_texture;
	ref_texture sky_texture_env;
	ref_texture clouds_texture;
};

class dxEnvDescriptorMixerRender : public IEnvDescriptorMixerRender
{
public:
	virtual void Copy(IEnvDescriptorMixerRender& _in);

	virtual void Destroy();
	virtual void Clear();
	virtual void lerp(IEnvDescriptorRender* inA, IEnvDescriptorRender* inB);
	void SnapshotForRT();
	//private:
public:
	STextureList sky_r_textures;
	STextureList sky_r_textures_env;
	STextureList clouds_r_textures;

	STextureList rt_sky_r_textures;
	STextureList rt_sky_r_textures_env;
	STextureList rt_clouds_r_textures;
	bool rt_snapshot_valid = false;
};

class dxEnvironmentRender : public IEnvironmentRender
{
public:
	dxEnvironmentRender();
	virtual void Copy(IEnvironmentRender& _in);

	virtual void OnFrame(CEnvironment& env);
	void SyncSkyTextures(CEnvironment& env);
	virtual void OnLoad();
	virtual void OnUnload();
	virtual void RenderSky(CEnvironment& env, bool OnlyMV = false);
	virtual void RenderClouds(CEnvironment& env);
	virtual void OnDeviceCreate();
	virtual void OnDeviceDestroy();
	virtual particles_systems::library_interface const& particles_systems_library();

private:
	CBlender_skybox m_b_skybox;

	ref_shader sh_2sky;
	ref_geom sh_2geom;

	ref_shader sh_2sky_mv;

	ref_shader clouds_sh;
	ref_geom clouds_geom;

	ref_texture tonemap;
	ref_texture tsky0, tsky1;
};

#endif	//	EnvironmentRender_included
