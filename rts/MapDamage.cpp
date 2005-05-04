#include "stdafx.h"
#include "mapdamage.h"
#include "readmap.h"
#include "basegrounddrawer.h"
#include "basetreedrawer.h"
#include "timeprofiler.h"
#include "quadfield.h"
#include "unit.h"
#include "loshandler.h"
#include "unithandler.h"
#include "infoconsole.h"
#include "math.h"
#include "PathManager.h"
#include "featurehandler.h"
#include "building.h"
#include "unitdef.h"
//#include "mmgr.h"

CMapDamage* mapDamage;

CMapDamage::CMapDamage(void)
{
	for(int a=0;a<128;++a)
		unsinkable[a]=false;

	for(int a=0;a<128;++a)
		unsinkable[a+128]=true;

	for(int y=0;y<64;++y){
		for(int x=0;x<64;++x){
			inRejuvQue[y][x]=false;
		}
	}
	nextRejuv=0;
	inRelosQue=new bool[qf->numQuadsX*qf->numQuadsZ];
	for(int a=0;a<qf->numQuadsX*qf->numQuadsZ;++a){
		inRelosQue[a]=false;
	}
	relosSize=0;
	neededLosUpdate=0;

	for(int a=0;a<=200;++a){
		float r=a/200.0;
		float d=cos((r-0.1)*(PI+0.3))*(1-r)*(0.5+0.5*cos(max(0,r*3-2)*PI));
		craterTable[a]=d;
	}
	for(int a=201;a<10000;++a){
		craterTable[a]=0;
	}

}

CMapDamage::~CMapDamage(void)
{
	while(!explosions.empty()){
		delete explosions.front();
		explosions.pop_front();
	}
	delete[] inRelosQue;
}

void CMapDamage::Explosion(const float3& pos, float strength,float radius)
{
	if(pos.x<0 || pos.z<0 || pos.x>gs->mapx*SQUARE_SIZE || pos.z>gs->mapy*SQUARE_SIZE){
//		info->AddLine("Map damage explosion outside map %.0f %.0f",pos.x,pos.z);
		return;
	}
	if(strength<10 || radius<8)
		return;

	radius*=1.5;

	Explo* e=new Explo;
	e->pos=pos;
	e->strength=strength;
	e->ttl=10;
	e->x1=max((int)(pos.x-radius)/SQUARE_SIZE,2);
	e->x2=min((int)(pos.x+radius)/SQUARE_SIZE,gs->mapx-3);
	e->y1=max((int)(pos.z-radius)/SQUARE_SIZE,2);
	e->y2=min((int)(pos.z+radius)/SQUARE_SIZE,gs->mapy-3);

	float baseStrength=-pow(strength,0.6f)*0.03;
	float invRadius=1.0/radius;
	for(int y=e->y1;y<=e->y2;++y){
		for(int x=e->x1;x<=e->x2;++x){
			if(readmap->typemap[y*gs->mapx+x]&128){			//dont change squares with building on them here
				e->squares.push_back(0);
				continue;
			}
			float dist=pos.distance2D(float3(x*SQUARE_SIZE,0,y*SQUARE_SIZE));							//calculate the distance
			float relDist=dist*invRadius;																					//normalize distance
			float dif=baseStrength*craterTable[int(relDist*200)];

			float prevDif=readmap->heightmap[y*(gs->mapx+1)+x]-readmap->orgheightmap[y*(gs->mapx+1)+x];

//			prevDif+=dif*5;

			if(prevDif<0)
				dif/=-prevDif*0.05+1;
			e->squares.push_back(dif);
			if(dif<-0.3 && strength>200)
				treeDrawer->RemoveGrass(x,y);
		}
	}
	std::vector<CUnit*> units=qf->GetUnitsExact(pos,radius);
	for(std::vector<CUnit*>::iterator ui=units.begin();ui!=units.end();++ui){		//calculate how much to offset the buildings in the explosion radius with (while still keeping the ground under them flat
		if(dynamic_cast<CBuilding*>(*ui) && !(*ui)->isDead){
			float3& upos=(*ui)->pos;
			UnitDef* unitDef=(*ui)->unitDef;

			int tx1 = max(0,(upos.x-(unitDef->xsize*0.5f*SQUARE_SIZE))/SQUARE_SIZE);
			int tx2 = min(gs->mapx,tx1+unitDef->xsize);
			int tz1 = max(0,(upos.z-(unitDef->ysize*0.5f*SQUARE_SIZE))/SQUARE_SIZE);
			int tz2 = min(gs->mapy,tz1+unitDef->ysize);

			float totalDif=0;
			for(int z=tz1; z<=tz2; z++){
				for(int x=tx1; x<=tx2; x++){
					float dist=pos.distance2D(float3(x*SQUARE_SIZE,0,z*SQUARE_SIZE));							//calculate the distance
					float relDist=dist*invRadius;																					//normalize distance
					float dif=baseStrength*craterTable[int(relDist*200)];
					float prevDif=readmap->heightmap[z*(gs->mapx+1)+x]-readmap->orgheightmap[z*(gs->mapx+1)+z];
					if(prevDif<0)
						dif/=-prevDif*0.05+1;
					totalDif+=dif;
				}
			}
			totalDif/=(tz2-tz1+1)*(tx2-tx1+1);
			if(totalDif!=0){
				ExploBuilding eb;
				eb.id=(*ui)->id;
				eb.dif=totalDif;
				eb.tx1=tx1;
				eb.tx2=tx2;
				eb.tz1=tz1;
				eb.tz2=tz2;
				e->buildings.push_back(eb);
			}
		}
	}

	explosions.push_back(e);

	readmap->Explosion(pos.x,pos.z,strength);
}

