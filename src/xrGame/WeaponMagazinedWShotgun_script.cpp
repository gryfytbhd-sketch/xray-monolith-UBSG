#include "stdafx.h"
#include "pch_script.h"
#include "WeaponMagazinedWShotgun.h"

using namespace luabind;

#pragma optimize("s",on)
void CWeaponMagazinedWShotgun::script_register(lua_State* L)
{
    module(L)
    [
        class_<CWeaponMagazinedWShotgun, CGameObject>("CWeaponMagazinedWShotgun")
        .def(constructor<>())
        // expose the shotgun methods to scripts and keep Type2 as the inactive magazine
        .def("GetShotgunMode", &CWeaponMagazinedWShotgun::GetShotgunMode)
        .def("SetAmmoType2", &CWeaponMagazinedWShotgun::SetAmmoType2)
        .def("GetAmmoType2", &CWeaponMagazinedWShotgun::GetAmmoType2)
    ];
}

