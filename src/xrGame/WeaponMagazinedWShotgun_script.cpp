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
    ];
}

