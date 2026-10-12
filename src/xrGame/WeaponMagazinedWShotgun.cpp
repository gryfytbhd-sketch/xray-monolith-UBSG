#include "stdafx.h"
#include "weaponmagazinedwshotgun.h"
#include "weaponshotgun.h"
#include "entity.h"
#include "ParticlesObject.h"
#include "xrserver_objects_alife_items.h"
#include "Actor.h"
#include "xr_level_controller.h"
#include "level.h"
#include "object_broker.h"
#include "game_base_space.h"
#include "../xrphysics/MathUtils.h"
#include "player_hud.h"
#include "../build_config_defines.h"
#include "GrenadeLauncher.h"
#include "ExplosiveRocket.h"

#ifdef DEBUG
#	include "phdebug.h"
#endif

// to do: load underbarrel shotgun coefficients and swap them with the guns coefficients during swap (stuff like accuracy,
// rpm, hit_impulse, hit_power (damage), fire_modes, fire_distance, bullet_speed (most important ones) and etc if we want more
// (hit_type?) (overheat?) (silencer??) (zoom_rotate_time?) (recoil stats?) )
// also include shell_point, shell_dir, shell_particles variables in the weapon hud config for the shotgun (also technically the underbarrel attachment
// doesnt need to be a shotgun if we do it right)
// maybe one smart solution is to ask for a separate section from the main weapon section that just contains the shotgun params (might break something but is very simple)
// okay this wont work ^^, loading CWeapon also loads hud params, but we can still manually load them here.

// maybe this will cause issues since CWeaponAutomaticShotgun doesnt take ESoundType but eh
CWeaponMagazinedWShotgun::CWeaponMagazinedWShotgun(ESoundTypes eSoundType) : CWeaponAutomaticShotgun()
{
	m_ammoType2 = 0;
	m_bShotgunMode = false;
}

CWeaponMagazinedWShotgun::~CWeaponMagazinedWShotgun()
{
}

extern BOOL g_aimmode_remember;

void CWeaponMagazinedWShotgun::Load(LPCSTR section)
{
	inherited::Load(section);
	// use the existing muzzle settings instead of separate grenade config keys
	// m_grenadeFireBone = READ_IF_EXISTS(pSettings, r_string, HudSection(), "grenade_fire_bone",
	//     pSettings->r_string(HudSection(), "fire_bone"));
	// m_grenadeFireOffset = READ_IF_EXISTS(pSettings, r_fvector3, HudSection(), "grenade_fire_point",
	//     pSettings->r_fvector3(HudSection(), "fire_point"));

    // restore the rocket component for the grenade launcher option
	CRocketLauncher::Load(section);
    if (m_eGrenadeLauncherStatus != ALife::eAddonDisabled)
    {
        m_sounds.LoadSound(section, "snd_shoot_grenade", "sndShotG", true, m_eSoundShot);
        m_sounds.LoadSound(section, "snd_reload_grenade", "sndReloadG", true, m_eSoundReload);
        m_sFlameParticles2 = pSettings->r_string(section, "grenade_flame_particles");
    }

	//// Sounds
	m_sounds.LoadSound(section, "snd_shoot_shotgun", "sndShotS", true, m_eSoundShot);
	m_sounds.LoadSound(section, "snd_reload_shotgun", "sndReloadS", true, m_eSoundReload);
	m_sounds.LoadSound(section, "snd_switch", "sndSwitch", true, m_eSoundReload);
	//maybe repurpose that line for UBSG shooting particle
	//m_sFlameParticles2 = pSettings->r_string(section, "grenade_flame_particles");
	//add an ltx parameter to know if mag fed or not
	m_bUBSGIsMagFed = READ_IF_EXISTS(pSettings, r_bool, section, "ubsg_mag_fed", false);
    m_bTriStateReload = false;
    if (!m_bUBSGIsMagFed)
    {
        m_sounds.LoadSound(section, "snd_open_weapon", "sndOpenS", false, m_eSoundOpen);
        m_sounds.LoadSound(section, "snd_add_cartridge", "sndAddCartridgeS", false, m_eSoundAddCartridge);
        m_sounds.LoadSound(section, "snd_close_weapon", "sndCloseS", false, m_eSoundClose);
        m_sounds.LoadSound(section, "snd_close_weapon_empty", "sndCloseEmptyS", false, m_eSoundClose);
    }
	if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
	{
        // permanent launchers get their speed from the weapon config
		CRocketLauncher::m_fLaunchSpeed = pSettings->r_float(section, "grenade_vel");
	}

    // koeffs is a base multiplication on the normal gun mode's params
	LoadLauncherKoeffs();

    // load shotgun params
    LoadShotgunParams();

	// load ammo classes SECOND (grenade_class)
	m_ammoTypes2.clear();
    // better name
	LPCSTR S = pSettings->r_string(section, "ammo_class_sg");
	if (S && S[0])
	{
		string128 _ammoItem;
		int count = _GetItemCount(S);
		for (int it = 0; it < count; ++it)
		{
			_GetItem(S, it, _ammoItem);
			m_ammoTypes2.push_back(_ammoItem);
		}
	}

	iMagazineSize2 = iMagazineSize;
    iMagazineSizeShotgun = pSettings->r_s32(section, "ammo_mag_size_s");
    // keep both ammo lists available but only one secondary magazine can be fitted
    m_shotgunAmmoTypes = m_ammoTypes2;
    m_grenadeAmmoTypes.clear();
    if (m_eGrenadeLauncherStatus != ALife::eAddonDisabled)
    {
        LPCSTR grenades = pSettings->r_string(section, "grenade_class");
        for (int i = 0, count = _GetItemCount(grenades); i < count; ++i)
        {
            string128 ammo;
            _GetItem(grenades, i, ammo);
            m_grenadeAmmoTypes.push_back(ammo);
        }
        R_ASSERT2(!m_grenadeAmmoTypes.empty(), "W_WMAGSG UBGL needs grenade_class");
        if (m_eShotgunStatus != ALife::eAddonDisabled)
            R_ASSERT2(m_sGrenadeLauncherName != m_sShotgunName, "UBGL and UBSG need different addon sections");
    }
    R_ASSERT2(!(IsShotgunAttached() && IsGrenadeLauncherAttached()), "Only one underbarrel can be attached");
    RefreshSecondaryAmmo(IsGrenadeLauncherAttached());
}

// switch ammo lists only after the old underbarrel has been unloaded or during state restoration
void CWeaponMagazinedWShotgun::RefreshSecondaryAmmo(bool grenade)
{
    if (m_shotgunAmmoTypes.empty() || grenade == m_secondaryIsGrenade)
        return;
    if (m_bShotgunMode)
        PerformSwitchSG();
    m_secondaryIsGrenade = grenade;
    m_ammoTypes2 = grenade ? m_grenadeAmmoTypes : m_shotgunAmmoTypes;
    R_ASSERT(!m_ammoTypes2.empty());
    m_magazine2.clear();
    m_ammoType2 = 0;
    iAmmoElapsed2 = 0;
    m_DefaultCartridge2.Load(m_ammoTypes2[0].c_str(), 0, m_APk);
    m_BriefInfo_CalcFrame = 0;
}

void CWeaponMagazinedWShotgun::net_Destroy()
{
	// clear the pending marker when the level tears down the rocket owner
	m_grenadeRocketPending = false;
	m_cancelPendingRocket = false;
	inherited::net_Destroy();
}

// previous spawn code before restoring both magazines and shell types
// BOOL CWeaponMagazinedWShotgun::net_Spawn(CSE_Abstract* DC)
// {
// 	CSE_ALifeItemWeapon* const weapon = smart_cast<CSE_ALifeItemWeapon*>(DC);
// 	R_ASSERT(weapon);
// 	if (IsGameTypeSingle())
// 	{
// 		inherited::net_Spawn_install_upgrades(weapon->m_upgrades);
// 	}
//
// 	BOOL l_res = inherited::net_Spawn(DC);
//
//     UpdateShotgunVisibility(!!iAmmoElapsed);
// 	SetPending(FALSE);
//
// 	iAmmoElapsed2 = weapon->a_elapsed_grenades.grenades_count;
// 	m_ammoType2 = weapon->a_elapsed_grenades.grenades_type;
//
// 	m_DefaultCartridge2.Load(m_ammoTypes2[m_ammoType2].c_str(), m_ammoType2);
//
// 	if (!IsGameTypeSingle())
// 	{
//         // we need an alternative to getRocketCount
// 		//if (!m_bShotgunMode && IsGrenadeLauncherAttached() && !getRocketCount() && iAmmoElapsed2)
//         if (!m_bShotgunMode && IsGrenadeLauncherAttached() && !m_magazine2.size() && iAmmoElapsed2)
// 		{
// 			m_magazine2.push_back(m_DefaultCartridge2);
//
// 			shared_str grenade_name = m_DefaultCartridge2.m_ammoSect;
// 			shared_str fake_grenade_name = pSettings->r_string(grenade_name, "fake_grenade_name");
//
//             // we need to make an alternative to reload the shotgun server side to replace this
// 			// CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
// 		}
// 	}
// 	else
// 	{
// 		xr_vector<CCartridge>* pM = NULL;
//         // we need an alternative to getRocketCount
// 		bool b_if_grenade_mode = (m_bShotgunMode && iAmmoElapsed && !m_magazine2.size());
//         //bool b_if_grenade_mode = (m_bShotgunMode && iAmmoElapsed && !getRocketCount());
// 		if (b_if_grenade_mode)
// 			pM = &m_magazine;
//
// 		bool b_if_simple_mode = (!m_bShotgunMode && m_magazine2.size());
// 		if (b_if_simple_mode)
// 			pM = &m_magazine2;
//
// 		//if (b_if_grenade_mode || b_if_simple_mode)
// 		//{
// 		//	shared_str fake_grenade_name = pSettings->r_string(pM->back().m_ammoSect, "fake_grenade_name");
//
//   //          // we need to make an alternative to reload the shotgun server side to replace this
// 		//	// CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
// 		//}
// 	}
// 	return l_res;
// }

BOOL CWeaponMagazinedWShotgun::net_Spawn(CSE_Abstract* DC)
{
    CSE_ALifeItemWeaponMagazinedWSG* const weapon = smart_cast<CSE_ALifeItemWeaponMagazinedWSG*>(DC);
    R_ASSERT(weapon);
    if (IsGameTypeSingle())
        inherited::net_Spawn_install_upgrades(weapon->m_upgrades);

    const u16 activeCount = weapon->a_elapsed;
    const u8 activeType = weapon->ammo_type;
    // the base spawn expects rifle ammo even when the save has the underbarrel selected
    // restore both magazines after it finishes rebuilding the rifle magazine
    if (weapon->m_bShotgunMode)
    {
        weapon->a_elapsed = weapon->m_bHasAmmoData ? weapon->m_inactiveAmmoElapsed : 0;
        weapon->ammo_type = weapon->m_bHasAmmoData ? weapon->m_inactiveAmmoType : 0;
    }
    if (weapon->ammo_type >= m_ammoTypes.size())
        weapon->ammo_type = 0;
    const BOOL result = inherited::net_Spawn(DC);
    weapon->a_elapsed = activeCount;
    weapon->ammo_type = activeType;

    const bool mode = m_loadedAmmo.pending ? m_loadedAmmo.shotgunMode : weapon->m_bShotgunMode;
    const int count = m_loadedAmmo.pending ? m_loadedAmmo.elapsed : activeCount;
    const u8 type = m_loadedAmmo.pending ? m_loadedAmmo.type : activeType;
    const u32 inactiveCount = m_loadedAmmo.pending ? m_loadedAmmo.inactiveElapsed :
        (weapon->m_bHasAmmoData ? weapon->m_inactiveAmmoElapsed : weapon->a_elapsed_grenades.grenades_count);
    const u8 inactiveType = m_loadedAmmo.pending ? m_loadedAmmo.inactiveType :
        (weapon->m_bHasAmmoData ? weapon->m_inactiveAmmoType : weapon->a_elapsed_grenades.grenades_type);

    m_ammoType = mode ? inactiveType : type;
    m_ammoType2 = mode ? type : inactiveType;
    if (m_ammoType >= m_ammoTypes.size()) m_ammoType = 0;
    if (m_ammoType2 >= m_ammoTypes2.size()) m_ammoType2 = 0;
    m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType, m_APk);
    m_DefaultCartridge2.Load(m_ammoTypes2[m_ammoType2].c_str(), m_ammoType2, m_APk);
    m_magazine.clear();
    m_magazine2.clear();
    SetAmmoElapsed(mode ? int(_min(inactiveCount, u32(iMagazineSize2))) : clampr(count, 0, iMagazineSize2));
    // use the capacity of the installed attachment while rebuilding its magazine
    // SetAmmoElapsed2(mode ? clampr(count, 0, iMagazineSizeShotgun) : int(_min(inactiveCount, u32(iMagazineSizeShotgun))));
    SetAmmoElapsed2(mode ? clampr(count, 0, GetSecondaryCapacity()) : int(_min(inactiveCount, u32(GetSecondaryCapacity()))));

    if (m_loadedAmmo.pending)
        RestoreShotgunAmmo(m_loadedAmmo.shellTypes);
    else if (weapon->m_bHasAmmoData && !m_loadedAmmo.pending)
        RestoreShotgunAmmo(weapon->m_shotgunAmmoIDs);

    // a saved secondary mode can now belong to either attachment
    // if (mode && IsShotgunAttached())
    if (mode && HasUnderbarrel())
        PerformSwitchSG();
    m_loadedAmmo.pending = false;
    m_loadedAmmo.shellTypes.clear();
    SetPending(FALSE);
    UpdateShotgunVisibility(!!iAmmoElapsed);
    EnsureGrenadeRocket();
    return result;
}

