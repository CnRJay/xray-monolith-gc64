#pragma once

#include "UIStatic.h"

class CUIStatic;

class CUIProgressShape : public CUIStatic
{
	friend class CUIXmlInit;
public:
	CUIProgressShape();
	virtual ~CUIProgressShape();
	void SetPos(int pos, int max);
	void SetPos(float pos);
	void SetTextVisible(bool b);

	virtual void Draw();

public:
	float m_stage;

	virtual CUIWindow* ui_cast_window() { return this; }
	virtual CUIStatic* ui_cast_static() { return this; }

protected:
	bool m_bClockwise;
	u32 m_sectorCount;
	bool m_bText;
	bool m_blend;

	float m_angle_begin;
	float m_angle_end;
};