void CMapDamage::RecalcArea(int x1, int x2, int y1, int y2)
{
	for(int y=y1;y<y2;y++){
    for(int x=x1;x<x2;x++){
			float height=readmap->heightmap[(y)*(gs->mapx+1)+x];
			height+=readmap->heightmap[(y)*(gs->mapx+1)+x+1];
			height+=readmap->heightmap[(y+1)*(gs->mapx+1)+x];
			height+=readmap->heightmap[(y+1)*(gs->mapx+1)+x+1];
      readmap->centerheightmap[y*gs->mapx+x]=height*0.25;
    }
  }
	int hy2=min(gs->hmapy-1,y2/2);
	int hx2=min(gs->hmapx-1,x2/2);
	for(int y=y1/2;y<=hy2;y++){
    for(int x=x1/2;x<=hx2;x++){
			readmap->halfHeightmap[y*gs->hmapx+x]=readmap->heightmap[(y*2+1)*(gs->mapx+1)+(x*2+1)];
		}
	}

	float3 n1,n2,n3,n4;
	int decy=max(0,y1-1);
	int incy=min(gs->mapy-1,y2+1);
	int decx=max(0,x1-1);
	int incx=min(gs->mapx-1,x2+1);

	for(int y=decy;y<=incy;y++){
		for(int x=decx;x<=incx;x++){

			float3 e1(-SQUARE_SIZE,readmap->heightmap[y*(gs->mapx+1)+x]-readmap->heightmap[y*(gs->mapx+1)+x+1],0);
			float3 e2( 0,readmap->heightmap[y*(gs->mapx+1)+x]-readmap->heightmap[(y+1)*(gs->mapx+1)+x],-SQUARE_SIZE);
			
			float3 n=e2.cross(e1);
			n.Normalize();

      readmap->facenormals[(y*gs->mapx+x)*2]=n;

			e1=float3( SQUARE_SIZE,readmap->heightmap[(y+1)*(gs->mapx+1)+x+1]-readmap->heightmap[(y+1)*(gs->mapx+1)+x],0);
			e2=float3( 0,readmap->heightmap[(y+1)*(gs->mapx+1)+x+1]-readmap->heightmap[(y)*(gs->mapx+1)+x+1],SQUARE_SIZE);
			
			n=e2.cross(e1);
			n.Normalize();

      readmap->facenormals[(y*gs->mapx+x)*2+1]=n;

    }
  }
	pathManager->TerrainChange(x1,y1,x2,y2);
	featureHandler->TerrainChanged(x1,y1,x2,y2);
	readmap->RecalcTexture(x1,x2,y1,y2);

	decy=max(0,(y1*SQUARE_SIZE-QUAD_SIZE/2)/QUAD_SIZE);
	incy=min(qf->numQuadsZ-1,(y2*SQUARE_SIZE+QUAD_SIZE/2)/QUAD_SIZE);
	decx=max(0,(x1*SQUARE_SIZE-QUAD_SIZE/2)/QUAD_SIZE);
	incx=min(qf->numQuadsX-1,(x2*SQUARE_SIZE+QUAD_SIZE/2)/QUAD_SIZE);

	for(int y=decy;y<=incy;y++){
		for(int x=decx;x<=incx;x++){
			if(inRelosQue[y*qf->numQuadsX+x])
				continue;
			RelosSquare rs;
			rs.x=x;
			rs.y=y;
			rs.neededUpdate=gs->frameNum;
			rs.numUnits=qf->baseQuads[y*qf->numQuadsX+x].units.size();
			relosSize+=rs.numUnits;
			inRelosQue[y*qf->numQuadsX+x]=true;
			relosQue.push_back(rs);
		}
	}
}

