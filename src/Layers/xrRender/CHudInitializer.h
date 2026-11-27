#pragma once

class CHudInitializer {
public:
	CHudInitializer(bool setup);
	CHudInitializer(int setup);
	~CHudInitializer();

	void SetHudMode();
	void SetCamMode();
	void SetDefaultMode();
private:
	void set(int);
	int b_auto_setup;
	Fmatrix mView_saved;
	Fmatrix mProject_saved;
	Fmatrix mFullTransform_saved;
};
