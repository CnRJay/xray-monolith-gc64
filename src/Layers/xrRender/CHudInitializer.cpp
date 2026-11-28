#include "stdafx.h"

#include "CHudInitializer.h"

void CHudInitializer::set(int setup)
{
	b_auto_setup = setup;

	if (CRenderThread::IsOnRenderThread())
	{
		mView_saved = Device.m_rt_view.mView;
		mProject_saved = Device.m_rt_view.mProject;
		mFullTransform_saved = Device.m_rt_view.mFullTransform;
	}
	else
	{
		mView_saved = Device.mView;
		mProject_saved = Device.mProject;
		mFullTransform_saved = Device.mFullTransform;
	}
}

CHudInitializer::CHudInitializer(bool setup)
{
	set(setup);
	if (!b_auto_setup) return;

	SetHudMode();
}

CHudInitializer::CHudInitializer(int setup)
{
	set(setup);
	if (!b_auto_setup) return;

	switch (b_auto_setup)
	{
		case 1:
			SetHudMode();
			break;
		case 2:
			SetCamMode();
			break;
	}
}

CHudInitializer::~CHudInitializer()
{
	if (!b_auto_setup) return;

	SetDefaultMode();
}

void CHudInitializer::SetHudMode()
{
	if (CRenderThread::IsOnRenderThread())
	{
		auto& RV = Device.m_rt_view;
		RV.mView.set(RV.mViewHud);
		RV.mProject.set(RV.mProjectHud);
		RV.mFullTransform.set(RV.mFullTransformHud);

		Device.m_pRender->SetCacheXform(RV.mView, RV.mProject);
		Device.m_pRender->SetCacheXform_prev(RV.mViewHud_prev, RV.mProjectHud_prev);
		return;
	}

	Device.mView.set(Device.mViewHud);
	Device.mProject.set(Device.mProjectHud);
	Device.mFullTransform.set(Device.mFullTransformHud);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mViewHud_prev, Device.mProjectHud_prev);
}

void CHudInitializer::SetCamMode()
{
	if (CRenderThread::IsOnRenderThread())
	{
		auto& RV = Device.m_rt_view;
		RV.mView.set(RV.mViewCam);
		RV.mProject.set(RV.mProjectCam);
		RV.mFullTransform.set(RV.mFullTransformCam);

		Device.m_pRender->SetCacheXform(RV.mView, RV.mProject);
		Device.m_pRender->SetCacheXform_prev(RV.mViewCam_prev, RV.mProjectCam_prev);
		return;
	}

	Device.mView.set(Device.mViewCam);
	Device.mProject.set(Device.mProjectCam);
	Device.mFullTransform.set(Device.mFullTransformCam);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mViewCam_prev, Device.mProjectCam_prev);
}

void CHudInitializer::SetDefaultMode()
{
	if (CRenderThread::IsOnRenderThread())
	{
		auto& RV = Device.m_rt_view;
		RV.mView.set(mView_saved);
		RV.mProject.set(mProject_saved);
		RV.mFullTransform.set(mFullTransform_saved);

		Device.m_pRender->SetCacheXform(RV.mView, RV.mProject);
		Device.m_pRender->SetCacheXform_prev(RV.mView_prev, RV.mProject_prev);
		return;
	}

	Device.mView.set(mView_saved);
	Device.mProject.set(mProject_saved);
	Device.mFullTransform.set(mFullTransform_saved);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mView_prev, Device.mProject_prev);
}