// apply the selected shell type before the inherited reload checks inventory ammo
void CWeaponMagazinedWShotgun::Reload()
{
    // only shells use the per-cartridge ammo selection path
    // if (m_bShotgunMode && !m_bUBSGIsMagFed && m_set_next_ammoType_on_reload != undefined_ammo_type)
    if (GetShotgunMode() && !m_bUBSGIsMagFed && m_set_next_ammoType_on_reload != undefined_ammo_type)
    {
        if (m_set_next_ammoType_on_reload < m_ammoTypes.size())
            m_ammoType = m_set_next_ammoType_on_reload;
        m_set_next_ammoType_on_reload = undefined_ammo_type;
    }
    inherited::Reload();
}

//going to try some stuff
/*void CWeaponMagazinedWShotgun::switch2_Reload()
{
	VERIFY(GetState() == eReload);
	if (m_bShotgunMode)
	{
		m_needReload = true;
		PlaySound("sndReloadS", get_LastFP2());
		PlayHUDMotion("anm_reload_s", TRUE, this, GetState());
		SetPending(TRUE);
	}
	else
		inherited::switch2_Reload();
}*/

void CWeaponMagazinedWShotgun::switch2_Reload()
{
	VERIFY(GetState() == eReload);
	if (m_bShotgunMode)
	{
		// grenades always use one reload animation and never enter the shell reload states
		if (GetGrenadeLauncherMode())
		{
			m_needReload = true;
			PlaySound("sndReloadG", get_LastFP2());
			PlayHUDMotion("anm_reload_g", TRUE, this, GetState());
			SetPending(TRUE);
			return;
		}
		//if magfed then play single reload
		if (m_bUBSGIsMagFed)
		{
			m_needReload = true;
			PlaySound("sndReloadS", get_LastFP2());
			// the magazine-fed shotgun has its own reload slot
			// PlayHUDMotion("anm_reload_g", TRUE, this, GetState());
			PlayHUDMotion("anm_reload_sg", TRUE, this, GetState());
			SetPending(TRUE);
		}
		else
		{
			//tri state reload stuff i'm not sure i understand

            // should be this instead i think
			TriStateReload();
		}
	}
	else
	{
		inherited::switch2_Reload();
	}
}
//Setting up anim and sound refs for tri state reload for UBSG
//Maybe change anim and sound names tho

// okay so this function never actually gets called, only the CWeaponAutomaticShotgun version gets called. So all these functions below are useless to us. aside
// from maybe having another class inheriting from here. plus i think just using the inherited version of the code is fine...?
void CWeaponMagazinedWShotgun::switch2_StartReload()
{
    if (m_bShotgunMode)
    {
        BeginReloadWasEmpty = !m_magazine.size();
        PlaySound("sndOpenS", get_LastFP2());
        VERIFY(GetState() == eReload);
        ClickInterruptFlag = false; 
        PlayHUDMotion("anm_reload_w_sg_open", TRUE, this, GetState(), 1.f, 0.f, false);
        
        SetPending(TRUE);
    }
    else
    {
        inherited::switch2_StartReload();
    }
}

void CWeaponMagazinedWShotgun::switch2_AddCartgidge()
{
    if (m_bShotgunMode)
    {
        PlaySound("sndAddCartridgeS", get_LastFP2());
        VERIFY(GetState() == eReload);
        PlayHUDMotion("anm_reload_w_sg_add_cartridge", FALSE, this, GetState());
        
        SetPending(TRUE);
    }
    else
    {
        inherited::switch2_AddCartgidge();
    }
}

//i think those are needed for it to not crash (i'm just trying to get smth to compile rn)
void CWeaponMagazinedWShotgun::switch2_StartAim()
{
    inherited::switch2_StartAim();
}

void CWeaponMagazinedWShotgun::switch2_EndAim()
{
    inherited::switch2_EndAim();
}

void CWeaponMagazinedWShotgun::PlayAnimOpenWeapon()
{
    inherited::PlayAnimShow();
}

void CWeaponMagazinedWShotgun::PlayAnimAddOneCartridgeWeapon()
{
    PlayAnimReload();
}

void CWeaponMagazinedWShotgun::PlayAnimCloseWeapon()
{
    inherited::PlayAnimHide();
}


void CWeaponMagazinedWShotgun::switch2_EndReload()
{
    if (m_bShotgunMode)
    {
        SetPending(FALSE);
        if (BeginReloadWasEmpty && m_sounds.FindSoundItem("sndCloseEmptyS", false))
            PlaySound("sndCloseEmptyS", get_LastFP2());
        else
            PlaySound("sndCloseS", get_LastFP2());

        VERIFY(GetState() == eReload);
        ClickInterruptFlag = false;
        
        if (BeginReloadWasEmpty && HudAnimationExist("anm_reload_w_sg_close_empty"))
            PlayHUDMotion("anm_reload_w_sg_close_empty", FALSE, this, GetState());
        else
            PlayHUDMotion("anm_reload_w_sg_close", FALSE, this, GetState());
    }
    else
    {
        inherited::switch2_EndReload();
    }
}

void CWeaponMagazinedWShotgun::OnStateSwitch(u32 S, u32 oldState)
{
    
    if (!m_bTriStateReload || S != eReload)
    {
        switch (S)
        {
        case eSwitch:
        {
            if (!SwitchMode())
            {
                SwitchState(eIdle);
                return;
            }
        }
        break;
        }
        inherited::OnStateSwitch(S, oldState);
        // refresh the loaded grenade mesh during reload and state changes
        UpdateShotgunVisibility(!!iAmmoElapsed || S == eReload);
        return;
    }

    CWeapon::OnStateSwitch(S, oldState);

    if (m_magazine.size() == (u32)iMagazineSize || !HaveCartridgeInInventory(1))
    {
        switch2_EndReload();
        m_sub_state = eSubstateReloadEnd;
        return;
    };

    switch (m_sub_state)
    {
    case eSubstateReloadBegin:
        if (HaveCartridgeInInventory(1))
            switch2_StartReload();
        break;
    case eSubstateReloadInProcess:
        if (HaveCartridgeInInventory(1))
            switch2_AddCartgidge();
        break;
    case eSubstateReloadEnd:
        switch2_EndReload();
        break;
    case eSubstateReloadInProcessEmptyEnd:
        switch2_EndReload();
        break;
    };
}

void CWeaponMagazinedWShotgun::OnShot()
{
	if (m_bShotgunMode)
	{
		PlayAnimShoot();
		PlaySound("sndShotS", get_LastFP2());
		AddShotEffector();
		StartFlameParticles2();

#ifndef EXTENDED_WEAPON_CALLBACKS
		CGameObject* object = smart_cast<CGameObject*>(H_Parent());
		if (object)
			object->callback(GameObject::eOnWeaponFired)(object->lua_game_object(), this->lua_game_object(), iAmmoElapsed);
#endif
	}
	else
		inherited::OnShot();
}

void CWeaponMagazinedWShotgun::PlayAnimFireModeSwitch()
{
	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
		if (!m_bShotgunMode)
		{
			if (!m_bHasDifferentFireModes) return;
			if (m_aFireModes.size() <= 1) return;
			if (GetState() != eIdle) return;

			if (HudAnimationExist("anm_switch_mode_w_gl"))
			{
				SetPending(TRUE);
				iAmmoElapsed == 0 && HudAnimationExist("anm_switch_mode_w_gl_empty")
					? PlayHUDMotion("anm_switch_mode_w_gl_empty", TRUE, this, eSwitchMode)
					: PlayHUDMotion("anm_switch_mode_w_gl", TRUE, this, eSwitchMode);
			}
			else
				UpdateFireMode();

			if (m_sounds.FindSoundItem("sndSwitchMode", false))
				PlaySound("sndSwitchMode", get_LastFP());
		}

		return;
	}

	if (IsShotgunAttached())
	{
		if (!m_bShotgunMode)
		{
			if (!m_bHasDifferentFireModes) return;
			if (m_aFireModes.size() <= 1) return;
			if (GetState() != eIdle) return;

			if (HudAnimationExist("anm_switch_mode_w_sg"))
			{
				SetPending(TRUE);
				iAmmoElapsed == 0 && HudAnimationExist("anm_switch_mode_w_sg_empty")
					? PlayHUDMotion("anm_switch_mode_w_sg_empty", TRUE, this, eSwitchMode)
					: PlayHUDMotion("anm_switch_mode_w_sg", TRUE, this, eSwitchMode);
			}
			else
				UpdateFireMode();

			if (m_sounds.FindSoundItem("sndSwitchMode", false))
				PlaySound("sndSwitchMode", get_LastFP());
		}
	}
	else
	{
		inherited::PlayAnimFireModeSwitch();
	}
}

bool CWeaponMagazinedWShotgun::SwitchMode(bool force)
{
	bool bUsefulStateToSwitch = (eIdle == GetState() && !IsPending());

	if (!force && !bUsefulStateToSwitch)
		return false;

	// either attachment can select the shared secondary magazine
	// if (!IsShotgunAttached())
	if (!HasUnderbarrel())
		return false;

	//OnZoomOut();

	SetPending(TRUE);

	PerformSwitchSG();

	PlaySound("sndSwitch", get_LastFP());

	PlayAnimModeSwitch();

	m_BriefInfo_CalcFrame = 0;

	return true;
}

void CWeaponMagazinedWShotgun::SwapWeaponParams()
{
    // okay here we swap all the params with one another

    // recoil
    swap(cam_recoil, m_shotgun_params.cam_recoil);
    swap(zoom_cam_recoil, m_shotgun_params.zoom_cam_recoil);

    // other params
    swap(m_crosshair_inertion, m_shotgun_params.crosshair_inertion);
    swap(m_zoom_params.m_fZoomRotateTime, m_shotgun_params.zoom_rotate_time);
    swap(fHitImpulse, m_shotgun_params.hit_impulse);

    swap(fireDistance, m_shotgun_params.fire_distance);
    swap(m_fStartBulletSpeed, m_shotgun_params.bullet_speed);
    swap(fOneShotTime, m_shotgun_params.fOneShotTime);

    swap(fireDispersionBase, m_shotgun_params.fire_dispersion_base);

    // damage is a little weird
    // say temp is 1, other is 0.5. 
    float temp = GetHitPower();
    swap(temp, m_shotgun_params.l_fHitPower);
    SetHitPower(temp);

}

