#ifndef __TSOLGEMCLUSTER_
#define __TSOLGEMCLUSTER_

#include "TROOT.h"

class TSolGEMCluster : public TObject {
    public:
	TSolGEMCluster() {printf("I'm a cluster!\n");}
	virtual ~TSolGEMCluster() {;}
	
	Double_t GetPos();
	Double_t GetE();

    private:
	Double_t fPos;
	Double_t fE;

    public:
	ClassDef(TSolGEMCluster,1)

};
#endif//__TSOLGEMCLUSTER_
