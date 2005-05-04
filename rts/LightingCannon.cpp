#include "stdafx.h"

#include ".\lightingcannon.h"

#include "unit.h"

#include "tracerprojectile.h"

#include "sound.h"

#include "gamehelper.h"

#include "lightingprojectile.h"

#include "ground.h"

//#include "mmgr.h"



CLightingCannon::CLightingCannon(CUnit* owner)

: CWeapon(owner)

{

}



CLightingCannon::~CLightingCannon(void)

{

}



void CLightingCannon::Update(void)

{

	if(targetType!=Target_None){

		weaponPos=owner->pos+owner->frontdir*relWeaponPos.z+owner->updir*relWeaponPos.y+owner->rightdir*relWeaponPos.x;

		if(!onlyForward){		

			wantedDir=targetPos-weaponPos;

			wantedDir.Normalize();

		}

	}

	CWeapon::Update();

}



bool CLightingCannon::TryTarget(const float3& pos,bool userTarget,CUnit* unit)

{

	if(!CWeapon::TryTarget(pos,userTarget,unit))

		return false;



	if(unit){

		if(unit->isUnderWater)

			return false;

	} else {

		if(pos.y<0)

			return false;

	}



	float3 dir=pos-weaponPos;

	float length=dir.Length();

	dir/=length;



	float g=ground->LineGroundCol(weaponPos,pos);

	if(g>0 && g<length*0.9)

		return false;



	if(helper->LineFeatureCol(weaponPos,dir,length))

		return false;



	if(helper->TestCone(weaponPos,dir,length,(accuracy+sprayangle),owner->allyteam,owner))

		return false;

	return true;

}



void CLightingCannon::Init(void)

{

	CWeapon::Init();

}



void CLightingCannon::Fire(void)

{

	float3 dir=targetPos-weaponPos;

	dir.Normalize();

	CUnit* u=0;

	float r=helper->TraceRay(weaponPos,dir,range,0,owner,u);



	if(u)

		u->DoDamage(damages,owner,ZeroVector);



	new CLightingProjectile(weaponPos,weaponPos+dir*(r+10),owner,color,10,this);

	if(fireSoundId)

		sound->PlaySound(fireSoundId,owner,fireSoundVolume);



}







void CLightingCannon::SlowUpdate(void)

{

	CWeapon::SlowUpdate();

	if(targetType==Target_Unit){

		predict=(gs->randFloat()-0.5)*20*range/weaponPos.distance(targetUnit->midPos)*(1.2-owner->limExperience);		//make the weapon somewhat less effecient against aircrafts hopefully

	}

}

