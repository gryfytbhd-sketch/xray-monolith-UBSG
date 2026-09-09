#pragma once
#include "weaponmagazined.h"

// include this just in case we want it
#include "WeaponAutomaticShotgun.h"

#include "script_export_space.h"

//class CWeaponFakeGrenade;

// this is fine because CWeaponAutomaticShotgun inherits CWeaponMagazined already, it just allows us to have tri-state reloads as well
class CWeaponMagazinedWShotgun : public CWeaponAutomaticShotgun
{
    // wrong inherited oops, should be automatic shotgun
	typedef CWeaponAutomaticShotgun inherited;
public:
	CWeaponMagazinedWShotgun(ESoundTypes eSoundType = SOUND_TYPE_WEAPON_SUBMACHINEGUN);
	virtual ~CWeaponMagazinedWShotgun();

	virtual void Load(LPCSTR section);
	void LoadLauncherKoeffs();
    void LoadShotgunParams();
    CWeapon ShotgunParams;
    void SwapWeaponParams();
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void net_Export(NET_Packet& P);
	virtual void net_Import(NET_Packet& P);

	virtual void OnH_B_Independent(bool just_before_destroy);

	virtual void save(NET_Packet& output_packet);
	virtual void load(IReader& input_packet);


	virtual bool Attach(PIItem pIItem, bool b_send_event);
	virtual bool Detach(LPCSTR item_section_name, bool b_spawn_item);
	virtual bool CanAttach(PIItem pIItem);
	virtual bool CanDetach(LPCSTR item_section_name);
	virtual void InitAddons();
	virtual bool UseScopeTexture();
	virtual float CurrentZoomFactor();
	virtual u8 GetCurrentHudOffsetIdx();
	virtual void FireEnd();
	void FireShotgun();

	virtual void OnStateSwitch(u32 S, u32 oldState);

	virtual void switch2_Reload();
	virtual void switch2_StartAim();
	virtual void switch2_EndAim();


	virtual void state_Fire(float dt);
	virtual void OnShot();
	virtual void OnEvent(NET_Packet& P, u16 type);
	virtual void ReloadMagazine();

	virtual bool Action(u16 cmd, u32 flags);

	//переключение в режим подствольника
	virtual bool SwitchMode(bool force = false);
	void PerformSwitchSG();
	void OnAnimationEnd(u32 state);
	virtual void OnMagazineEmpty();
	virtual bool GetBriefInfo(II_BriefInfo& info);

	virtual bool IsNecessaryItem(const shared_str& item_sect);
	virtual float Weight() const;
	//виртуальные функции для проигрывания анимации HUD
	virtual void PlayAnimShow();
	virtual void PlayAnimHide();
	virtual void PlayAnimReload();
	virtual void PlayAnimIdle();
	virtual void PlayAnimShoot();
	virtual void PlayAnimModeSwitch();
	virtual void PlayAnimFireModeSwitch();
	virtual bool TryPlayAnimBore();
	//for tri state reload
	virtual void switch2_StartReload();
	virtual void switch2_AddCartgidge();
	virtual void switch2_EndReload();

	virtual void PlayAnimOpenWeapon();
	virtual void PlayAnimAddOneCartridgeWeapon();
	virtual void PlayAnimCloseWeapon();
	//Script exports
	void SetAmmoElapsed2(int ammo_count);
	void AmmoTypeForEach2(const ::luabind::functor<bool>& funct);
	virtual void SetAmmoType2(u8 type) { m_ammoType2 = type; };
	u8 GetAmmoType2() { return m_ammoType2; };
	int GetAmmoCount2(u8 ammo2_type) const;

	IC int GetAmmoElapsed2() const
	{
		return iAmmoElapsed2;
	}

	IC int GetAmmoMagSize2() const
	{
		return iMagazineSize2;
	}

	IC bool GetShotgunMode() const
	{
		return m_bShotgunMode;
	}

	IC void SetShotgunMode(bool mode)
	{
		if (!IsShotgunAttached())
			return;

		if (mode != m_bShotgunMode)
			PerformSwitchSG();
	}



private:
	virtual void net_Spawn_install_upgrades(Upgrades_type saved_upgrades);
	virtual bool install_upgrade_impl(LPCSTR section, bool test);
	virtual bool install_upgrade_ammo_class(LPCSTR section, bool test);

public:
	//дополнительные параметры патронов 
	//для подствольника
	//-	CWeaponAmmo*			m_pAmmo2;
	xr_vector<shared_str> m_ammoTypes2;
	u8 m_ammoType2;

	int iMagazineSize2;
	xr_vector<CCartridge> m_magazine2;

	bool m_bShotgunMode;
	//adding this to track if it's mag fed or needs tri state reload
	bool m_bUBSGIsMagFed;

	CCartridge m_DefaultCartridge2;
	u8 iAmmoElapsed2;

    // ver, ammo variable for ammo capacity of ubsg (not defined before)
    int iMagazineSizeShotgun;

    // ambigious name but w/e
	virtual void UpdateShotgunVisibility(bool visibility);

    DECLARE_SCRIPT_REGISTER_FUNCTION

protected:
	void ApplyLauncherKoeffs();
	void ResetLauncherKoeffs();

};
