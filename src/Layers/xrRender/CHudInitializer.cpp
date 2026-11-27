#include "stdafx.h"

#include "CHudInitializer.h"

void CHudInitializer::set(int setup)
{
	b_auto_setup = setup;

	mView_saved = Device.mView;
	mProject_saved = Device.mProject;
	mFullTransform_saved = Device.mFullTransform;
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
	Device.mView.set(Device.mViewHud);
	Device.mProject.set(Device.mProjectHud);
	Device.mFullTransform.set(Device.mFullTransformHud);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mViewHud_prev, Device.mProjectHud_prev);
}

void CHudInitializer::SetCamMode()
{
	Device.mView.set(Device.mViewCam);
	Device.mProject.set(Device.mProjectCam);
	Device.mFullTransform.set(Device.mFullTransformCam);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mViewCam_prev, Device.mProjectCam_prev);
}

void CHudInitializer::SetDefaultMode()
{
	Device.mView.set(mView_saved);
	Device.mProject.set(mProject_saved);
	Device.mFullTransform.set(mFullTransform_saved);

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.m_pRender->SetCacheXform_prev(Device.mView_prev, Device.mProject_prev);
}