extern BOOL useSeparateUBGLKeybind;
extern BOOL g_launcher_dynamic_range_zoom;
void CWeaponMagazinedWShotgun::PerformSwitchSG()
{
	m_bShotgunMode = !m_bShotgunMode;
    // only use shell-by-shell reload when the non-mag-fed UBSG is selected, not for the rifle
    // m_bTriStateReload = m_bShotgunMode && !m_bUBSGIsMagFed;
    m_bTriStateReload = GetShotgunMode() && !m_bUBSGIsMagFed;

	if (useSeparateUBGLKeybind) {
		u8 newzoomtype = 0;

		if (g_aimmode_remember)
		{
			newzoomtype = zoomTypeBeforeLauncher;
		}

		SetZoomType(m_bShotgunMode ? 2 : newzoomtype);
	} else {
		SetZoomType(m_bShotgunMode ? 2 : 0);
	}	

	UpdateUIScope();

    // change magazine size to the correct amount
	// grenades have one chamber while the shotgun keeps its configured capacity
	// iMagazineSize = m_bShotgunMode ? iMagazineSizeShotgun : iMagazineSize2;
	iMagazineSize = m_bShotgunMode ? GetSecondaryCapacity() : iMagazineSize2;

	m_ammoTypes.swap(m_ammoTypes2);

	swap(m_ammoType, m_ammoType2);
	swap(m_DefaultCartridge, m_DefaultCartridge2);

    // grenade mode must never borrow shotgun recoil or damage values
    // SwapWeaponParams();
    if (!m_secondaryIsGrenade)
        SwapWeaponParams();

	m_magazine.swap(m_magazine2);
	iAmmoElapsed = (int)m_magazine.size();
	// both counts must follow the swapped magazines so the inactive ammo query stays correct
	iAmmoElapsed2 = u8(m_magazine2.size());
	m_set_next_ammoType_on_reload = undefined_ammo_type;
}


void CWeaponMagazinedWShotgun::SetAmmoElapsed2(int ammo_count)
{
	iAmmoElapsed2 = ammo_count;

	u32 uAmmo = u32(iAmmoElapsed2);

	if (uAmmo != m_magazine2.size())
	{
		if (uAmmo > m_magazine2.size())
		{
			CCartridge l_cartridge;
			l_cartridge.Load(m_ammoTypes2[m_ammoType2].c_str(), m_ammoType2, m_APk);
			while (uAmmo > m_magazine2.size())
				m_magazine2.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine2.size())
				m_magazine2.pop_back();
		};
	};
}

void CWeaponMagazinedWShotgun::AmmoTypeForEach2(const ::luabind::functor<bool> &funct)
{
	for (u8 i = 0; i < u8(m_ammoTypes2.size()); ++i)
	{
		if (funct(i, *m_ammoTypes2[i]))
			break;
	}
}

bool CWeaponMagazinedWShotgun::Action(u16 cmd, u32 flags)
{
    // grenades launch once per trigger press while the UBSG keeps the normal bullet firing path
    if (GetGrenadeLauncherMode() && cmd == kWPN_FIRE)
    {
        if (IsPending()) return false;
        if (ParentIsActor() && Actor()->is_safemode())
        {
            Actor()->set_safemode(false);
            if (iAmmoElapsed) return false;
        }
        if (flags & CMD_START)
        {
            if (iAmmoElapsed) LaunchGrenade();
            else Reload();
        }
        return true;
    }
	//if (m_bShotgunMode && cmd == kWPN_FIRE)
	//{
	//	if (IsPending())
	//		return false;

	//	if (ParentIsActor() && Actor()->is_safemode())
	//	{
	//		Actor()->set_safemode(false);

	//		if (iAmmoElapsed)
	//			return false;
	//	}

	//	if (flags & CMD_START)
	//	{
	//		if (iAmmoElapsed)
 //               FireShotgun();
	//		else
	//			Reload();

	//		if (GetState() == eIdle)
	//			OnEmptyClick();
	//	}
	//	return true;
	//}
	if (inherited::Action(cmd, flags))
		return true;

	/*switch (cmd)
	{
	case kWPN_FUNC:
	{
	    if (flags&CMD_START && !IsPending())
	        SwitchState(eSwitch);
	    return true;
	}
	}*/
	return false;
}

#include "inventory.h"
#include "inventoryOwner.h"

void CWeaponMagazinedWShotgun::state_Fire(float dt)
{
	VERIFY(fOneShotTime > 0.f);
    // the rocket action handles grenade firing so never generate bullet hits for VOG ammo
    if (GetGrenadeLauncherMode()) return;

	//режим стрельбы подствольника
	if (m_bShotgunMode)
	{
        inherited::state_Fire(dt);
		/*
		fTime					-=dt;
		while (fTime<=0 && (iAmmoElapsed>0) && (IsWorking() || m_bFireSingleShot))
		{
		++m_iShotNum;
		OnShot			();

		// Ammo
		if(Local())
		{
		VERIFY				(m_magazine.size());
		m_magazine.pop_back	();
		--iAmmoElapsed;

		VERIFY((u32)iAmmoElapsed == m_magazine.size());
		}
		}
		UpdateSounds				();
		if(m_iShotNum == m_iQueueSize)
		FireEnd();
		*/
	}
		//режим стрельбы очередями
	else
		inherited::state_Fire(dt);
}

// restore rocket ownership events instead of only playing effects for an untracked rocket
// void CWeaponMagazinedWShotgun::OnEvent(NET_Packet& P, u16 type)
// {
// 	inherited::OnEvent(P, type);
// 	u16 id;
// 	switch (type)
// 	{
// 	case GE_OWNERSHIP_TAKE:
// 		{
//             // basically a reload function
// 			//P.r_u16(id);
// 			//CRocketLauncher::AttachRocket(id, this);
// 		}
// 		break;
// 	case GE_OWNERSHIP_REJECT:
// 	case GE_LAUNCH_ROCKET:
// 		{
//             // our fire case
// 			bool bLaunch = (type == GE_LAUNCH_ROCKET); // ?? maybe made sense originally and then edited to not make sense
// 			//P.r_u16(id);
// 			//CRocketLauncher::DetachRocket(id, bLaunch);
// 			if (bLaunch)
// 			{
// 				PlayAnimShoot();
// 				PlaySound("sndShotG", get_LastFP2());
// 				AddShotEffector();
// 				StartFlameParticles2();
// 			}
// 			break;
// 		}
// 	}
// }

void CWeaponMagazinedWShotgun::OnEvent(NET_Packet& packet, u16 type)
{
    inherited::OnEvent(packet, type);
    switch (type)
    {
    case GE_OWNERSHIP_TAKE:
        CRocketLauncher::AttachRocket(packet.r_u16(), this);
        m_grenadeRocketPending = false;
        // a late spawn may arrive after the grenade was unloaded or the UBGL detached
        if (m_cancelPendingRocket || !IsGrenadeLauncherAttached() || (m_bShotgunMode ? m_magazine.empty() : m_magazine2.empty()))
            UnloadRocket();
        m_cancelPendingRocket = false;
        UpdateShotgunVisibility(true);
        break;
    case GE_OWNERSHIP_REJECT:
    case GE_LAUNCH_ROCKET:
    {
        const bool launch = type == GE_LAUNCH_ROCKET;
        CRocketLauncher::DetachRocket(packet.r_u16(), launch);
        if (launch)
        {
            PlayAnimShoot();
            PlaySound("sndShotG", get_LastFP2());
            AddShotEffector();
            StartFlameParticles2();
        }
        // a replacement ammo type may have finished loading while the old rocket was being rejected
        if (!launch) EnsureGrenadeRocket();
        UpdateShotgunVisibility(true);
        break;
    }
    }
}

// the grenade launch now uses the engine fire points directly
// Fvector CWeaponMagazinedWShotgun::GetGrenadeFirePoint()
// {
//     if (!GetHUDmode()) return get_LastFP2();
//     attachable_hud_item* item = HudItemData();
//     item->update(false);
//     const u16 bone = item->m_model->LL_BoneID(m_grenadeFireBone);
//     R_ASSERT2(bone != BI_NONE, "grenade_fire_bone is missing from the HUD model");
//     R_ASSERT2(item->m_model->LL_GetBoneVisible(bone), "grenade_fire_bone must not be on a hidden attachment branch");
//     Fvector point;
//     item->m_model->LL_GetTransform(bone).transform_tiny(point, m_grenadeFireOffset);
//     item->m_item_transform.transform_tiny(point);
//     return point;
// }

Fmatrix CWeaponMagazinedWShotgun::RayTransform()
{
    if (GetGrenadeLauncherMode() && GetHUDmode())
    {
        attachable_hud_item* item = HudItemData();
        // use the normal HUD muzzle because the shotgun branch is hidden with a UBGL fitted
        // const u16 bone = item->m_model->LL_BoneID(m_grenadeFireBone);
        const u16 bone = item->m_measures.m_fire_bone;
        R_ASSERT2(bone != BI_NONE, "grenade_fire_bone is missing from the HUD model");
        R_ASSERT2(item->m_model->LL_GetBoneVisible(bone), "grenade_fire_bone must stay visible while using the UBGL");
        // return CWeapon::RayTransform(true, bone, &m_grenadeFireOffset);
        return CWeapon::RayTransform(false);
    }
    // rifle and shotgun still use their original muzzle selection
    return CWeapon::RayTransform(m_bShotgunMode);
}

// the same explosive rocket ownership and launch path used by ordinary UBGL weapons

// use the same optional ballistic aiming setting as ordinary UBGL weapons
extern BOOL g_launcher_dynamic_range_zoom;
void CWeaponMagazinedWShotgun::LaunchGrenade()
{
	if (!getRocketCount()) return;
	R_ASSERT(GetGrenadeLauncherMode());
	{
#ifdef CROCKETLAUNCHER_CHANGE
		LPCSTR ammo_name = m_ammoTypes[m_ammoType].c_str();
		float launch_speed = READ_IF_EXISTS(pSettings, r_float, ammo_name, "ammo_grenade_vel", CRocketLauncher::m_fLaunchSpeed);
#endif
		Fvector p1, d;
		// use the visible rifle HUD muzzle and the normal UBGL world fire point
		// p1.set(get_LastFP2());
		// p1.set(GetGrenadeFirePoint());
		p1.set(GetHUDmode() ? get_LastFP() : get_LastFP2());
		d.set(get_LastFD());
		CEntity* E = smart_cast<CEntity*>(H_Parent());

		if (E)
		{
			CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
			if (NULL == io->inventory().ActiveItem())
			{
				Log("current_state", GetState());
				Log("next_state", GetNextState());
				Log("item_sect", cNameSect().c_str());
				Log("H_Parent", H_Parent()->cNameSect().c_str());
			}
			E->g_fireParams(this, p1, d);
		}
		if (IsGameTypeSingle())
			// p1.set(get_LastFP2());
			// p1.set(GetGrenadeFirePoint());
			p1.set(GetHUDmode() ? get_LastFP() : get_LastFP2());

		// catch invalid launch data here instead of letting a bad velocity reach explosion physics later
		R_ASSERT2(_valid(p1), "WSG invalid grenade launch position");
		R_ASSERT2(_valid(d) && d.square_magnitude() > EPS_S, "WSG invalid grenade launch direction");
#ifdef CROCKETLAUNCHER_CHANGE
		R_ASSERT2(_valid(launch_speed) && launch_speed > 0.f, "WSG invalid grenade launch speed");
#endif

		Fmatrix launch_matrix;
		launch_matrix.identity();
		launch_matrix.k.set(d);
		Fvector::generate_orthonormal_basis(launch_matrix.k,
		                                    launch_matrix.j,
		                                    launch_matrix.i);

		launch_matrix.c.set(p1);

		if (IsGameTypeSingle() && IsZoomed() && smart_cast<CActor*>(H_Parent()) && g_launcher_dynamic_range_zoom)
		{
			H_Parent()->setEnabled(FALSE);
			setEnabled(FALSE);

			collide::rq_result RQ;
			BOOL HasPick = Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);

			setEnabled(TRUE);
			H_Parent()->setEnabled(TRUE);

			if (HasPick)
			{
				Fvector Transference;
				Transference.mul(d, RQ.range);
				Fvector res[2];
#ifdef		DEBUG
				//.				DBG_OpenCashedDraw();
				//.				DBG_DrawLine(p1,Fvector().add(p1,d),D3DCOLOR_XRGB(255,0,0));
#endif
#ifdef CROCKETLAUNCHER_CHANGE
				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, launch_speed, EffectiveGravity(), res);
#else
				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference,
				                                                CRocketLauncher::m_fLaunchSpeed,
				                                                EffectiveGravity(),
				                                                res);
#endif
#ifdef DEBUG
				//.				if(canfire0>0)DBG_DrawLine(p1,Fvector().add(p1,res[0]),D3DCOLOR_XRGB(0,255,0));
				//.				if(canfire0>1)DBG_DrawLine(p1,Fvector().add(p1,res[1]),D3DCOLOR_XRGB(0,0,255));
				//.				DBG_ClosedCashedDraw(30000);
#endif

				if (canfire0 != 0)
				{
					d = res[0];
				};
			}
		};

		d.normalize();
