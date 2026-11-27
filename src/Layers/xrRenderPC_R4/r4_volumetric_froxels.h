#pragma once

class light;

class CVolumetricFroxels
{
public:
	static constexpr u32 MAX_DYNAMIC_LIGHTS = 64;
	static constexpr u32 MAX_SHADOW_SPOTS = 16;

	CVolumetricFroxels();
	~CVolumetricFroxels();

	void Destroy();
	bool IsReady() const { return m_shadersReady && m_gridW > 0; }
	void InvalidateHistory() { m_historyValid = false; }

	// Begin a new main-viewport froxel frame: update camera/weather CB, clear sun/spot accumulation volumes.
	void BeginFrame(bool sunEnabled);

	// Evaluate a single sun cascade shadowmap directly into the 3D sun-visibility volume while the SMAP is live.
	void EvaluateSunCascade(u32 cascadeIdx, ID3D11ShaderResourceView* smapSrv, const Fmatrix& cascadeXform,
		float zMin, float zMax, float smapSize, float depthBias);

	// Accumulate shadowed spotlights from the current LP_smap_pool atlas batch while rt_smap_depth holds their maps.
	void AccumulateShadowedSpots(const xr_vector<light*>& spots, ID3D11ShaderResourceView* atlasSrv);

	// Collect dynamic point and unshadowed spot lights for volumetric scattering in the lighting compute pass.
	void CollectDynamicLights(const xr_vector<light*>& points, const xr_vector<light*>& spots);

	// Execute media injection -> unified lighting + temporal reprojection -> front-to-back transmittance integration.
	void DispatchVolumePasses();

	// Composite the integrated 3D volumetric LUT onto the active scene color target with hardware Beer-Lambert blend.
	void ApplyToScene(ID3D11RenderTargetView* targetRtv, ID3D11ShaderResourceView* posSrv,
		ID3D11ShaderResourceView* tonemapSrv, u32 width, u32 height, bool msaa);

private:
	struct SFroxelFrameCB
	{
		Fmatrix mInvViewProj;
		Fmatrix mViewProj;
		Fmatrix mPrevViewProj;
		Fmatrix mSunCascadeXform;

		Fvector4 vCameraPos;
		Fvector4 vCameraDir;
		Fvector4 vGridParams;
		Fvector4 vInvGridParams;

		Fvector4 vSunDir;
		Fvector4 vSunColor;
		Fvector4 vFogColor;
		Fvector4 vFogParams;

		Fvector4 vAmbientColor;
		Fvector4 vHemiColor;
		Fvector4 vWindAndJitter;
		Fvector4 vCascadeAndLights;
		Fvector4 vScreenParams;
		Fvector4 vSunCascadeUVClip;
	};

	struct SFroxelLightGPU
	{
		Fvector4 posRange;
		Fvector4 colorVol;
		Fvector4 dirCone;
		Fvector4 spotParams;
	};

	struct SFroxelShadowSpotGPU
	{
		Fmatrix mWorldToShadow;
		Fvector4 posRange;
		Fvector4 colorVol;
		Fvector4 dirCone;
		Fvector4 atlasBounds;
	};

	bool EnsureShaders();
	bool EnsureVolumeResources(int quality);
	void ReleaseVolumeResources();
	void UploadFrameCB();

	bool m_initAttempted = false;
	bool m_shadersReady = false;
	bool m_frameActive = false;
	bool m_volumeReadyThisFrame = false;
	bool m_historyValid = false;

	int m_currentQuality = -1;
	u32 m_gridW = 0;
	u32 m_gridH = 0;
	u32 m_gridD = 0;
	u32 m_historyIndex = 0;
	u32 m_frameCounter = 0;

	Fmatrix m_prevViewProj = Fidentity;
	Fvector m_prevCamPos{ 0.f, 0.f, 0.f };
	SFroxelFrameCB m_cbData{};
	xr_vector<SFroxelLightGPU> m_collectedLights;

	// Shaders
	ID3D11ComputeShader* m_csSunShadow = nullptr;
	ID3D11ComputeShader* m_csSpotShadow = nullptr;
	ID3D11ComputeShader* m_csInject = nullptr;
	ID3D11ComputeShader* m_csLight = nullptr;
	ID3D11ComputeShader* m_csIntegrate = nullptr;
	ID3D11VertexShader* m_vsApply = nullptr;
	ID3D11PixelShader* m_psApply = nullptr;
	ID3D11PixelShader* m_psApplyMsaa = nullptr;

	// 3D Volume Textures & Views
	ID3D11Texture3D* m_mediaTex = nullptr;
	ID3D11ShaderResourceView* m_mediaSrv = nullptr;
	ID3D11UnorderedAccessView* m_mediaUav = nullptr;

	ID3D11Texture3D* m_sunVisTex = nullptr;
	ID3D11ShaderResourceView* m_sunVisSrv = nullptr;
	ID3D11UnorderedAccessView* m_sunVisUav = nullptr;

	ID3D11Texture3D* m_spotAccumTex = nullptr;
	ID3D11ShaderResourceView* m_spotAccumSrv = nullptr;
	ID3D11UnorderedAccessView* m_spotAccumUav = nullptr;

	ID3D11Texture3D* m_lightTex[2]{ nullptr, nullptr };
	ID3D11ShaderResourceView* m_lightSrv[2]{ nullptr, nullptr };
	ID3D11UnorderedAccessView* m_lightUav[2]{ nullptr, nullptr };

	ID3D11Texture3D* m_integratedTex = nullptr;
	ID3D11ShaderResourceView* m_integratedSrv = nullptr;
	ID3D11UnorderedAccessView* m_integratedUav = nullptr;

	// Buffers
	ID3D11Buffer* m_cbFrame = nullptr;
	ID3D11Buffer* m_lightBuf = nullptr;
	ID3D11ShaderResourceView* m_lightBufSrv = nullptr;
	ID3D11Buffer* m_shadowSpotBuf = nullptr;
	ID3D11ShaderResourceView* m_shadowSpotBufSrv = nullptr;

	// Samplers & Fixed-Function States
	ID3D11SamplerState* m_smpLinearClamp = nullptr;
	ID3D11SamplerState* m_smpShadowCmp = nullptr;
	ID3D11BlendState* m_blendApply = nullptr;
	ID3D11DepthStencilState* m_dsDisabled = nullptr;
	ID3D11RasterizerState* m_rsNoCull = nullptr;
};
