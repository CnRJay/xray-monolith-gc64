#include "stdafx.h"
#include "r4_volumetric_froxels.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../xrRender/light.h"
#include "../xrRender/xrRender_console.h"
#include <d3dcompiler.h>
#include <algorithm>

namespace
{
	class CFroxelShaderInclude : public ID3DInclude
	{
	public:
		HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*IncludeType*/, LPCSTR pFileName, LPCVOID /*pParentData*/,
			LPCVOID* ppData, UINT* pBytes) override
		{
			string_path path;
			strconcat(sizeof(path), path, ::Render->getShaderPath(), pFileName);
			IReader* R = FS.r_open("$game_shaders$", path);
			if (!R)
				return E_FAIL;

			const u32 sz = R->length();
			void* mem = xr_malloc(sz);
			CopyMemory(mem, R->pointer(), sz);
			FS.r_close(R);

			*ppData = mem;
			*pBytes = sz;
			return S_OK;
		}

		HRESULT __stdcall Close(LPCVOID pData) override
		{
			void* ptr = const_cast<void*>(pData);
			xr_free(ptr);
			return S_OK;
		}
	};

	bool CompileFroxelShader(LPCSTR fileName, LPCSTR target, const D3D_SHADER_MACRO* defines, ID3DBlob** outBlob)
	{
		string_path path;
		strconcat(sizeof(path), path, ::Render->getShaderPath(), fileName);
		IReader* R = FS.r_open("$game_shaders$", path);
		if (!R)
		{
			Msg("! [FROXEL] Missing shader file: %s", path);
			return false;
		}

		CFroxelShaderInclude includer;
		ID3DBlob* errBlob = nullptr;
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		HRESULT hr = D3DCompile(R->pointer(), R->length(), fileName, defines, &includer, "main", target, flags, 0, outBlob, &errBlob);
		FS.r_close(R);

		if (FAILED(hr))
		{
			Msg("! [FROXEL] Failed to compile %s (%s): %s", fileName, target,
				errBlob ? (LPCSTR)errBlob->GetBufferPointer() : "unknown error");
			_RELEASE(errBlob);
			return false;
		}
		_RELEASE(errBlob);
		return true;
	}

	bool CreateVolumeTexture3D(u32 w, u32 h, u32 d, DXGI_FORMAT fmt,
		ID3D11Texture3D** tex, ID3D11ShaderResourceView** srv, ID3D11UnorderedAccessView** uav)
	{
		D3D11_TEXTURE3D_DESC td{};
		td.Width = w;
		td.Height = h;
		td.Depth = d;
		td.MipLevels = 1;
		td.Format = fmt;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		if (FAILED(HW.pDevice->CreateTexture3D(&td, nullptr, tex)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = fmt;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		sd.Texture3D.MipLevels = 1;
		if (FAILED(HW.pDevice->CreateShaderResourceView(*tex, &sd, srv)))
			return false;

		D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
		ud.Format = fmt;
		ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		ud.Texture3D.MipSlice = 0;
		ud.Texture3D.FirstWSlice = 0;
		ud.Texture3D.WSize = d;
		if (FAILED(HW.pDevice->CreateUnorderedAccessView(*tex, &ud, uav)))
			return false;

		return true;
	}

	void UnbindComputeSlots()
	{
		ID3D11UnorderedAccessView* nullUavs[2] = { nullptr, nullptr };
		ID3D11ShaderResourceView* nullSrvs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		HW.pContext->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
		HW.pContext->CSSetShaderResources(0, 6, nullSrvs);
		HW.pContext->CSSetShader(nullptr, nullptr, 0);
	}
}

CVolumetricFroxels::CVolumetricFroxels()
{
	m_collectedLights.reserve(MAX_DYNAMIC_LIGHTS);
}

CVolumetricFroxels::~CVolumetricFroxels()
{
	Destroy();
}

void CVolumetricFroxels::ReleaseVolumeResources()
{
	_RELEASE(m_mediaUav); _RELEASE(m_mediaSrv); _RELEASE(m_mediaTex);
	_RELEASE(m_sunVisUav); _RELEASE(m_sunVisSrv); _RELEASE(m_sunVisTex);
	_RELEASE(m_spotAccumUav); _RELEASE(m_spotAccumSrv); _RELEASE(m_spotAccumTex);
	for (int i = 0; i < 2; ++i)
	{
		_RELEASE(m_lightUav[i]); _RELEASE(m_lightSrv[i]); _RELEASE(m_lightTex[i]);
	}
	_RELEASE(m_integratedUav); _RELEASE(m_integratedSrv); _RELEASE(m_integratedTex);
	m_gridW = m_gridH = m_gridD = 0;
	m_currentQuality = -1;
	m_historyValid = false;
}

void CVolumetricFroxels::Destroy()
{
	ReleaseVolumeResources();

	_RELEASE(m_csSunShadow);
	_RELEASE(m_csSpotShadow);
	_RELEASE(m_csInject);
	_RELEASE(m_csLight);
	_RELEASE(m_csIntegrate);
	_RELEASE(m_vsApply);
	_RELEASE(m_psApply);
	_RELEASE(m_psApplyMsaa);

	_RELEASE(m_cbFrame);
	_RELEASE(m_lightBufSrv); _RELEASE(m_lightBuf);
	_RELEASE(m_shadowSpotBufSrv); _RELEASE(m_shadowSpotBuf);

	_RELEASE(m_smpLinearClamp);
	_RELEASE(m_smpShadowCmp);
	_RELEASE(m_blendApply);
	_RELEASE(m_dsDisabled);
	_RELEASE(m_rsNoCull);

	m_shadersReady = false;
	m_initAttempted = false;
	m_frameActive = false;
	m_volumeReadyThisFrame = false;
}

bool CVolumetricFroxels::EnsureShaders()
{
	if (m_shadersReady)
		return true;
	if (m_initAttempted)
		return false;
	m_initAttempted = true;

	auto loadCS = [](LPCSTR fn, ID3D11ComputeShader** outCS) -> bool
	{
		ID3DBlob* blob = nullptr;
		if (!CompileFroxelShader(fn, "cs_5_0", nullptr, &blob))
			return false;
		HRESULT hr = HW.pDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outCS);
		_RELEASE(blob);
		return SUCCEEDED(hr);
	};

	if (!loadCS("froxel_sun_shadow.cs", &m_csSunShadow) ||
		!loadCS("froxel_spot_shadow.cs", &m_csSpotShadow) ||
		!loadCS("froxel_inject.cs", &m_csInject) ||
		!loadCS("froxel_light.cs", &m_csLight) ||
		!loadCS("froxel_integrate.cs", &m_csIntegrate))
	{
		Destroy();
		m_initAttempted = true;
		return false;
	}

	{
		ID3DBlob* vsBlob = nullptr;
		if (!CompileFroxelShader("froxel_apply.vs", "vs_5_0", nullptr, &vsBlob) ||
			FAILED(HW.pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vsApply)))
		{
			_RELEASE(vsBlob);
			Destroy();
			m_initAttempted = true;
			return false;
		}
		_RELEASE(vsBlob);
	}

	{
		ID3DBlob* psBlob = nullptr;
		if (!CompileFroxelShader("froxel_apply.ps", "ps_5_0", nullptr, &psBlob) ||
			FAILED(HW.pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_psApply)))
		{
			_RELEASE(psBlob);
			Destroy();
			m_initAttempted = true;
			return false;
		}
		_RELEASE(psBlob);

		D3D_SHADER_MACRO msaaMacros[] = { { "USE_MSAA", "1" }, { nullptr, nullptr } };
		ID3DBlob* psMsaaBlob = nullptr;
		if (!CompileFroxelShader("froxel_apply.ps", "ps_5_0", msaaMacros, &psMsaaBlob) ||
			FAILED(HW.pDevice->CreatePixelShader(psMsaaBlob->GetBufferPointer(), psMsaaBlob->GetBufferSize(), nullptr, &m_psApplyMsaa)))
		{
			_RELEASE(psMsaaBlob);
			Destroy();
			m_initAttempted = true;
			return false;
		}
		_RELEASE(psMsaaBlob);
	}

	// Constant buffer
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = (sizeof(SFroxelFrameCB) + 15u) & ~15u;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(HW.pDevice->CreateBuffer(&bd, nullptr, &m_cbFrame)))
		{
			Destroy();
			m_initAttempted = true;
			return false;
		}
	}

	// StructuredBuffer: dynamic lights
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(SFroxelLightGPU) * MAX_DYNAMIC_LIGHTS;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(SFroxelLightGPU);
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_UNKNOWN;
		sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sd.Buffer.NumElements = MAX_DYNAMIC_LIGHTS;
		if (FAILED(HW.pDevice->CreateBuffer(&bd, nullptr, &m_lightBuf)) ||
			FAILED(HW.pDevice->CreateShaderResourceView(m_lightBuf, &sd, &m_lightBufSrv)))
		{
			Destroy();
			m_initAttempted = true;
			return false;
		}
	}

	// StructuredBuffer: shadowed spotlight batch
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(SFroxelShadowSpotGPU) * MAX_SHADOW_SPOTS;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(SFroxelShadowSpotGPU);
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = DXGI_FORMAT_UNKNOWN;
		sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sd.Buffer.NumElements = MAX_SHADOW_SPOTS;
		if (FAILED(HW.pDevice->CreateBuffer(&bd, nullptr, &m_shadowSpotBuf)) ||
			FAILED(HW.pDevice->CreateShaderResourceView(m_shadowSpotBuf, &sd, &m_shadowSpotBufSrv)))
		{
			Destroy();
			m_initAttempted = true;
			return false;
		}
	}

	// Samplers
	{
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		HW.pDevice->CreateSamplerState(&sd, &m_smpLinearClamp);

		D3D11_SAMPLER_DESC cmp{};
		cmp.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		cmp.AddressU = cmp.AddressV = cmp.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		cmp.BorderColor[0] = cmp.BorderColor[1] = cmp.BorderColor[2] = cmp.BorderColor[3] = 1.0f;
		cmp.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		cmp.MaxLOD = D3D11_FLOAT32_MAX;
		HW.pDevice->CreateSamplerState(&cmp, &m_smpShadowCmp);
	}

	// Fixed-function apply states: OutRGB = InScatter + SceneRGB * Transmittance
	{
		D3D11_BLEND_DESC bd{};
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
		HW.pDevice->CreateBlendState(&bd, &m_blendApply);

		D3D11_DEPTH_STENCIL_DESC dsd{};
		dsd.DepthEnable = FALSE;
		dsd.StencilEnable = FALSE;
		HW.pDevice->CreateDepthStencilState(&dsd, &m_dsDisabled);

		D3D11_RASTERIZER_DESC rsd{};
		rsd.FillMode = D3D11_FILL_SOLID;
		rsd.CullMode = D3D11_CULL_NONE;
		rsd.DepthClipEnable = FALSE;
		HW.pDevice->CreateRasterizerState(&rsd, &m_rsNoCull);
	}

	m_shadersReady = true;
	Msg("* [FROXEL] Native DX11 Compute 3D Froxel Volumetric Fog initialized.");
	return true;
}