#ifdef CROCKETLAUNCHER_CHANGE
		d.mul(launch_speed);
#else
		d.mul(CRocketLauncher::m_fLaunchSpeed);
#endif
		VERIFY2(_valid(launch_matrix), "CWeaponMagazinedWShotgun::SwitchState. Invalid launch_matrix!");
		R_ASSERT2(_valid(d) && d.square_magnitude() > EPS_S, "WSG invalid grenade velocity after ballistic aiming");
		CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);

		CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
		VERIFY(pGrenade);
		pGrenade->SetInitiator(H_Parent()->ID());

		if (Local() && OnServer())
		{
			VERIFY(m_magazine.size());
			m_magazine.pop_back();
			--iAmmoElapsed;
			VERIFY((u32) iAmmoElapsed == m_magazine.size());

			NET_Packet P;
			u_EventGen(P, GE_LAUNCH_ROCKET, ID());
			P.w_u16(getCurrentRocket()->ID());
			u_EventSend(P);
		};
	}
}

// create one fake grenade entity for the loaded cartridge and wait for its ownership event
void CWeaponMagazinedWShotgun::EnsureGrenadeRocket()
{
    if (!IsGrenadeLauncherAttached() || !m_secondaryIsGrenade || getRocketCount() || m_grenadeRocketPending)
        return;
    const auto& magazine = m_bShotgunMode ? m_magazine : m_magazine2;
    if (magazine.empty() || !OnServer()) return;
    const shared_str section = pSettings->r_string(magazine.back().m_ammoSect, "fake_grenade_name");
    m_grenadeRocketPending = true;
    CRocketLauncher::SpawnRocket(section, this);
}

// request rocket removal and let ownership events remove the pointers
void CWeaponMagazinedWShotgun::UnloadRocket()
{
    if (m_grenadeRocketPending) m_cancelPendingRocket = true;
    xr_vector<u16> ids;
    for (auto* rocket : m_rockets) ids.push_back(rocket->ID());
    for (u16 id : ids)
    {
        NET_Packet packet;
        u_EventGen(packet, GE_OWNERSHIP_REJECT, ID());
        packet.w_u16(id);
        u_EventSend(packet);
    }
}

void CWeaponMagazinedWShotgun::FireShotgun()
{
    // main thing, just use FireStart from CWeaponMagazined since we edit the main ammo type here; not sure if this works actually but w/e
    inherited::FireStart();

//	if (!getRocketCount()) return;
//	R_ASSERT(m_bGrenadeMode);
//	{
//#ifdef CROCKETLAUNCHER_CHANGE
//		LPCSTR ammo_name = m_ammoTypes[m_ammoType].c_str();
//		float launch_speed = READ_IF_EXISTS(pSettings, r_float, ammo_name, "ammo_grenade_vel", CRocketLauncher::m_fLaunchSpeed);
//#endif
//		Fvector p1, d;
//		p1.set(get_LastFP2());
//		d.set(get_LastFD());
//		CEntity* E = smart_cast<CEntity*>(H_Parent());
//
//		if (E)
//		{
//			CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
//			if (NULL == io->inventory().ActiveItem())
//			{
//				Log("current_state", GetState());
//				Log("next_state", GetNextState());
//				Log("item_sect", cNameSect().c_str());
//				Log("H_Parent", H_Parent()->cNameSect().c_str());
//			}
//			E->g_fireParams(this, p1, d);
//		}
//		if (IsGameTypeSingle())
//			p1.set(get_LastFP2());
//
//		Fmatrix launch_matrix;
//		launch_matrix.identity();
//		launch_matrix.k.set(d);
//		Fvector::generate_orthonormal_basis(launch_matrix.k,
//		                                    launch_matrix.j,
//		                                    launch_matrix.i);
//
//		launch_matrix.c.set(p1);
//
//		if (IsGameTypeSingle() && IsZoomed() && smart_cast<CActor*>(H_Parent()) && g_launcher_dynamic_range_zoom)
//		{
//			H_Parent()->setEnabled(FALSE);
//			setEnabled(FALSE);
//
//			collide::rq_result RQ;
//			BOOL HasPick = Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);
//
//			setEnabled(TRUE);
//			H_Parent()->setEnabled(TRUE);
//
//			if (HasPick)
//			{
//				Fvector Transference;
//				Transference.mul(d, RQ.range);
//				Fvector res[2];
//#ifdef		DEBUG
//				//.				DBG_OpenCashedDraw();
//				//.				DBG_DrawLine(p1,Fvector().add(p1,d),D3DCOLOR_XRGB(255,0,0));
//#endif
//#ifdef CROCKETLAUNCHER_CHANGE
//				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, launch_speed, EffectiveGravity(), res);
//#else
//				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference,
//				                                                CRocketLauncher::m_fLaunchSpeed,
//				                                                EffectiveGravity(),
//				                                                res);
//#endif
//#ifdef DEBUG
//				//.				if(canfire0>0)DBG_DrawLine(p1,Fvector().add(p1,res[0]),D3DCOLOR_XRGB(0,255,0));
//				//.				if(canfire0>1)DBG_DrawLine(p1,Fvector().add(p1,res[1]),D3DCOLOR_XRGB(0,0,255));
//				//.				DBG_ClosedCashedDraw(30000);
//#endif
//
//				if (canfire0 != 0)
//				{
//					d = res[0];
//				};
//			}
//		};
//
//		d.normalize();
//#ifdef CROCKETLAUNCHER_CHANGE
//		d.mul(launch_speed);
//#else
//		d.mul(CRocketLauncher::m_fLaunchSpeed);
//#endif
//		VERIFY2(_valid(launch_matrix), "CWeaponMagazinedWShotgun::SwitchState. Invalid launch_matrix!");
//		CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);
//
//		CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
//		VERIFY(pGrenade);
//		pGrenade->SetInitiator(H_Parent()->ID());

        // we still have a use for this though
		if (Local() && OnServer())
		{
			VERIFY(m_magazine.size());
			m_magazine.pop_back();
			--iAmmoElapsed;
			VERIFY((u32) iAmmoElapsed == m_magazine.size());

            // we need to make an alternative to delete the shotgun bullet server side to replace this
			//NET_Packet P;
			//u_EventGen(P, GE_LAUNCH_ROCKET, ID());
			//P.w_u16(getCurrentRocket()->ID());
			//u_EventSend(P);
		};
	//}
}

void CWeaponMagazinedWShotgun::FireEnd()
{
	if (m_bShotgunMode)
	{
		CWeapon::FireEnd();
	}
	else
		inherited::FireEnd();
}

void CWeaponMagazinedWShotgun::OnMagazineEmpty()
{
	if (GetState() == eIdle)
	{
		OnEmptyClick();
	}
}

// replace the old fake grenade lookup in the shotgun reload with the actual UBGL rocket path
// void CWeaponMagazinedWShotgun::ReloadMagazine()
// {
// 	inherited::ReloadMagazine();
//
// 	//перезарядка подствольного гранатомета
//     //"reloading the under-barrel grenade launcher"
//
//     // we need an alternative to shotgun ammo tracking and an alternative to getRocketCount
//     //if (iAmmoElapsed && !getRocketCount() && m_bShotgunMode)
//     if (iAmmoElapsed && !m_magazine2.size() && m_bShotgunMode)
// 	{
// 		shared_str fake_grenade_name = pSettings->r_string(m_ammoTypes[m_ammoType].c_str(), "fake_grenade_name");
//
//         // adds a grenade on the server side
//         // we need to make an alternative to reload the shotgun server side to replace this
// 		// CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
// 	}
// }
//
/*void CWeaponMagazinedWShotgun::OnStateSwitch(u32 S, u32 oldState)
{
	switch (S)
	{
	case eSwitch:
		{
			if (!SwitchMode())
			{
				SwitchState(eIdle);
				return;
			}
		}
		break;
	}

	inherited::OnStateSwitch(S, oldState);
    // use the correct name
    UpdateShotgunVisibility(!!iAmmoElapsed || S == eReload);
}*/

void CWeaponMagazinedWShotgun::ReloadMagazine()
{
    inherited::ReloadMagazine();
    EnsureGrenadeRocket();
}


void CWeaponMagazinedWShotgun::UnloadMagazine(bool spawn_ammo)
{
    if (GetGrenadeLauncherMode()) UnloadRocket();
    inherited::UnloadMagazine(spawn_ammo);
    UpdateShotgunVisibility(false);
}

void CWeaponMagazinedWShotgun::OnAnimationEnd(u32 state)
{
	switch (state)
	{
	case eSwitch:
		{
			SetPending(FALSE);
			SwitchState(eIdle);
		}
		break;
	case eFire:
		{
			//if (m_bShotgunMode)
			//	Reload();
		}
		break;
	}
	inherited::OnAnimationEnd(state);
}

void CWeaponMagazinedWShotgun::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);

	SetPending(FALSE);
	if (m_bShotgunMode)
	{
		SetState(eIdle);
		SetPending(FALSE);
	}
}

// both launcher items use the same class so match their sections and reject an occupied slot before falling through
// bool CWeaponMagazinedWShotgun::CanAttach(PIItem pIItem)
// {
//     CGrenadeLauncher* pShotgun = smart_cast<CGrenadeLauncher*>(pIItem);
//
// 	if (pShotgun &&
// 		ALife::eAddonAttachable == m_eShotgunStatus &&
// 		0 == (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonShotgun) &&
// 		!xr_strcmp(*m_sShotgunName, pIItem->object().cNameSect()))
// 		return true;
// 	else
// 		return inherited::CanAttach(pIItem);
// }

bool CWeaponMagazinedWShotgun::CanAttach(PIItem item)
{
    if (!item) return false;
    CGrenadeLauncher* addon = smart_cast<CGrenadeLauncher*>(item);
    if (!addon) return inherited::CanAttach(item);
    if (HasUnderbarrel() || IsPending() || (GetState() != eIdle && GetState() != eHidden))
        return false;
    const shared_str section = item->object().cNameSect();
    return (m_eShotgunStatus == ALife::eAddonAttachable && section == m_sShotgunName) ||
        (m_eGrenadeLauncherStatus == ALife::eAddonAttachable && section == m_sGrenadeLauncherName);
}

// do not detach a permanent launcher or remove one while a firing or reload animation is still using it
// bool CWeaponMagazinedWShotgun::CanDetach(LPCSTR item_section_name)
// {
// 	if (ALife::eAddonAttachable == m_eShotgunStatus &&
// 		0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonShotgun) &&
// 		!xr_strcmp(*m_sShotgunName, item_section_name))
// 		return true;
// 	else
// 		return inherited::CanDetach(item_section_name);
// }

bool CWeaponMagazinedWShotgun::CanDetach(LPCSTR section)
{
    const bool shotgun = !xr_strcmp(section, *m_sShotgunName);
    const bool grenade = !xr_strcmp(section, *m_sGrenadeLauncherName);
    if (!shotgun && !grenade) return inherited::CanDetach(section);
    if (IsPending() || (GetState() != eIdle && GetState() != eHidden)) return false;
    return (shotgun && m_eShotgunStatus == ALife::eAddonAttachable && IsShotgunAttached()) ||
        (grenade && m_eGrenadeLauncherStatus == ALife::eAddonAttachable && IsGrenadeLauncherAttached());
}

