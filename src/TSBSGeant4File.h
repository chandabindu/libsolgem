#ifndef __TSBSGEANT4FILE_H
#define __TSBSGEANT4FILE_H

// Put prototypes here first so that it doens't freak out
//#ifndef __CINT__
//#include "evioUtil.hxx"
//#endif//__CINT__

#include "TROOT.h"
#include "TFile.h"
#include "TChain.h"
#include "g4sbs_tree.h"
#include "TSBSGEMSimHitData.h"
#include "TSBSSimEvent.h"
#include "TRandom3.h"

class TSBSDBManager;

#define __DEFAULT_DATA_SIZE 32

////////////////////////////////////////////////////////////////////////////
// Auxilliary class for storing hit data
//
// Stores an arbitrary double data in dynamically allocated
// arrays.  Allows us to add in data as we get it and then check
// to make sure all entries in the array are filled
// // ___________________________________________________________ //
// // hit_data: {GEM plane, E deposited, X_RO_x, X_RO_y, X_RO_z, 
// //            X_in_x, X_in_y, X_in_z, tmin,
// //            X_out_x, X_out_y, X_out_z, tmax,
// //            type (prim, second), x_vtx, y_vtx, z_vtx,
// //            Track ID, Particle ID, sector, 
// //            px, py, pz}
// // the strucutre of the data array is identical to the structure 
// // of the hitdata array defined in TSolEVIOFile class

class g4sbshitdata {
    public:
        //Default constructor. The size may depend on the data we examine (GEM, CDET, ECal)
        //TODO: at some point, include CDET and ECal hits
  	g4sbshitdata( int detid, unsigned int size = __DEFAULT_DATA_SIZE );
	virtual ~g4sbshitdata();
	
	//Get detector ID
	int     GetDetID() const { return fDetID;}

	// Get/set one specific element of the data for this hit
	void    SetData( unsigned int, double );
	double  GetData( unsigned int ) const ;
	double *GetData(){ return fData; }//Get all data array 
	
	bool    IsFilled() const ;
	
    protected:
	int     fDetID;//detector ID
	unsigned int     fSize;//data array size;
	long long int fFillbits;
	double *fData;//data array: See in .cxx the sequence of this data array for g4sbs GEMs
};

////////////////////////////////////////////////////////////////////////////
// Auxilliary class for storing generated track data
// // ___________________________________________________________ //
// //gendata: {TRID, PID, px, py, pz, x_vtx, y_vtx, z_vtx, weight};
// // the strucutre of this data array is identical to the structure 
// // of the gendata array defined in TSolEVIOFile class

class g4sbsgendata : public g4sbshitdata {
    public:
	g4sbsgendata();
	~g4sbsgendata(){;}
	
	int	GetTRID() const { return IsFilled()? (int) fData[0] : -1e9; }//G4 particle ID
	int	GetPID() const { return IsFilled()? (int) fData[1] : -1e9; }//G4 particle ID
	double  GetWeight() const { return fData[8]; }//cross section
	TVector3 GetP() const { return IsFilled()? TVector3(fData[2], fData[3], fData[4]) : TVector3(-1e9, -1e9, -1e9 ); }//Track momentum 3-vector
	TVector3 GetV() const { return IsFilled()? TVector3(fData[5], fData[6], fData[7]) : TVector3(-1e9, -1e9, -1e9 ); }//Track vtx 3-vector
	TVector3 GetMomentumAtTarget() const { return IsFilled()? TVector3(fData[9], fData[10], fData[11]) : TVector3(-1e9, -1e9, -1e9 ); }
	TVector3 GetVertexAtTarget() const { return IsFilled()? TVector3(fData[12], fData[13], fData[14]) : TVector3(-1e9, -1e9, -1e9 ); }

};


///////////////////////////////////////////////////////////////////////////////


class TSBSGeant4File {