void CMapDamage::Update(void)
{
START_TIME_PROFILE;

	std::deque<Explo*>::iterator ei;

	for(ei=explosions.begin();ei!=explosions.end();++ei){
		Explo* e=*ei;
		--e->ttl;

		int x1=e->x1;
		int x2=e->x2;
		int y1=e->y1;
		int y2=e->y2;
		std::vector<float>::iterator si=e->squares.begin();
		for(int y=y1;y<=y2;++y){
			for(int x=x1;x<=x2;++x){
				float dif=*(si++);
				readmap->heightmap[y*(gs->mapx+1)+x]+=dif;
			}
		}
		for(std::vector<ExploBuilding>::iterator bi=e->buildings.begin();bi!=e->buildings.end();++bi){
			float dif=bi->dif;
			int tx1 = bi->tx1;
			int tx2 = bi->tx2;
			int tz1 = bi->tz1;
			int tz2 = bi->tz2;

			for(int z=tz1; z<=tz2; z++){
				for(int x=tx1; x<=tx2; x++){
					readmap->heightmap[z*(gs->mapx+1)+x]+=dif;
				}
			}
			CUnit* unit=uh->units[bi->id];
			if(unit){
				unit->pos.y+=dif;
				unit->midPos.y+=dif;
			}
		}
		readmap->ExplosionUpdate(e->x1,e->x2,e->y1,e->y2);
		if(e->ttl==0){
			float3 pos=e->pos;
			float strength=e->strength;
			float ds=2.5*sqrt(strength);
			float dd=strength/ds*8;
			RecalcArea(x1-2,x2+2,y1-2,y2+2);
	/*		for(int y=y1>>3;y<=y2>>3;++y){
				for(int x=x1>>3;x<=x2>>3;++x){
					if(!inRejuvQue[y][x]){
						RejuvSquare rs;
						rs.x=x;
						rs.y=y;
						rejuvQue.push_back(rs);
						inRejuvQue[y][x]=true;
					}
				}
			}*/
		}
	}
	while(!explosions.empty() && explosions.front()->ttl==0){
		delete explosions.front();
		explosions.pop_front();
	}

	nextRejuv+=rejuvQue.size()/300.0;
	while(nextRejuv>0){
		nextRejuv-=1;
		int bx=rejuvQue.front().x;
		int by=rejuvQue.front().y;
		bool damaged=false;

		for(int y=by*16;y<by*16+16;++y){
			for(int x=bx*16;x<bx*16+16;++x){
				if(readmap->damagemap[y*1024+x]!=0){
					damaged=true;
					if(readmap->damagemap[y*1024+x]>5){
						int hsquare=((y+1)/2)*(gs->mapx+1)+(x+1)/2;
						readmap->heightmap[hsquare]-=(readmap->heightmap[hsquare]-readmap->orgheightmap[hsquare])*0.003;
						readmap->damagemap[y*1024+x]-=4;
					} else {
						readmap->damagemap[y*1024+x]=0;
						if(readmap->typemap[(y/2)*512+x/2]&128){
							readmap->typemap[(y/2)*512+x/2]&=127;
							treeDrawer->ResetPos(float3(x*4,0,y*4));
						}
					}
				}
			}
		}
		if(damaged){
			rejuvQue.push_back(rejuvQue.front());
			RecalcArea(bx*8,bx*8+8,by*8,by*8+8);
			readmap->RecalcTexture(bx*8,bx*8+8,by*8,by*8+8);
		} else {
			inRejuvQue[by][bx]=false;
		}
		rejuvQue.pop_front();
	}
	UpdateLos();
END_TIME_PROFILE("Map damage");
}

void CMapDamage::UpdateLos(void)
{
	int updateSpeed=relosSize*0.01+1;

	if(relosUnits.empty()){
		if(relosQue.empty())
			return;
		RelosSquare* rs=&relosQue.front();

		std::list<CUnit*>* q=&qf->baseQuads[rs->y*qf->numQuadsX+rs->x].units;
		for(std::list<CUnit*>::iterator ui=q->begin();ui!=q->end();++ui){
			relosUnits.push_back((*ui)->id);
		}
		relosSize-=rs->numUnits;
		neededLosUpdate=rs->neededUpdate;
		inRelosQue[rs->y*qf->numQuadsX+rs->x]=false;
		relosQue.pop_front();
	}
	for(int a=0;a<updateSpeed;++a){
		if(relosUnits.empty())
			return;
		CUnit* u=uh->units[relosUnits.front()];
		relosUnits.pop_front();

		if(u==0 || u->lastLosUpdate>=neededLosUpdate){
			continue;
		}
		loshandler->MoveUnit(u,true);
	}
}