bool CVolumetricFroxels::EnsureVolumeResources(int quality)
{
	quality = clampr(quality, 0, 2);
	if (m_currentQuality == quality && m_gridW > 0)
		return true;

	ReleaseVolumeResources();

	static const u32 kGridSizes[3][3] = {
		{ 128, 72, 48 },
		{ 160, 90, 64 },
		{ 240, 135, 96 },
	};

	const u32 w = kGridSizes[quality][0];
	const u32 h = kGridSizes[quality][1];
	const u32 d = kGridSizes[quality][2];

	if (!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_mediaTex, &m_mediaSrv, &m_mediaUav) ||
		!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R8_UNORM, &m_sunVisTex, &m_sunVisSrv, &m_sunVisUav) ||
		!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_spotAccumTex, &m_spotAccumSrv, &m_spotAccumUav) ||
		!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_lightTex[0], &m_lightSrv[0], &m_lightUav[0]) ||
		!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_lightTex[1], &m_lightSrv[1], &m_lightUav[1]) ||
		!CreateVolumeTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_integratedTex, &m_integratedSrv, &m_integratedUav))
	{
		ReleaseVolumeResources();
		Msg("! [FROXEL] Failed to allocate 3D volume textures (%ux%ux%u).", w, h, d);
		return false;
	}

	m_gridW = w;
	m_gridH = h;
	m_gridD = d;
	m_currentQuality = quality;
	m_historyValid = false;
	Msg("* [FROXEL] Allocated 3D froxel grid %ux%ux%u (quality=%d).", w, h, d, quality);
	return true;
}

