#pragma once

#include "UICustomEdit.h"

class CUIFrameWindow;

class CUIEditBoxEx : /*public CUIMultiTextureOwner, */public CUICustomEdit
{
public:
	CUIEditBoxEx();
	virtual ~CUIEditBoxEx();

	virtual void InitCustomEdit(Fvector2 pos, Fvector2 size);

	// CUIMultiTextureOwner
	virtual void InitTexture(LPCSTR texture);
	virtual void InitTextureEx(LPCSTR texture, LPCSTR shader);

	virtual CUIWindow* ui_cast_window() { return this; }
	virtual CUIStatic* ui_cast_static() { return this; }

protected:
	CUIFrameWindow* m_pFrameWindow;
};