// enforce mutual exclusion again in the real attach operation
// bool CWeaponMagazinedWShotgun::Attach(PIItem pIItem, bool b_send_event)
// {
//     // there is no CShotgun class, we need to make a new class for the UBSG item or reuse the grenade launcher class
//     CGrenadeLauncher* pShotgun = smart_cast<CGrenadeLauncher*>(pIItem);
//
// 	if (pShotgun &&
// 		ALife::eAddonAttachable == m_eShotgunStatus &&
// 		0 == (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonShotgun) &&
// 		!xr_strcmp(*m_sShotgunName, pIItem->object().cNameSect()))
// 	{
// 		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonShotgun;
//
//
// 		//уничтожить подствольник из инвентаря
// 		if (b_send_event)
// 		{
// 			if (OnServer())
// 				pIItem->object().DestroyObject();
// 		}
// 		InitAddons();
// 		UpdateAddonsVisibility();
//
// 		if (GetState() == eIdle)
// 			PlayAnimIdle();
//
// 		return true;
// 	}
// 	else
// 		return inherited::Attach(pIItem, b_send_event);
// }

bool CWeaponMagazinedWShotgun::Attach(PIItem item, bool sendEvent)
{
    if (!item) return false;
    CGrenadeLauncher* addon = smart_cast<CGrenadeLauncher*>(item);
    if (!addon) return inherited::Attach(item, sendEvent);
    if (!CanAttach(item)) return false;
    const bool grenade = item->object().cNameSect() == m_sGrenadeLauncherName;
    m_flagsAddOnState |= grenade ? CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher :
        CSE_ALifeItemWeapon::eWeaponAddonShotgun;
    // read attachment launch speed through its public getter
    // if (grenade) CRocketLauncher::m_fLaunchSpeed = addon->m_fGrenadeVel;
    if (grenade) CRocketLauncher::m_fLaunchSpeed = addon->GetGrenadeVel();
    InitAddons();
    UpdateAddonsVisibility();
    if (sendEvent && OnServer()) item->object().DestroyObject();
    if (GetState() == eIdle) PlayAnimIdle();
    return true;
}

// return the old underbarrel ammo before clearing its flag so a later attachment cannot inherit the wrong magazine
// bool CWeaponMagazinedWShotgun::Detach(LPCSTR item_section_name, bool b_spawn_item)
// {
// 	if (ALife::eAddonAttachable == m_eShotgunStatus &&
// 		0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonShotgun) &&
// 		!xr_strcmp(*m_sShotgunName, item_section_name))
// 	{
// 		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonShotgun;
//
// 		if (!m_bShotgunMode)
// 		{
// 			PerformSwitchSG();
// 		}
// 		UnloadMagazine();
// 		PerformSwitchSG();
//
// 		UpdateAddonsVisibility();
//
// 		if (GetState() == eIdle)
// 			PlayAnimIdle();
//
// 		InitAddons();
//
// 		return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
// 	}
// 	else
// 		return inherited::Detach(item_section_name, b_spawn_item);
// }

bool CWeaponMagazinedWShotgun::Detach(LPCSTR section, bool spawnItem)
{
    const bool shotgun = !xr_strcmp(section, *m_sShotgunName);
    const bool grenade = !xr_strcmp(section, *m_sGrenadeLauncherName);
    if (!shotgun && !grenade) return inherited::Detach(section, spawnItem);
    if (!CanDetach(section)) return false;
    // keep the attachment kind until its ammo and firing parameters have been restored
    if (!m_bShotgunMode) PerformSwitchSG();
    UnloadMagazine();
    PerformSwitchSG();
    m_flagsAddOnState &= ~(grenade ? CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher :
        CSE_ALifeItemWeapon::eWeaponAddonShotgun);
    m_set_next_ammoType_on_reload = undefined_ammo_type;
    InitAddons();
    UpdateAddonsVisibility();
    if (GetState() == eIdle) PlayAnimIdle();
    return CInventoryItemObject::Detach(section, spawnItem);
}

// rebuild the secondary ammo list when the attachment kind changes and reject conflicting restored addon flags
// void CWeaponMagazinedWShotgun::InitAddons()
// {
// 	inherited::InitAddons();
//
// 	if (ShotgunAttachable())
// 	{
//         // typo, should be IsShotgunAttached instead of IsShotgunAttachable
// 		if (IsShotgunAttached())
// 		{
// 			ApplyLauncherKoeffs();
// 		}
// 		else
// 		{
// 			ResetLauncherKoeffs();
// 		}
// 	}
// }

void CWeaponMagazinedWShotgun::InitAddons()
{
    inherited::InitAddons();
    R_ASSERT2(!(IsShotgunAttached() && IsGrenadeLauncherAttached()), "Only one underbarrel can be attached");
    RefreshSecondaryAmmo(IsGrenadeLauncherAttached());
    if (HasUnderbarrel()) ApplyLauncherKoeffs();
    else ResetLauncherKoeffs();
}

void CWeaponMagazinedWShotgun::LoadLauncherKoeffs()
{
	if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable)
	{
		LPCSTR sect = GetGrenadeLauncherName().c_str();
		m_launcher_koef.cam_dispersion = READ_IF_EXISTS(pSettings, r_float, sect, "cam_dispersion_k", 1.0f);
		m_launcher_koef.cam_disper_inc = READ_IF_EXISTS(pSettings, r_float, sect, "cam_dispersion_inc_k", 1.0f);
		m_launcher_koef.pdm_base = READ_IF_EXISTS(pSettings, r_float, sect, "PDM_disp_base_k", 1.0f);
		m_launcher_koef.pdm_accel = READ_IF_EXISTS(pSettings, r_float, sect, "PDM_disp_accel_k", 1.0f);
		m_launcher_koef.pdm_vel = READ_IF_EXISTS(pSettings, r_float, sect, "PDM_disp_vel_k", 1.0f);
		m_launcher_koef.crosshair_inertion = READ_IF_EXISTS(pSettings, r_float, sect, "crosshair_inertion_k", 1.0f);
		m_launcher_koef.zoom_rotate_time = READ_IF_EXISTS(pSettings, r_float, sect, "zoom_rotate_time_k", 1.0f);
	}

	clamp(m_launcher_koef.cam_dispersion, 0.01f, 2.0f);
	clamp(m_launcher_koef.cam_disper_inc, 0.01f, 2.0f);
	clamp(m_launcher_koef.pdm_base, 0.01f, 2.0f);
	clamp(m_launcher_koef.pdm_accel, 0.01f, 2.0f);
	clamp(m_launcher_koef.pdm_vel, 0.01f, 2.0f);
	clamp(m_launcher_koef.crosshair_inertion, 0.01f, 2.0f);
	clamp(m_launcher_koef.zoom_rotate_time, 0.01f, 2.0f);
}

void CWeaponMagazinedWShotgun::ApplyLauncherKoeffs()
{
	cur_launcher_koef = m_launcher_koef;
}

void CWeaponMagazinedWShotgun::ResetLauncherKoeffs()
{
	cur_launcher_koef.Reset();
}

void CWeaponMagazinedWShotgun::LoadShotgunParams()
{
    if (m_eShotgunStatus == ALife::eAddonAttachable)
    {
        LPCSTR sect = GetShotgunName().c_str();

        // small experiment
        // made it persistent to avoid unloading it which causes a crash for some reason
        // should load all necessary params and reuses code which is always nice
        ShotgunParams.Load(sect);

        // should probably refactor this so we only load the things we need. do later...

        // bullet speed is the ONLY param i cant access, so we do this instead
        m_shotgun_params.bullet_speed = pSettings->r_float(sect, "bullet_speed");

        m_shotgun_params.cam_recoil = ShotgunParams.cam_recoil;
        m_shotgun_params.zoom_cam_recoil = ShotgunParams.zoom_cam_recoil;

        // thanks demonized for the exports!!
        // misc
        m_shotgun_params.crosshair_inertion = ShotgunParams.GetCrosshairInertion();
        m_shotgun_params.zoom_rotate_time = ShotgunParams.GetZoomRotateTime();
        m_shotgun_params.fire_dispersion_base = ShotgunParams.GetFireDispersionScript();
        m_shotgun_params.l_fHitPower = ShotgunParams.GetHitPower();
        m_shotgun_params.hit_impulse = ShotgunParams.GetHitImpulse();
        m_shotgun_params.fire_distance = ShotgunParams.GetFireDistance();
        m_shotgun_params.fOneShotTime = ShotgunParams.RPMScript();

    }
}


bool CWeaponMagazinedWShotgun::UseScopeTexture()
{
	// if (IsShotgunAttached() && m_bShotgunMode) return false;
	if (HasUnderbarrel() && m_bShotgunMode) return false;

	return true;
};

float CWeaponMagazinedWShotgun::CurrentZoomFactor()
{
	// if (IsShotgunAttached() && m_bShotgunMode) return m_zoom_params.m_fScopeZoomFactor;
	if (HasUnderbarrel() && m_bShotgunMode) return m_zoom_params.m_fScopeZoomFactor;
	return inherited::CurrentZoomFactor();
}

//виртуальные функции для проигрывания анимации HUD
void CWeaponMagazinedWShotgun::PlayAnimShow()
{
	VERIFY(GetState() == eShowing);

	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
		if (!m_bShotgunMode)
			HUD_VisualBulletUpdate();

		if (!m_bShotgunMode)
			iAmmoElapsed == 0 && HudAnimationExist("anm_show_empty_w_gl")
			? PlayHUDMotion("anm_show_empty_w_gl", FALSE, this, GetState(), 1.f, 0.f, false)
			: PlayHUDMotion("anm_show_w_gl", FALSE, this, GetState(), 1.f, 0.f, false);
		else
			iAmmoElapsed == 0 && HudAnimationExist("anm_show_empty_g")
			? PlayHUDMotion("anm_show_empty_g", FALSE, this, GetState(), 1.f, 0.f, false)
			: PlayHUDMotion("anm_show_g", FALSE, this, GetState(), 1.f, 0.f, false);

		return;
	}
	// the shotgun keeps its own draw poses
	// if (IsShotgunAttached())
	// if (HasUnderbarrel())
	if (IsShotgunAttached())
	{
		if (!m_bShotgunMode)
			HUD_VisualBulletUpdate();

		if (!m_bShotgunMode)
			iAmmoElapsed == 0 && HudAnimationExist("anm_show_empty_w_sg")
			? PlayHUDMotion("anm_show_empty_w_sg", FALSE, this, GetState(), 1.f, 0.f, false)
			: PlayHUDMotion("anm_show_w_sg", FALSE, this, GetState(), 1.f, 0.f, false);
		else
			// iAmmoElapsed == 0 && HudAnimationExist("anm_show_empty_g")
			iAmmoElapsed == 0 && HudAnimationExist("anm_show_empty_sg")
			// ? PlayHUDMotion("anm_show_empty_g", FALSE, this, GetState(), 1.f, 0.f, false)
			? PlayHUDMotion("anm_show_empty_sg", FALSE, this, GetState(), 1.f, 0.f, false)
			// : PlayHUDMotion("anm_show_g", FALSE, this, GetState(), 1.f, 0.f, false);
			: PlayHUDMotion("anm_show_sg", FALSE, this, GetState(), 1.f, 0.f, false);
	}
	else
		inherited::PlayAnimShow();
}

void CWeaponMagazinedWShotgun::PlayAnimHide()
{
	VERIFY(GetState() == eHiding);

	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
		if (!m_bShotgunMode)
			iAmmoElapsed == 0 && HudAnimationExist("anm_hide_empty_w_gl")
			? PlayHUDMotion("anm_hide_empty_w_gl", TRUE, this, GetState())
			: PlayHUDMotion("anm_hide_w_gl", TRUE, this, GetState());
		else
			iAmmoElapsed == 0 && HudAnimationExist("anm_hide_empty_g")
			? PlayHUDMotion("anm_hide_empty_g", TRUE, this, GetState())
			: PlayHUDMotion("anm_hide_g", TRUE, this, GetState());

		return;
	}

	// the old grenade check also skipped the shotgun hide pose
	// if (IsGrenadeLauncherAttached())
	// if (HasUnderbarrel())
	if (IsShotgunAttached())
		if (!m_bShotgunMode)
			iAmmoElapsed == 0 && HudAnimationExist("anm_hide_empty_w_sg")
			? PlayHUDMotion("anm_hide_empty_w_sg", TRUE, this, GetState())
			: PlayHUDMotion("anm_hide_w_sg", TRUE, this, GetState());
		else
			// iAmmoElapsed == 0 && HudAnimationExist("anm_hide_empty_s")
			iAmmoElapsed == 0 && HudAnimationExist("anm_hide_empty_sg")
			// ? PlayHUDMotion("anm_hide_empty_s", TRUE, this, GetState())
			? PlayHUDMotion("anm_hide_empty_sg", TRUE, this, GetState())
			// : PlayHUDMotion("anm_hide_s", TRUE, this, GetState());
			: PlayHUDMotion("anm_hide_sg", TRUE, this, GetState());

	else
		inherited::PlayAnimHide();
}