void CVolumetricFroxels::UploadFrameCB()
{
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(HW.pContext->Map(m_cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		CopyMemory(mapped.pData, &m_cbData, sizeof(m_cbData));
		HW.pContext->Unmap(m_cbFrame, 0);
	}
}

void CVolumetricFroxels::BeginFrame(bool sunEnabled)
{
	m_frameActive = false;
	m_volumeReadyThisFrame = false;
	m_collectedLights.clear();

	if (!ps_r4_volumetric_froxels || !HW.pDevice || !HW.pContext)
		return;
	if (!EnsureShaders() || !EnsureVolumeResources(ps_r4_volumetric_quality))
		return;

	const auto& RV = Device.m_rt_view;
	const auto& RE = Device.m_rt_env;

	// Drop temporal history automatically on camera cuts / fast travel / level transitions
	if (m_historyValid && m_prevCamPos.distance_to_sqr(RV.vCameraPosition) > 625.0f)
		m_historyValid = false;
	m_prevCamPos = RV.vCameraPosition;

	Fmatrix viewProj;
	viewProj.mul(RV.mProject, RV.mView);
	Fmatrix invViewProj;
	invViewProj.invert(viewProj);

	m_cbData.mViewProj = viewProj;
	m_cbData.mInvViewProj = invViewProj;
	m_cbData.mPrevViewProj = m_historyValid ? m_prevViewProj : viewProj;
	m_cbData.mSunCascadeXform = Fidentity;

	const float zNear = 0.45f;
	const float zFar = clampr(ps_r4_volumetric_range, 30.0f, 250.0f);
	constexpr float depthPower = 2.15f;

	m_cbData.vCameraPos.set(RV.vCameraPosition.x, RV.vCameraPosition.y, RV.vCameraPosition.z, zNear);
	m_cbData.vCameraDir.set(RV.vCameraDirection.x, RV.vCameraDirection.y, RV.vCameraDirection.z, zFar);
	m_cbData.vGridParams.set(float(m_gridW), float(m_gridH), float(m_gridD), depthPower);

	const float temporal = m_historyValid ? clampr(ps_r4_volumetric_temporal, 0.0f, 0.96f) : 0.0f;
	m_cbData.vInvGridParams.set(1.0f / float(m_gridW), 1.0f / float(m_gridH), 1.0f / float(m_gridD), temporal);

	// Sun parameters from adapted sun light (world space)
	Fvector sunDir = { 0.f, -1.f, 0.f };
	Fvector sunCol = { 0.f, 0.f, 0.f };
	float sunActive = 0.0f;
	if (sunEnabled && RImplementation.Lights.sun_adapted)
	{
		light* sun = (light*)RImplementation.Lights.sun_adapted._get();
		sunDir = sun->direction;
		sunDir.normalize_safe();
		sunCol.set(sun->color.r, sun->color.g, sun->color.b);
		// Respect weather sunshafts / sun intensity so cloudy/night weather stays balanced
		const float lum = sunCol.x * 0.3f + sunCol.y * 0.59f + sunCol.z * 0.11f;
		if (lum > 0.002f)
			sunActive = 1.0f;
	}
	m_cbData.vSunDir.set(sunDir.x, sunDir.y, sunDir.z, sunActive);
	m_cbData.vSunColor.set(sunCol.x, sunCol.y, sunCol.z, clampr(ps_r4_volumetric_anisotropy, -0.5f, 0.92f));

	Fvector3 fogCol = { 0.3f, 0.32f, 0.35f };
	float fogDens = 0.2f, fogNear = 10.f, fogFar = 250.f, rainDens = 0.f;
	Fvector3 ambCol = { 0.05f, 0.05f, 0.05f };
	Fvector4 hemiCol = { 0.2f, 0.2f, 0.22f, 0.f };
	float windDir = 0.f, windVel = 1.f;
	if (RE.valid)
	{
		fogCol = RE.fog_color;
		fogDens = RE.fog_density;
		fogNear = RE.fog_near;
		fogFar = RE.fog_far;
		rainDens = RE.rain_density;
		ambCol = RE.ambient;
		hemiCol = RE.hemi_color;
		windDir = RE.wind_direction;
		windVel = RE.wind_velocity;
	}
	else if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
	{
		const CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
		fogCol = desc.fog_color;
		fogDens = desc.fog_density;
		fogNear = desc.fog_near;
		fogFar = desc.fog_far;
		rainDens = desc.rain_density;
		ambCol = desc.ambient;
		hemiCol = desc.hemi_color;
		windDir = desc.wind_direction;
		windVel = desc.wind_velocity;
	}

	m_cbData.vFogColor.set(fogCol.x, fogCol.y, fogCol.z, clampr(ps_r4_volumetric_density, 0.0f, 4.0f));
	m_cbData.vFogParams.set(fogDens, fogNear, fogFar, rainDens);
	m_cbData.vAmbientColor.set(ambCol.x, ambCol.y, ambCol.z, RV.fTimeGlobal);
	m_cbData.vHemiColor.set(hemiCol.x, hemiCol.y, hemiCol.z, windVel);
	m_cbData.vWindAndJitter.set(_sin(windDir), _cos(windDir), 0.5f, float(m_frameCounter & 63u));
	m_cbData.vCascadeAndLights.set(0.f, zFar, 0.f, 0.f);
	m_cbData.vScreenParams.set(float(RV.dwWidth), float(RV.dwHeight),
		RV.dwWidth ? 1.0f / float(RV.dwWidth) : 0.f, RV.dwHeight ? 1.0f / float(RV.dwHeight) : 0.f);
	m_cbData.vSunCascadeUVClip.set(1.0f / float(_max(RImplementation.o.smapsize, 512)), 0.0008f, 0.f, 0.55f);

	// Clear sun visibility to 1.0 (unoccluded default outside cascade bounds) and spot accumulation to 0.0
	const FLOAT clearOne[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const FLOAT clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	HW.pContext->ClearUnorderedAccessViewFloat(m_sunVisUav, sunActive > 0.5f ? clearOne : clearZero);
	HW.pContext->ClearUnorderedAccessViewFloat(m_spotAccumUav, clearZero);

	m_frameActive = true;
}

void CVolumetricFroxels::EvaluateSunCascade(u32 cascadeIdx, ID3D11ShaderResourceView* smapSrv,
	const Fmatrix& cascadeXform, float zMin, float zMax, float smapSize, float depthBias)
{
	if (!m_frameActive || !smapSrv || m_cbData.vSunDir.w < 0.5f)
		return;

	PIX_EVENT(FROXEL_SUN_CASCADE);

	// World-space -> shadowmap UV+Z matrix
	Fmatrix mTexelAdjust = {
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	};
	m_cbData.mSunCascadeXform.mul(mTexelAdjust, cascadeXform);
	m_cbData.vCascadeAndLights.x = zMin;
	m_cbData.vCascadeAndLights.y = zMax;
	m_cbData.vSunCascadeUVClip.x = 1.0f / _max(smapSize, 256.0f);
	m_cbData.vSunCascadeUVClip.y = _max(depthBias, 0.0006f);
	m_cbData.vSunCascadeUVClip.z = float(cascadeIdx);
	UploadFrameCB();

	// Unbind DSV temporarily so the shadowmap SRV has no DSV hazard while CS reads it
	ID3D11RenderTargetView* oldRtvs[8]{};
	ID3D11DepthStencilView* oldDsv = nullptr;
	HW.pContext->OMGetRenderTargets(8, oldRtvs, &oldDsv);
	ID3D11RenderTargetView* nullRtvs[8]{};
	HW.pContext->OMSetRenderTargets(8, nullRtvs, nullptr);

	HW.pContext->CSSetShader(m_csSunShadow, nullptr, 0);
	HW.pContext->CSSetConstantBuffers(0, 1, &m_cbFrame);
	HW.pContext->CSSetShaderResources(0, 1, &smapSrv);
	HW.pContext->CSSetSamplers(0, 1, &m_smpShadowCmp);
	HW.pContext->CSSetUnorderedAccessViews(0, 1, &m_sunVisUav, nullptr);

	HW.pContext->Dispatch((m_gridW + 7u) / 8u, (m_gridH + 7u) / 8u, (m_gridD + 3u) / 4u);

	UnbindComputeSlots();

	HW.pContext->OMSetRenderTargets(8, oldRtvs, oldDsv);
	for (auto* rtv : oldRtvs) _RELEASE(rtv);
	_RELEASE(oldDsv);
}

void CVolumetricFroxels::AccumulateShadowedSpots(const xr_vector<light*>& spots, ID3D11ShaderResourceView* atlasSrv)
{
	if (!m_frameActive || !atlasSrv || spots.empty())
		return;

	SFroxelShadowSpotGPU gpuSpots[MAX_SHADOW_SPOTS];
	u32 count = 0;

	const float smapSize = float(_max(RImplementation.o.smapsize, 512));
	const float fTexelOffs = 0.5f / smapSize;
	const Fvector& camPos = Device.m_rt_view.vCameraPosition;
	const float maxRange = m_cbData.vCameraDir.w;

	for (light* L : spots)
	{
		if (!L || count >= MAX_SHADOW_SPOTS)
			break;
		if (L->flags.type != IRender_Light::SPOT && L->flags.type != IRender_Light::OMNIPART)
			continue;

		const float dist = camPos.distance_to(L->position);
		if (dist > maxRange + L->range)
			continue;

		const float volScale = L->flags.bVolumetric ? clampr(L->m_volumetric_intensity * 1.4f, 0.25f, 3.0f) : 0.32f;
		const float lod = L->get_LOD();
		if (lod <= 0.01f)
			continue;

		const float view_dim = float(L->X.S.size - 2) / smapSize;
		const float view_sx = float(L->X.S.posX + 1) / smapSize;
		const float view_sy = float(L->X.S.posY + 1) / smapSize;
		if (view_dim <= 0.0f)
			continue;

		Fmatrix mTexelAdjust = {
			view_dim * 0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -view_dim * 0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			view_dim * 0.5f + view_sx + fTexelOffs, view_dim * 0.5f + view_sy + fTexelOffs, 0.0f, 1.0f
		};
		Fmatrix xfProject;
		xfProject.mul(mTexelAdjust, L->X.S.project);

		SFroxelShadowSpotGPU& dst = gpuSpots[count++];
		dst.mWorldToShadow.mul(xfProject, L->X.S.view);
		const float effRange = _max(L->range, 0.5f);
		dst.posRange.set(L->position.x, L->position.y, L->position.z, 1.0f / (effRange * effRange));
		dst.colorVol.set(L->color.r * volScale * lod, L->color.g * volScale * lod, L->color.b * volScale * lod, effRange);
		Fvector dir = L->direction;
		dir.normalize_safe();
		dst.dirCone.set(dir.x, dir.y, dir.z, _cos(L->cone * 0.5f));
		dst.atlasBounds.set(view_sx + fTexelOffs, view_sy + fTexelOffs,
			view_sx + view_dim - fTexelOffs, view_sy + view_dim - fTexelOffs);
	}

	if (count == 0)
		return;

	PIX_EVENT(FROXEL_SPOT_SHADOW);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(HW.pContext->Map(m_shadowSpotBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		CopyMemory(mapped.pData, gpuSpots, count * sizeof(SFroxelShadowSpotGPU));
		HW.pContext->Unmap(m_shadowSpotBuf, 0);
	}

	m_cbData.vCascadeAndLights.w = float(count);
	m_cbData.vSunCascadeUVClip.x = 1.0f / smapSize;
	UploadFrameCB();

	ID3D11RenderTargetView* oldRtvs[8]{};
	ID3D11DepthStencilView* oldDsv = nullptr;
	HW.pContext->OMGetRenderTargets(8, oldRtvs, &oldDsv);
	ID3D11RenderTargetView* nullRtvs[8]{};
	HW.pContext->OMSetRenderTargets(8, nullRtvs, nullptr);

	ID3D11ShaderResourceView* srvs[2] = { atlasSrv, m_shadowSpotBufSrv };
	HW.pContext->CSSetShader(m_csSpotShadow, nullptr, 0);
	HW.pContext->CSSetConstantBuffers(0, 1, &m_cbFrame);
	HW.pContext->CSSetShaderResources(0, 2, srvs);
	HW.pContext->CSSetSamplers(0, 1, &m_smpShadowCmp);
	HW.pContext->CSSetUnorderedAccessViews(0, 1, &m_spotAccumUav, nullptr);

	HW.pContext->Dispatch((m_gridW + 7u) / 8u, (m_gridH + 7u) / 8u, (m_gridD + 3u) / 4u);

	UnbindComputeSlots();

	HW.pContext->OMSetRenderTargets(8, oldRtvs, oldDsv);
	for (auto* rtv : oldRtvs) _RELEASE(rtv);
	_RELEASE(oldDsv);
}

void CVolumetricFroxels::CollectDynamicLights(const xr_vector<light*>& points, const xr_vector<light*>& spots)
{
	if (!m_frameActive)
		return;

	struct ScoredLight
	{
		float score;
		SFroxelLightGPU gpu;
	};
	xr_vector<ScoredLight> candidates;
	candidates.reserve(points.size() + spots.size());

	const Fvector& camPos = Device.m_rt_view.vCameraPosition;
	const float maxRange = m_cbData.vCameraDir.w;

	auto addLight = [&](light* L, bool isSpot)
	{
		if (!L || L->range < 0.25f)
			return;
		const float distSq = camPos.distance_to_sqr(L->position);
		const float cullR = maxRange + L->range;
		if (distSq > cullR * cullR)
			return;

		const float lum = L->color.r * 0.3f + L->color.g * 0.59f + L->color.b * 0.11f;
		if (lum <= 0.01f)
			return;

		const float volScale = L->flags.bVolumetric ? clampr(L->m_volumetric_intensity * 1.2f, 0.3f, 2.5f) : (isSpot ? 0.55f : 0.45f);
		ScoredLight item;
		item.score = (lum * L->range * volScale) / (1.0f + distSq * 0.04f);

		const float r = L->range;
		item.gpu.posRange.set(L->position.x, L->position.y, L->position.z, 1.0f / (r * r));
		item.gpu.colorVol.set(L->color.r, L->color.g, L->color.b, r);
		if (isSpot)
		{
			Fvector dir = L->direction;
			dir.normalize_safe();
			const float outerCos = _cos(L->cone * 0.5f);
			const float innerCos = _cos(L->cone * 0.35f);
			item.gpu.dirCone.set(dir.x, dir.y, dir.z, outerCos);
			item.gpu.spotParams.set(1.0f / _max(innerCos - outerCos, 1e-3f), volScale * 1.6f, 1.0f, 0.0f);
		}
		else
		{
			item.gpu.dirCone.set(0.f, -1.f, 0.f, -2.0f);
			item.gpu.spotParams.set(1.0f, volScale * 2.4f, 0.0f, 0.0f);
		}
		candidates.push_back(item);
	};

	for (light* L : points) addLight(L, false);
	for (light* L : spots) addLight(L, true);

	if (candidates.size() > MAX_DYNAMIC_LIGHTS)
	{
		std::nth_element(candidates.begin(), candidates.begin() + MAX_DYNAMIC_LIGHTS, candidates.end(),
			[](const ScoredLight& a, const ScoredLight& b) { return a.score > b.score; });
		candidates.resize(MAX_DYNAMIC_LIGHTS);
	}

	for (const auto& c : candidates)
	{
		if (m_collectedLights.size() >= MAX_DYNAMIC_LIGHTS)
			break;
		m_collectedLights.push_back(c.gpu);
	}
}

void CVolumetricFroxels::DispatchVolumePasses()
{
	if (!m_frameActive)
		return;

	PIX_EVENT(FROXEL_VOLUMETRIC_SOLVE);

	const u32 numLights = (u32)m_collectedLights.size();
	if (numLights > 0)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(HW.pContext->Map(m_lightBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			CopyMemory(mapped.pData, m_collectedLights.data(), numLights * sizeof(SFroxelLightGPU));
			HW.pContext->Unmap(m_lightBuf, 0);
		}
	}

	m_cbData.vCascadeAndLights.z = float(numLights);
	UploadFrameCB();

	const u32 gx = (m_gridW + 7u) / 8u;
	const u32 gy = (m_gridH + 7u) / 8u;
	const u32 gz = (m_gridD + 3u) / 4u;

	// Pass 1: Participating media injection (weather fog + height stratification + 3D wind noise)
	{
		PIX_EVENT(FROXEL_INJECT);
		HW.pContext->CSSetShader(m_csInject, nullptr, 0);
		HW.pContext->CSSetConstantBuffers(0, 1, &m_cbFrame);
		HW.pContext->CSSetUnorderedAccessViews(0, 1, &m_mediaUav, nullptr);
		HW.pContext->Dispatch(gx, gy, gz);
		UnbindComputeSlots();
	}

	// Pass 2: Unified volumetric lighting (Sun + shadowed spots + dynamic lights + sky ambient) + temporal reprojection
	const u32 curIdx = m_historyIndex;
	const u32 prevIdx = 1u - curIdx;
	{
		PIX_EVENT(FROXEL_LIGHT);
		ID3D11ShaderResourceView* srvs[5] = {
			m_mediaSrv,
			m_sunVisSrv,
			m_spotAccumSrv,
			m_lightSrv[prevIdx],
			m_lightBufSrv
		};
		HW.pContext->CSSetShader(m_csLight, nullptr, 0);
		HW.pContext->CSSetConstantBuffers(0, 1, &m_cbFrame);
		HW.pContext->CSSetShaderResources(0, 5, srvs);
		HW.pContext->CSSetSamplers(0, 1, &m_smpLinearClamp);
		HW.pContext->CSSetUnorderedAccessViews(0, 1, &m_lightUav[curIdx], nullptr);
		HW.pContext->Dispatch(gx, gy, gz);
		UnbindComputeSlots();
	}

	// Pass 3: Front-to-back analytical Beer-Lambert scattering & transmittance integration along Z
	{
		PIX_EVENT(FROXEL_INTEGRATE);
		HW.pContext->CSSetShader(m_csIntegrate, nullptr, 0);
		HW.pContext->CSSetConstantBuffers(0, 1, &m_cbFrame);
		HW.pContext->CSSetShaderResources(0, 1, &m_lightSrv[curIdx]);
		HW.pContext->CSSetUnorderedAccessViews(0, 1, &m_integratedUav, nullptr);
		HW.pContext->Dispatch(gx, gy, 1u);
		UnbindComputeSlots();
	}

	m_prevViewProj = m_cbData.mViewProj;
	m_historyValid = true;
	m_historyIndex = prevIdx;
	m_frameCounter++;
	m_volumeReadyThisFrame = true;
}

void CVolumetricFroxels::ApplyToScene(ID3D11RenderTargetView* targetRtv, ID3D11ShaderResourceView* posSrv,
	ID3D11ShaderResourceView* tonemapSrv, u32 width, u32 height, bool msaa)
{
	if (!m_volumeReadyThisFrame || !targetRtv || !posSrv || !tonemapSrv)
		return;

	PIX_EVENT(FROXEL_APPLY);

	m_cbData.vScreenParams.set(float(width), float(height),
		width ? 1.0f / float(width) : 0.f, height ? 1.0f / float(height) : 0.f);
	UploadFrameCB();

	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = float(width);
	vp.Height = float(height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	HW.pContext->RSSetViewports(1, &vp);

	HW.pContext->OMSetRenderTargets(1, &targetRtv, nullptr);
	const FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	HW.pContext->OMSetBlendState(m_blendApply, blendFactor, 0xffffffff);
	HW.pContext->OMSetDepthStencilState(m_dsDisabled, 0);
	HW.pContext->RSSetState(m_rsNoCull);

	HW.pContext->IASetInputLayout(nullptr);
	HW.pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	HW.pContext->VSSetShader(m_vsApply, nullptr, 0);
	HW.pContext->GSSetShader(nullptr, nullptr, 0);
	HW.pContext->HSSetShader(nullptr, nullptr, 0);
	HW.pContext->DSSetShader(nullptr, nullptr, 0);
	HW.pContext->PSSetShader(msaa ? m_psApplyMsaa : m_psApply, nullptr, 0);
	HW.pContext->PSSetConstantBuffers(0, 1, &m_cbFrame);

	ID3D11ShaderResourceView* srvs[3] = { m_integratedSrv, posSrv, tonemapSrv };
	HW.pContext->PSSetShaderResources(0, 3, srvs);
	HW.pContext->PSSetSamplers(0, 1, &m_smpLinearClamp);

	HW.pContext->Draw(3, 0);

	// Unbind SRVs and synchronize X-Ray's state caches so subsequent RCache passes rebind cleanly
	ID3D11ShaderResourceView* nullSrvs[3] = { nullptr, nullptr, nullptr };
	HW.pContext->PSSetShaderResources(0, 3, nullSrvs);
	HW.pContext->VSSetShader(nullptr, nullptr, 0);
	HW.pContext->PSSetShader(nullptr, nullptr, 0);

	StateManager.Reset();
	SRVSManager.ResetDeviceState();
	RCache.Invalidate();
}
