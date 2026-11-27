//---------------------------------------------------------------------------
#ifndef particle_actionsH
#define particle_actionsH


namespace PAPI
{
	// refs
	struct ParticleEffect;

	struct PARTICLES_API ParticleAction
	{
		enum
		{
			ALLOW_ROTATE = (1 << 1)
		};

		Flags32 m_Flags;
		PActionEnum type; // Type field
		ParticleAction() { m_Flags.zero(); }

		virtual void Execute(ParticleEffect* pe, const float dt, float& m_max) = 0;
		virtual void Transform(const Fmatrix& m) = 0;

		virtual void Load(IReader& F) =0;
		virtual void Save(IWriter& F) =0;
	};

	DEFINE_VECTOR(ParticleAction*, PAVec, PAVecIt);

	class ParticleActions : public intrusive_base
	{
		PAVec actions;
	public:
		IC ParticleActions();
		IC ~ParticleActions();

		IC void			clear();
        IC void         reserve(u32 capacity);

		IC void			append(ParticleAction* pa) { actions.push_back(pa); }
		IC bool			empty() { return	actions.empty(); }
		IC PAVecIt		begin() { return	actions.begin(); }
		IC PAVecIt		end() { return actions.end(); }
		IC int			size() { return (int)actions.size(); }
		IC void			resize(int cnt) { actions.resize(cnt); }
	};
};

IC PAPI::ParticleActions::ParticleActions()
{
	actions.reserve(8);
}

IC PAPI::ParticleActions::~ParticleActions()
{
	clear();
}

IC void PAPI::ParticleActions::clear()
{
	for (ParticleAction* pPAction : actions)
		xr_delete(pPAction);

	actions.clear();
}

IC void PAPI::ParticleActions::reserve(u32 capacity)
{
    actions.reserve(capacity);
}

//---------------------------------------------------------------------------
#endif