void CWeaponMagazinedWShotgun::PlayAnimReload()
{
	VERIFY(GetState() == eReload);

	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
#ifdef NEW_ANIMS //AVO: use new animations
		if (bMisfire)
		{
			if (HudAnimationExist("anm_reload_misfire_w_gl"))
			{
				PlayHUDMotion("anm_reload_misfire_w_gl", TRUE, this, GetState());
				bClearJamOnly = true;
				return;
			}
			else
				PlayHUDMotion("anm_reload_w_gl", TRUE, this, GetState());
		}
		else
		{
			if (iAmmoElapsed == 0)
			{
				if (HudAnimationExist("anm_reload_empty_w_gl"))
					PlayHUDMotion("anm_reload_empty_w_gl", TRUE, this, GetState());
				else
					PlayHUDMotion("anm_reload_w_gl", TRUE, this, GetState());
			}
			else
			{
				PlayHUDMotion("anm_reload_w_gl", TRUE, this, GetState());
			}
		}
#else
		PlayHUDMotion("anm_reload_w_gl", TRUE, this, GetState());
#endif //-NEW_ANIMS
		return;
	}

#ifdef NEW_ANIMS //AVO: use new animations
	// the rifle with a UBSG fitted uses its own reload poses
	// if (IsShotgunAttached())
	// if (HasUnderbarrel())
	if (IsShotgunAttached())
	{
		if (bMisfire)
		{
			if (HudAnimationExist("anm_reload_misfire_w_sg"))
			{
				PlayHUDMotion("anm_reload_misfire_w_sg", TRUE, this, GetState());
				bClearJamOnly = true;
				return;
			}
			else
				PlayHUDMotion("anm_reload_w_sg", TRUE, this, GetState());
		}
		else
		{
			if (iAmmoElapsed == 0)
			{
				if (HudAnimationExist("anm_reload_empty_w_sg"))
					PlayHUDMotion("anm_reload_empty_w_sg", TRUE, this, GetState());
				else
					PlayHUDMotion("anm_reload_w_sg", TRUE, this, GetState());
			}
			else
			{
				PlayHUDMotion("anm_reload_w_sg", TRUE, this, GetState());
			}
		}
	}
	else
		inherited::PlayAnimReload();
#else
    // if (IsGrenadeLauncherAttached())
    if (IsShotgunAttached())
        PlayHUDMotion("anm_reload_w_sg", TRUE, this, GetState());
    else
        inherited::PlayAnimReload();
#endif //-NEW_ANIMS
}

