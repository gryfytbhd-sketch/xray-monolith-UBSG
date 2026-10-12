#pragma once
#include "weaponmagazined.h"

// include this just in case we want it
#include "WeaponAutomaticShotgun.h"
// use the same rocket owner as UBGL weapons without adding another weapon base
#include "RocketLauncher.h"

#include "script_export_space.h"

//class CWeaponFakeGrenade;

// this is fine because CWeaponAutomaticShotgun inherits CWeaponMagazined already, it just allows us to have tri-state reloads as well
// class CWeaponMagazinedWShotgun : public CWeaponAutomaticShotgun
class CWeaponMagazinedWShotgun : public CWeaponAutomaticShotgun, public CRocketLauncher
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
    // hold saved ammo until the base spawn has finished rebuilding the rifle magazine
    struct SLoadedAmmo
    {
        bool pending = false;
        bool shotgunMode = false;
        int elapsed = 0;
        u32 inactiveElapsed = 0;
        u8 type = 0;
        u8 inactiveType = 0;
        xr_vector<u8> shellTypes;
    } m_loadedAmmo;
    void RestoreShotgunAmmo(const xr_vector<u8>& types);
    void WriteShotgunAmmo(NET_Packet& P);
    virtual void Reload();


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
    // the secondary magazine belongs to whichever underbarrel is attached
    void LaunchGrenade();
    void EnsureGrenadeRocket();
    void UnloadRocket();
    void RefreshSecondaryAmmo(bool grenade);
    // use the existing fire point getters instead of a separate grenade muzzle helper
    // Fvector GetGrenadeFirePoint();
    int GetSecondaryCapacity() const { return m_secondaryIsGrenade ? 1 : iMagazineSizeShotgun; }
    bool HasUnderbarrel() const { return IsShotgunAttached() || IsGrenadeLauncherAttached(); }
    bool GetGrenadeLauncherMode() const { return m_bShotgunMode && m_secondaryIsGrenade; }

	virtual void OnStateSwitch(u32 S, u32 oldState);

	virtual void switch2_Reload();
	virtual void switch2_StartAim();
	virtual void switch2_EndAim();


	virtual void state_Fire(float dt);
	// the bullet firing path uses the second muzzle while the UBSG is selected
	virtual const Fvector& get_CurrentFirePoint() override
	{
		return m_bShotgunMode ? get_LastFP2() : inherited::get_CurrentFirePoint();
	}
	// virtual Fmatrix RayTransform() override
	// {
	//     return CWeapon::RayTransform(m_bShotgunMode);
	// }
	virtual Fmatrix RayTransform() override;
	virtual void OnShot();
	virtual void OnEvent(NET_Packet& P, u16 type);
	virtual void ReloadMagazine();
    // unload the fake grenade too when scripts or reload logic empty the UBGL chamber
    virtual void UnloadMagazine(bool spawn_ammo = true) override;

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
        // the old mode bit now selects either underbarrel but this script query still means shotgun only
        // return m_bShotgunMode;
		return m_bShotgunMode && !m_secondaryIsGrenade;
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
    bool m_secondaryIsGrenade = false;
    // grenade mode uses the existing muzzle settings rather than its own config fields
    // shared_str m_grenadeFireBone;
    // Fvector m_grenadeFireOffset;
    bool m_grenadeRocketPending = false;
    bool m_cancelPendingRocket = false;
    xr_vector<shared_str> m_shotgunAmmoTypes;
    xr_vector<shared_str> m_grenadeAmmoTypes;
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