 public:
  //constructor may be inputed a data file to input some of the paramaters used by this class
  //NB: if the second file path does not select a valid file, default parameters will be used.
  TSBSGeant4File();// Default constructor
  TSBSGeant4File( const char *name);// Constructor with input file name: recommanded
  virtual ~TSBSGeant4File();// Default destructor
  
  //void ReadGasData(const char* filename); // NB: See comment lines 138-141
  
  // Standard getters and setters
  void  SetFilename( const char *name );
  void  SetSource( Int_t i ) { fSource = i; }
  void  Clear();
  Int_t Open();
  Int_t Close();
  
  Long64_t GetEntries(){
    return fTree->GetEntries();
  };
  
  const char* GetFileName() const { return fFilename; }
  Int_t GetSource() const { return fSource; }
  
  // This is actually where the data is read: 
  Int_t ReadNextEvent(int d_flag = 0);
  
  //return the size of the hit arrays
  UInt_t GetNData() const { return fg4sbsHitData.size(); }
  UInt_t GetNGen() const { return fg4sbsGenData.size(); }
  UInt_t GetNECal() const { return fECalClusters.size(); }
  
  UInt_t GetEvNum() const { return fEvNum; }
  void SetFirstEvNum(UInt_t evnum){ fEvNum = evnum-1; }
  
  //get one hit from the hit data arrays
  g4sbshitdata *GetHitData(Int_t i) const { return fg4sbsHitData[i]; }
  g4sbsgendata *GetGenData(Int_t i) const { return fg4sbsGenData[i]; }
  
  //get GEM data
  TSBSGEMSimHitData *GetGEMData();
  void GetGEMData(TSBSGEMSimHitData* gd);
  
  //double FindGasRange(double p); // NB: See comment lines 138-141
  //NB: *this is a complete kludge for an emergency*
  // we (I) should migrate to libsbsdig once for all after that
  void GetBBECalCluster();
  void GetGEpECalCluster();
  void GetGEpCDetCluster();
  void GetHCalCluster();
  
  UInt_t GetClusterSize() const {return fECalClusters.size();};
  void AddCluster(TSBSECalCluster* clus){fECalClusters.push_back(clus);};
  TSBSECalCluster* GetCluster(int i) const {return fECalClusters.at(i);};
  
  UInt_t GetSciClusterSize() const {return fScintClusters.size();};
  void AddSciCluster(TSBSScintCluster* clus){fScintClusters.push_back(clus);};
  TSBSScintCluster* GetSciCluster(int i) const {return fScintClusters.at(i);};
  
 private:
  // Members
  char  fFilename[255];
  TFile *fFile;
  g4sbs_tree *fTree;// needed to easily unfold root file data
  Int_t fSource;   // User-defined source ID (e.g. MC run number)  // Temp: Do we use that ?
  //double fZSpecOffset; // Offset with which the GEM hits are registered in g4sbs for GEP.
  
  // These two variables are arrays to store the segmentations of the GEM detectors.
  /* std::vector<double> fXseg1; */
  /* std::vector<double> fXseg2; */
  /* int fNSector1, fNSector2; */
  
  // NB: 2017/12/12: The use of electron range in ionized gas is now deprecated 
  // due to the addition of x/y/zin and x/y/z/out 
  // (position of the particle at entrance and exit of GEM drift gas) 
  // in the g4sbs out put
  /* vector<double> feMom; */
  /* vector<double> fgasErange; */

  TRandom3* fR;
  
  //hit data arrays
  std::vector<g4sbshitdata *> fg4sbsHitData;
  std::vector<g4sbsgendata *> fg4sbsGenData;
  
  std::vector<TSBSECalCluster *> fECalClusters; // ECal clusters
  std::vector<TSBSScintCluster *> fScintClusters; // ECal clusters
  
  unsigned int fEvNum;// global event incrementer

  TSBSDBManager *fManager;
};



#endif//__TSOLSIMG4SBSFILE_H