void CWeaponMagazinedWShotgun::PlayAnimIdle()
{
	if (GetState() == eSwitch)
		return;

	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
		if (IsZoomed())
		{
			if (m_bShotgunMode)
				iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_g_aim")
				? PlayHUDMotion("anm_idle_empty_g_aim", TRUE, NULL, GetState())
				: PlayHUDMotion("anm_idle_g_aim", TRUE, NULL, GetState());
			else
				iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_w_gl_aim")
				? PlayHUDMotion("anm_idle_empty_w_gl_aim", TRUE, NULL, GetState())
				: PlayHUDMotion("anm_idle_w_gl_aim", TRUE, NULL, GetState());
		}
		else
		{
			int act_state = 0;
			CActor* pActor = smart_cast<CActor*>(H_Parent());
			if (pActor && pActor->AnyMove())
			{
				CEntity::SEntityState st;
				pActor->g_State(st);
				if (pActor->is_safemode())
					act_state = 0;
				else if (st.bSprint)
				{
					act_state = 1;
				}
				else if (!st.bCrouch)
				{
					act_state = 2;
				}
				else if (st.bCrouch)
				{
					act_state = 3;
				}
			}

			if (m_bShotgunMode)
			{
				if (act_state == 0 || psDeviceFlags2.test(rsBlendMoveAnims))
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_g")
					? PlayHUDMotion("anm_idle_empty_g", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_g", TRUE, NULL, GetState());
				else if (act_state == 1)
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_sprint_empty_g")
					? PlayHUDMotion("anm_idle_sprint_empty_g", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_sprint_g", TRUE, NULL, GetState());
				else if (act_state == 2)
				{
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_g")
						? PlayHUDMotion("anm_idle_moving_empty_g", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_moving_g", TRUE, NULL, GetState());
				}
				else if (act_state == 3)
				{
#ifdef NEW_ANIMS //AVO: custom move animation
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_crouch_empty_g")
						? PlayHUDMotion("anm_idle_moving_crouch_empty_g", TRUE, NULL, GetState())
						: iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_g") ? PlayHUDMotion("anm_idle_moving_empty_g", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_g", TRUE, NULL, GetState(), .7f);
#endif //-NEW_ANIMS
				}
			}
			else
			{
				if (act_state == 0 || psDeviceFlags2.test(rsBlendMoveAnims))
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_w_gl")
						? PlayHUDMotion("anm_idle_empty_w_gl", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_w_gl", TRUE, NULL, GetState());
				else if (act_state == 1)
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_sprint_empty_w_gl")
						? PlayHUDMotion("anm_idle_sprint_empty_w_gl", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_sprint_w_gl", TRUE, NULL, GetState());
				else if (act_state == 2)
				{
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_w_gl")
						? PlayHUDMotion("anm_idle_moving_empty_w_gl", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_moving_w_gl", TRUE, NULL, GetState());
				}
				else if (act_state == 3)
				{
#ifdef NEW_ANIMS //AVO: custom move animation
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_crouch_empty_w_gl")
						? PlayHUDMotion("anm_idle_moving_crouch_empty_w_gl", TRUE, NULL, GetState())
						: iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_w_gl") ? PlayHUDMotion("anm_idle_moving_empty_w_gl", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_w_gl", TRUE, NULL, GetState(), .7f);
#endif //-NEW_ANIMS
				}
			}
		}

		return;
	}

	// the shotgun keeps its own idle and movement poses
	// if (IsShotgunAttached())
	// if (HasUnderbarrel())
	if (IsShotgunAttached())
	{
		if (IsZoomed())
		{
			if (m_bShotgunMode)
				// iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_s_aim")
				iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_sg_aim")
				// ? PlayHUDMotion("anm_idle_empty_s_aim", TRUE, NULL, GetState())
				? PlayHUDMotion("anm_idle_empty_sg_aim", TRUE, NULL, GetState())
				// : PlayHUDMotion("anm_idle_s_aim", TRUE, NULL, GetState());
				: PlayHUDMotion("anm_idle_sg_aim", TRUE, NULL, GetState());
			else
				iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_w_sg_aim")
				? PlayHUDMotion("anm_idle_empty_w_sg_aim", TRUE, NULL, GetState())
				: PlayHUDMotion("anm_idle_w_sg_aim", TRUE, NULL, GetState());
		}
		else
		{
			int act_state = 0;
			CActor* pActor = smart_cast<CActor*>(H_Parent());
			if (pActor && pActor->AnyMove())
			{
				CEntity::SEntityState st;
				pActor->g_State(st);
				if (pActor->is_safemode())
					act_state = 0;
				else if (st.bSprint)
				{
					act_state = 1;
				}
				else if (!st.bCrouch)
				{
					act_state = 2;
				}
				else if (st.bCrouch)
				{
					act_state = 3;
				}
			}

			if (m_bShotgunMode)
			{
				if (act_state == 0 || psDeviceFlags2.test(rsBlendMoveAnims))
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_sg")
					? PlayHUDMotion("anm_idle_empty_sg", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_sg", TRUE, NULL, GetState());
				else if (act_state == 1)
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_sprint_empty_sg")
					? PlayHUDMotion("anm_idle_sprint_empty_sg", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_sprint_sg", TRUE, NULL, GetState());
				else if (act_state == 2)
				{
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_sg")
						? PlayHUDMotion("anm_idle_moving_empty_sg", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_moving_sg", TRUE, NULL, GetState());
				}
				else if (act_state == 3)
				{
#ifdef NEW_ANIMS //AVO: custom move animation
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_crouch_empty_sg")
						? PlayHUDMotion("anm_idle_moving_crouch_empty_sg", TRUE, NULL, GetState())
						// : iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_sg") ? PlayHUDMotion("anm_idle_moving_empty_g", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_g", TRUE, NULL, GetState(), .7f);
						: iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_sg") ? PlayHUDMotion("anm_idle_moving_empty_sg", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_sg", TRUE, NULL, GetState(), .7f);
#endif //-NEW_ANIMS
				}
			}
			else
			{
				if (act_state == 0 || psDeviceFlags2.test(rsBlendMoveAnims))
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_empty_w_sg")
						? PlayHUDMotion("anm_idle_empty_w_sg", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_w_sg", TRUE, NULL, GetState());
				else if (act_state == 1)
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_sprint_empty_w_sg")
						? PlayHUDMotion("anm_idle_sprint_empty_w_sg", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_sprint_w_sg", TRUE, NULL, GetState());
				else if (act_state == 2)
				{
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_w_sg")
						? PlayHUDMotion("anm_idle_moving_empty_w_sg", TRUE, NULL, GetState())
						: PlayHUDMotion("anm_idle_moving_w_sg", TRUE, NULL, GetState());
				}
				else if (act_state == 3)
				{
#ifdef NEW_ANIMS //AVO: custom move animation
					iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_crouch_empty_w_sg")
						? PlayHUDMotion("anm_idle_moving_crouch_empty_w_sg", TRUE, NULL, GetState())
						// : iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_w_sg") ? PlayHUDMotion("anm_idle_moving_empty_w_gl", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_w_gl", TRUE, NULL, GetState(), .7f);
						: iAmmoElapsed == 0 && HudAnimationExist("anm_idle_moving_empty_w_sg") ? PlayHUDMotion("anm_idle_moving_empty_w_sg", TRUE, NULL, GetState(), .7f) : PlayHUDMotion("anm_idle_moving_w_sg", TRUE, NULL, GetState(), .7f);
#endif //-NEW_ANIMS
				}
			}
		}
	}
	else
		inherited::PlayAnimIdle();
}

void CWeaponMagazinedWShotgun::PlayAnimShoot()
{
    // UBGL shots and the rifle with a UBGL attached use the normal grenade launcher aliases
    if (m_secondaryIsGrenade && IsGrenadeLauncherAttached())
    {
        const bool secondary = GetGrenadeLauncherMode();
        LPCSTR normal = secondary ? "anm_shots_g" : "anm_shots_w_gl";
        LPCSTR aimed = secondary ? "anm_shots_g_aim" : "anm_shots_w_gl_aim";
        LPCSTR last = secondary ? "anm_shot_g_l" : "anm_shot_w_gl_l";
        LPCSTR lastAim = secondary ? "anm_shot_g_l_aim" : "anm_shot_w_gl_l_aim";
        if (iAmmoElapsed <= 1 && HudAnimationExist(last))
            PlayHUDMotion(IsZoomed() && HudAnimationExist(lastAim) ? lastAim : last, TRUE, this, GetState(), 1.f, 0.f, false);
        else
            PlayHUDMotion(IsZoomed() && HudAnimationExist(aimed) ? aimed : normal, TRUE, this, GetState(), 1.f, 0.f, false);
        return;
    }
	// the shotgun uses its own normal aimed and last-shot slots
// 	if (m_bShotgunMode)
// 	{
// 		if (iAmmoElapsed > 1 || !HudAnimationExist("anm_shots_g"))
// 		{
// 			if (!IsZoomed() || !HudAnimationExist("anm_shots_g_aim"))
// 				PlayHUDMotion("anm_shots_g", TRUE, this, GetState(), 1.f, 0.f, false);
// 			else
// 				PlayHUDMotion("anm_shots_g_aim", TRUE, this, GetState(), 1.f, 0.f, false);
// 		}
// 		else
// 		{
// 			if(!IsZoomed() || !HudAnimationExist("anm_shots_sg_aim"))
// 				PlayHUDMotion("anm_shots_sg", TRUE, this, GetState(), 1.f, 0.f, false);
// 			else
// 				PlayHUDMotion("anm_shots_sg_aim", TRUE, this, GetState(), 1.f, 0.f, false);
// 		}
// 	}
	if (m_bShotgunMode)
	{
		if (iAmmoElapsed > 1 || !HudAnimationExist("anm_shot_sg_l"))
		{
			if (!IsZoomed() || !HudAnimationExist("anm_shots_sg_aim"))
				PlayHUDMotion("anm_shots_sg", TRUE, this, GetState(), 1.f, 0.f, false);
			else
				PlayHUDMotion("anm_shots_sg_aim", TRUE, this, GetState(), 1.f, 0.f, false);
		}
		else
		{
			if(!IsZoomed() || !HudAnimationExist("anm_shot_sg_l_aim"))
				PlayHUDMotion("anm_shot_sg_l", TRUE, this, GetState(), 1.f, 0.f, false);
			else
				PlayHUDMotion("anm_shot_sg_l_aim", TRUE, this, GetState(), 1.f, 0.f, false);
		}		
	}
	else
	{
		VERIFY(GetState() == eFire);
		// the rifle with a UBSG attached keeps its shotgun attachment firing pose
		// if (IsGrenadeLauncherAttached())
		if (IsShotgunAttached())
			if (iAmmoElapsed > 1 || !HudAnimationExist("anm_shot_w_sg_l"))
			{
				if (!IsZoomed() || !HudAnimationExist("anm_shots_w_sg_aim"))
					PlayHUDMotion("anm_shots_w_sg", TRUE, this, GetState(), 1.f, 0.f, false);
				else
					PlayHUDMotion("anm_shots_w_sg_aim", TRUE, this, GetState(), 1.f, 0.f, false);
			}
			else
			{
				if (!IsZoomed() || !HudAnimationExist("anm_shot_w_sg_l_aim"))
					PlayHUDMotion("anm_shot_w_sg_l", TRUE, this, GetState(), 1.f, 0.f, false);
				else
					PlayHUDMotion("anm_shot_w_sg_l_aim", TRUE, this, GetState(), 1.f, 0.f, false);
			}
		else
			inherited::PlayAnimShoot();
	}
}

void CWeaponMagazinedWShotgun::PlayAnimModeSwitch()
{
	// switching the shotgun must not select the grenade launcher animations
	if (IsShotgunAttached())
	{
		if (m_bShotgunMode)
			iAmmoElapsed == 0 && HudAnimationExist("anm_switch_sg_empty")
			? PlayHUDMotion("anm_switch_sg_empty", TRUE, this, eSwitch)
			: HudAnimationExist("anm_switch_sg") ? PlayHUDMotion("anm_switch_sg", TRUE, this, eSwitch) : SwitchState(eSwitch);
		else
			iAmmoElapsed == 0 && HudAnimationExist("anm_switch_w_sg_empty")
			? PlayHUDMotion("anm_switch_w_sg_empty", TRUE, this, eSwitch)
			: HudAnimationExist("anm_switch_w_sg") ? PlayHUDMotion("anm_switch_w_sg", TRUE, this, eSwitch) : SwitchState(eSwitch);
		return;
	}
	// grenade switching keeps the standard UBGL aliases

	if (m_bShotgunMode)
		iAmmoElapsed == 0 && HudAnimationExist("anm_switch_g_empty")
		? PlayHUDMotion("anm_switch_g_empty", TRUE, this, eSwitch)
		: HudAnimationExist("anm_switch_g") ? PlayHUDMotion("anm_switch_g", TRUE, this, eSwitch) : SwitchState(eSwitch);
	else
		iAmmoElapsed == 0 && HudAnimationExist("anm_switch_empty")
		? PlayHUDMotion("anm_switch_empty", TRUE, this, eSwitch)
		: HudAnimationExist("anm_switch") ? PlayHUDMotion("anm_switch", TRUE, this, eSwitch) : SwitchState(eSwitch);
}

bool CWeaponMagazinedWShotgun::TryPlayAnimBore()
{
	// use the standard UBGL animation selection before the shotgun path
	if (IsGrenadeLauncherAttached())
	{
		if (m_bShotgunMode)
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_bore_empty_g"))
			{
				PlayHUDMotion("anm_bore_empty_g", TRUE, NULL, GetState());
				return true;
			}

			if (HudAnimationExist("anm_bore_g"))
			{
				PlayHUDMotion("anm_bore_g", TRUE, NULL, GetState());
				return true;
			}
		}
		else
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_bore_empty_w_gl"))
			{
				PlayHUDMotion("anm_bore_empty_w_gl", TRUE, NULL, GetState());
				return true;
			}

			if (HudAnimationExist("anm_bore_w_gl"))
			{
				PlayHUDMotion("anm_bore_w_gl", TRUE, NULL, GetState());
				return true;
			}
		}

		return false;
	}

	// the shotgun uses its own selected and attached inspection poses
	// if (IsGrenadeLauncherAttached())
	// if (HasUnderbarrel())
	if (IsShotgunAttached())
	{
		if (m_bShotgunMode)
		{
			// if (iAmmoElapsed == 0 && HudAnimationExist("anm_bore_empty_g"))
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_bore_empty_sg"))
			{
				// PlayHUDMotion("anm_bore_empty_g", TRUE, NULL, GetState());
				PlayHUDMotion("anm_bore_empty_sg", TRUE, NULL, GetState());
				return true;
			}

			// if (HudAnimationExist("anm_bore_g"))
			if (HudAnimationExist("anm_bore_sg"))
			{
				// PlayHUDMotion("anm_bore_g", TRUE, NULL, GetState());
				PlayHUDMotion("anm_bore_sg", TRUE, NULL, GetState());
				return true;
			}
		}
		else
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_bore_empty_w_sg"))
			{
				PlayHUDMotion("anm_bore_empty_w_sg", TRUE, NULL, GetState());
				return true;
			}

			if (HudAnimationExist("anm_bore_w_sg"))
			{
				PlayHUDMotion("anm_bore_w_sg", TRUE, NULL, GetState());
				return true;
			}
		}
	}
	else
		return inherited::TryPlayAnimBore();

	return false;
}

void CWeaponMagazinedWShotgun::UpdateShotgunVisibility(bool visibility)
{
	if (!GetHUDmode()) return;
	// the grenade mesh must stay hidden when a shotgun is fitted
	// HudItemData()->set_bone_visible("grenade", visibility, TRUE);
	const bool loaded = m_bShotgunMode ? !m_magazine.empty() : !m_magazine2.empty();
	HudItemData()->set_bone_visible("grenade", IsGrenadeLauncherAttached() &&
		(loaded || (GetGrenadeLauncherMode() && GetState() == eReload)), TRUE);
}

// old save/update code, retained so the format change is easy to compare
// void CWeaponMagazinedWShotgun::save(NET_Packet& output_packet)
// {
// 	inherited::save(output_packet);
// 	save_data(m_bShotgunMode, output_packet);
// 	save_data(m_magazine2.size(), output_packet);
//
//     // i *think* this should be okay and doesnt affect old saves
//     //save_data(m_ammoType2, output_packet);
// }
//
// void CWeaponMagazinedWShotgun::load(IReader& input_packet)
// {
// 	inherited::load(input_packet);
// 	bool b;
// 	load_data(b, input_packet);
// 	if (b != m_bShotgunMode)
// 		SwitchMode(true);
//
// 	if (b && !m_bShotgunMode) {
// 		Msg("![%s] ERROR: CWeaponMagazinedWShotgun::load: m_bShotgunMode = %d, failed to switch to grenade mode", Name(), m_bShotgunMode);
// 		return;
// 	}
//
// 	u32 sz;
// 	load_data(sz, input_packet);
//
//     // load correct ammo type
//     //u8 Type = 0;
//     //load_data(Type, input_packet);
//
// 	CCartridge l_cartridge;
// 	l_cartridge.Load(m_ammoTypes2[0].c_str(), 0);
//
//     if (sz > 0xffff)
//     {
//         Msg("![%s] ERROR: CWeaponMagazinedWShotgun::load: current magazine size %zu for underbarrel is too big, truncate to 1", Name(), sz);
//         sz = 1;
//     }
//
// 	while (sz > m_magazine2.size())
// 		m_magazine2.push_back(l_cartridge);
// }
//
// void CWeaponMagazinedWShotgun::net_Export(NET_Packet& P)
// {
// 	P.w_u8(m_bShotgunMode ? 1 : 0);
//
// 	CWeaponMagazined::net_Export(P);
//     // this below crashes because i used a mismatching pair of server class vs object class !
//     // inherited::net_Export(P);
//     // to do: create a new server class for this class with corrct memory sizes ! see xrServer_process_update.cpp and add the class in
//     // xrServer_Objects_Alife.cpp
// }
//
// void CWeaponMagazinedWShotgun::net_Import(NET_Packet& P)
// {
// 	bool NewMode = FALSE;
// 	NewMode = !!P.r_u8();
// 	if (NewMode != m_bShotgunMode)
// 		SwitchMode();
//
//     CWeaponMagazined::net_Import(P);
//     // this below crashes because i used a mismatching pair of server class vs object class ! see above
//     // inherited::net_Import(P);
// }

// Always serialize the physical UBSG magazine, not whichever vector happens to be active
void CWeaponMagazinedWShotgun::WriteShotgunAmmo(NET_Packet& P)
{
    // the existing shell extension stays empty when the shared magazine holds a grenade
    if (m_secondaryIsGrenade)
    {
        P.w_u8(0);
        return;
    }
    const auto& magazine = m_bShotgunMode ? m_magazine : m_magazine2;
    R_ASSERT(magazine.size() <= 0xff);
    P.w_u8(u8(magazine.size()));
    for (const CCartridge& cartridge : magazine)
        P.w_u8(cartridge.m_LocalAmmoType);
}

void CWeaponMagazinedWShotgun::RestoreShotgunAmmo(const xr_vector<u8>& types)
{
    // grenade count and type are already restored by the regular magazine fields
    if (m_secondaryIsGrenade) return;
    auto& magazine = m_bShotgunMode ? m_magazine : m_magazine2;
    const auto& ammoTypes = m_bShotgunMode ? m_ammoTypes : m_ammoTypes2;
    magazine.clear();
    for (u32 i = 0; i < types.size() && i < u32(iMagazineSizeShotgun); ++i)
    {
        // Consume all bytes before getting here, but don't index a removed/invalid ammo class
        const u8 type = types[i] < ammoTypes.size() ? types[i] : 0;
        CCartridge cartridge;
        cartridge.Load(ammoTypes[type].c_str(), type, m_APk);
        magazine.push_back(cartridge);
    }
    if (m_bShotgunMode)
        iAmmoElapsed = int(magazine.size());
    else
        iAmmoElapsed2 = u8(magazine.size());
    m_BriefInfo_CalcFrame = 0;
}

void CWeaponMagazinedWShotgun::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    save_data(m_bShotgunMode, output_packet);
    // save_data(m_magazine2.size(), output_packet);
    // keep the tested save layout with its reserved high bit
    output_packet.w_u32(u32(m_magazine2.size()) | 0x80000000u);
    output_packet.w_u8(m_ammoType2);
    WriteShotgunAmmo(output_packet);
}

void CWeaponMagazinedWShotgun::load(IReader& input_packet)
{
    inherited::load(input_packet);
    m_loadedAmmo.pending = true;
    m_loadedAmmo.elapsed = iAmmoElapsed;
    m_loadedAmmo.type = m_ammoType;
    load_data(m_loadedAmmo.shotgunMode, input_packet);
    const u32 size = input_packet.r_u32();
    m_loadedAmmo.inactiveElapsed = size & 0x7fffffffu;
    R_ASSERT(input_packet.elapsed() >= 2);
    m_loadedAmmo.inactiveType = input_packet.r_u8();
    const u8 count = input_packet.r_u8();
    R_ASSERT(input_packet.elapsed() >= count);
    m_loadedAmmo.shellTypes.resize(count);
    for (u8& type : m_loadedAmmo.shellTypes)
        type = input_packet.r_u8();
}

void CWeaponMagazinedWShotgun::net_Export(NET_Packet& P)
{
    const u32 start = P.w_tell();
    // P.w_u8(m_bShotgunMode ? 1 : 0);
    // add attachment identity to the existing flag byte without adding any payload bytes
    // P.w_u8((m_bShotgunMode ? 1 : 0) | CSE_ALifeItemWeaponMagazinedWSG::eAmmoData);
    P.w_u8((m_bShotgunMode ? 1 : 0) | CSE_ALifeItemWeaponMagazinedWSG::eAmmoData |
        (m_secondaryIsGrenade ? CSE_ALifeItemWeaponMagazinedWSG::eGrenadeAttachment : 0));
    // inherited::net_Export(P); uses the ordinary shotgun payload
    // use the magazine payload with the WSG extension to match its server reader
    CWeaponMagazined::net_Export(P);
    P.w_u16(u16(m_magazine2.size()));
    P.w_u8(m_ammoType2);
    WriteShotgunAmmo(P);
    R_ASSERT2(P.w_tell() - start <= 0xff, "WSG update exceeds the object packet size limit");
}

void CWeaponMagazinedWShotgun::net_Import(NET_Packet& P)
{
    // NewMode = !!P.r_u8();
    const u8 flags = P.r_u8();
    const bool mode = (flags & 1) != 0;
    // select the correct ammo list before the base reader interprets an active ammo index
    RefreshSecondaryAmmo((flags & CSE_ALifeItemWeaponMagazinedWSG::eGrenadeAttachment) != 0);
    // SwitchMode(); can fail while pending and leave the ammo index in the wrong list
    // network state must select the right magazine before reading its ammo index
    if (mode != m_bShotgunMode)
        PerformSwitchSG();
    CWeaponMagazined::net_Import(P);
    if (!(flags & CSE_ALifeItemWeaponMagazinedWSG::eAmmoData))
        return;

    R_ASSERT(P.r_elapsed() >= 4);
    const u16 inactiveCount = P.r_u16();
    const u8 inactiveType = P.r_u8();
    const u8 count = P.r_u8();
    R_ASSERT(P.r_elapsed() >= count);
    xr_vector<u8> types(count);
    for (u8& type : types)
        type = P.r_u8();

    m_ammoType2 = inactiveType < m_ammoTypes2.size() ? inactiveType : 0;
    m_DefaultCartridge2.Load(m_ammoTypes2[m_ammoType2].c_str(), m_ammoType2, m_APk);
    m_magazine2.clear();
    // SetAmmoElapsed2(_min(int(inactiveCount), m_bShotgunMode ? iMagazineSize2 : iMagazineSizeShotgun));
    SetAmmoElapsed2(_min(int(inactiveCount), m_bShotgunMode ? iMagazineSize2 : GetSecondaryCapacity()));
    // Match CWeapon's rule: a network update must not replace shells mid-shot or mid-reload
    // When the UBSG is inactive its shell list is safe to restore immediately
    if (!m_bShotgunMode || (GetState() != eFire && GetState() != eFire2 && GetState() != eSwitch && GetState() != eReload))
        RestoreShotgunAmmo(types);
    EnsureGrenadeRocket();
}

float CWeaponMagazinedWShotgun::Weight() const
{
	float res = inherited::Weight();
	res += GetMagazineWeight(m_magazine2);

	return res;
}

bool CWeaponMagazinedWShotgun::IsNecessaryItem(const shared_str& item_sect)
{
	return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() ||
		std::find(m_ammoTypes2.begin(), m_ammoTypes2.end(), item_sect) != m_ammoTypes2.end()
	);
}

u8 CWeaponMagazinedWShotgun::GetCurrentHudOffsetIdx()
{
	bool b_aiming = ((IsZoomed() && m_zoom_params.m_fZoomRotationFactor <= 1.f) ||
		(!IsZoomed() && m_zoom_params.m_fZoomRotationFactor > 0.f));

	if (Actor()->is_safemode())
		return 4;
	else if (!IsZoomed())
		return 0;
	else if (m_bShotgunMode)
		return 2;
	else if (m_zoomtype == 1)
		return 3;
	else
		return 1;
}

bool CWeaponMagazinedWShotgun::install_upgrade_ammo_class(LPCSTR section, bool test)
{
	LPCSTR str;

	bool result = process_if_exists(section, "ammo_mag_size", &CInifile::r_s32, iMagazineSize2, test);
	// upgrades must preserve the single grenade chamber
	// iMagazineSize = m_bShotgunMode ? iMagazineSizeShotgun : iMagazineSize2;
	iMagazineSize = m_bShotgunMode ? GetSecondaryCapacity() : iMagazineSize2;

	//	ammo_class = ammo_5.45x39_fmj, ammo_5.45x39_ap  // name of the ltx-section of used ammo
	bool result2 = process_if_exists_set(section, "ammo_class", &CInifile::r_string, str, test);
	if (result2 && !test)
	{
		xr_vector<shared_str>& ammo_types = m_bShotgunMode ? m_ammoTypes2 : m_ammoTypes;
		ammo_types.clear();
		for (int i = 0, count = _GetItemCount(str); i < count; ++i)
		{
			string128 ammo_item;
			_GetItem(str, i, ammo_item);
			ammo_types.push_back(ammo_item);
		}

		m_ammoType = 0;
		m_ammoType2 = 0;
	}
	result |= result2;

	return result2;
}

bool CWeaponMagazinedWShotgun::install_upgrade_impl(LPCSTR section, bool test)
{
	LPCSTR str;
	bool result = inherited::install_upgrade_impl(section, test);

	//	grenade_class = ammo_vog-25, ammo_vog-25p          // name of the ltx-section of used grenades
	bool result2 = process_if_exists_set(section, "grenade_class", &CInifile::r_string, str, test);
	if (result2 && !test)
	{
		// grenade upgrades must not replace the shotgun shell list when the UBSG is fitted
		// xr_vector<shared_str>& ammo_types = !m_bShotgunMode ? m_ammoTypes2 : m_ammoTypes;
		xr_vector<shared_str>& ammo_types = m_grenadeAmmoTypes;
		ammo_types.clear();
		for (int i = 0, count = _GetItemCount(str); i < count; ++i)
		{
			string128 ammo_item;
			_GetItem(str, i, ammo_item);
			ammo_types.push_back(ammo_item);
		}

		m_ammoType = 0;
		m_ammoType2 = 0;
		if (m_secondaryIsGrenade)
			(m_bShotgunMode ? m_ammoTypes : m_ammoTypes2) = m_grenadeAmmoTypes;
	}
	result |= result2;

    // launcher speed upgrades now apply to real UBGL projectiles
	result |= process_if_exists(section, "launch_speed", &CInifile::r_float, m_fLaunchSpeed, test);

	result2 = process_if_exists_set(section, "snd_shoot_grenade", &CInifile::r_string, str, test);
	if (result2 && !test)
	{
		m_sounds.LoadSound(section, "snd_shoot_grenade", "sndShotG", false, m_eSoundShot);
	}
	result |= result2;

	result2 = process_if_exists_set(section, "snd_reload_grenade", &CInifile::r_string, str, test);
	if (result2 && !test)
	{
		m_sounds.LoadSound(section, "snd_reload_grenade", "sndReloadG", true, m_eSoundReload);
	}
	result |= result2;

	result2 = process_if_exists_set(section, "snd_switch", &CInifile::r_string, str, test);
	if (result2 && !test)
	{
		m_sounds.LoadSound(section, "snd_switch", "sndSwitch", true, m_eSoundReload);
	}
	result |= result2;

	return result;
}

void CWeaponMagazinedWShotgun::net_Spawn_install_upgrades(Upgrades_type saved_upgrades)
{
	// do not delete this
	// this is intended behaviour
}

#include "string_table.h"

bool CWeaponMagazinedWShotgun::GetBriefInfo(II_BriefInfo& info)
{
	VERIFY(m_pInventory);
	/*
	    if(!inherited::GetBriefInfo(info))
	    return false;
	    */
	string32 int_str;
	int ae = GetAmmoElapsed();
	xr_sprintf(int_str, "%d", ae);
	info.cur_ammo._set(int_str);

	if (bHasBulletsToHide && !m_bShotgunMode)
	{
		last_hide_bullet = ae >= bullet_cnt ? bullet_cnt : bullet_cnt - ae - 1;
		if (ae == 0) last_hide_bullet = -1;
	}

	if (HasFireModes())
	{
		if (m_iQueueSize == WEAPON_ININITE_QUEUE)
			info.fire_mode._set("A");
		else
		{
			xr_sprintf(int_str, "%d", m_iQueueSize);
			info.fire_mode._set(int_str);
		}
	}
	if (m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame)
		return false;

	GetSuitableAmmoTotal();

	u32 at_size = m_bShotgunMode ? m_ammoTypes2.size() : m_ammoTypes.size();
	if (unlimited_ammo() || at_size == 0)
	{
		info.fmj_ammo._set("--");
		info.ap_ammo._set("--");
		info.third_ammo._set("--"); //Alundaio
	}
	else
	{
		//Alundaio: Added third ammo type and cleanup
		info.fmj_ammo._set("");
		info.ap_ammo._set("");
		info.third_ammo._set("");

		u8 ammo_type = m_bShotgunMode ? m_ammoType2 : m_ammoType;
		xr_sprintf(int_str, "%d", m_bShotgunMode ? GetAmmoCount2(ammo_type) : GetAmmoCount(ammo_type));

		if (m_ammoType == 0)
			info.fmj_ammo._set(int_str);
		else if (m_ammoType == 1)
			info.ap_ammo._set(int_str);
		else
			info.third_ammo._set(int_str);

		//Alundaio: Added third ammo type and cleanup
		info.fmj_ammo._set("");
		info.ap_ammo._set("");
		info.third_ammo._set("");

		if (at_size >= 1)
		{
			xr_sprintf(int_str, "%d", m_bShotgunMode ? GetAmmoCount2(0) : GetAmmoCount(0));
			info.fmj_ammo._set(int_str);
		}
		if (at_size >= 2)
		{
			xr_sprintf(int_str, "%d", m_bShotgunMode ? GetAmmoCount2(1) : GetAmmoCount(1));
			info.ap_ammo._set(int_str);
		}
		if (at_size >= 3)
		{
			xr_sprintf(int_str, "%d", m_bShotgunMode ? GetAmmoCount2(2) : GetAmmoCount(2));
			info.third_ammo._set(int_str);
		}
		//-Alundaio
	}

	if (ae != 0 && m_magazine.size() != 0)
	{
		LPCSTR ammo_type = m_ammoTypes[m_magazine.back().m_LocalAmmoType].c_str();
		info.name._set(CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short")));
		info.icon._set(ammo_type);
	}
	else
	{
		LPCSTR ammo_type = m_ammoTypes[m_ammoType].c_str();
		info.name._set(CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short")));
		info.icon._set(ammo_type);
	}

	if (!IsGrenadeLauncherAttached())
	{
		info.grenade = "";
		return false;
	}

	int total2 = m_bShotgunMode ? GetAmmoCount(0) : GetAmmoCount2(0);
	if (unlimited_ammo())
		xr_sprintf(int_str, "--");
	else
	{
		if (total2)
			xr_sprintf(int_str, "%d", total2);
		else
			xr_sprintf(int_str, "X");
	}
	info.grenade = int_str;

	return true;
}

int CWeaponMagazinedWShotgun::GetAmmoCount2(u8 ammo2_type) const
{
	VERIFY(m_pInventory);
	R_ASSERT(ammo2_type < m_ammoTypes2.size());

	return GetAmmoCount_forType(m_ammoTypes2[ammo2_type]);
}

#ifdef CROCKETLAUNCHER_CHANGE
// we need to develop something server side for this
//void CWeaponMagazinedWShotgun::UnloadRocket()
//{
//    // we need an alternative to getRocketCount
//	while (getRocketCount() > 0)
//	{
//		NET_Packet P;
//		u_EventGen(P, GE_OWNERSHIP_REJECT, ID());
//		P.w_u16(u16(getCurrentRocket()->ID()));
//		u_EventSend(P);
//        dropCurrentRocket();
//	}
//}
#endif
