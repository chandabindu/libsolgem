/////////////////////////////////////////////////////////////////////
//
//   TSBSSimEvent
//
//   Common class definitions (event, MC track, etc.) for SoLID
//   simulation decoder
//
//   Ole Hansen (ole@jlab.org)  December 2011
//
/////////////////////////////////////////////////////////////////////

#ifndef __TSBSSim_h
#define __TSBSSim_h

#include "SimDecoder.h"
#include "TVector3.h"
#include "TArrayS.h"
#include "TArrayI.h"
#include "TArrayD.h"
#include <vector>

class TClonesArray;

//-----------------------------------------------------------------------------
class TSBSSimTrack : public Podd::MCTrack {
public:
  TSBSSimTrack( Int_t number, Int_t pid,
		const TVector3& vertex, const TVector3& momentum, const TVector3& vertexAtTarget, const TVector3& momentumAtTarget );
  TSBSSimTrack();
  
  //Special function for debugging
  Double_t MCFitX_print()     const;

  TVector3 vertex_target;
  TVector3 momentum_target;

  Double_t targetX()      { return vertex_target.X(); }
  Double_t targetY()      { return vertex_target.Y(); }
  Double_t targetZ()      { return vertex_target.Z(); }
  Double_t targetP()      { return momentum_target.Mag();   }
  Double_t targetPX()      { return momentum_target.X();   }
  Double_t targetPY()      { return momentum_target.Y();   }
  Double_t targetPZ()      { return momentum_target.Z();   }
  Double_t targetPTheta() { return momentum_target.Theta(); }
  Double_t targetPPhi()   { return momentum_target.Phi();   }


  
  Double_t PX()           { return fMomentum.X(); }
  Double_t PY()           { return fMomentum.Y(); }
  Double_t PZ()           { return fMomentum.Z(); }


  // Accessors for SBS-specific parameters
  // EFuchey: 2017/01/24:
  // Those below are not useful for SBS, which needs X, Y, Xdir, Ydir (unless otherwise demonstrated)
  // Now I'm wondering if TSBSSimTrack is useful at all, but in case, we will stick to that
  /* Double_t MCFitR()     const; */
  /* Double_t MCFitPhi()     const; */
  /* Double_t MCFitThetaDir()  const; */
  /* Double_t MCFitPhiDir()  const; */
  /* Double_t RcFitR()     const; */
  /* Double_t RcFitPhi()     const; */
  /* Double_t RcFitThetaDir()  const; */
  /* Double_t RcFitPhiDir()  const; */
  
  ClassDef(TSBSSimTrack,3)  // A MC physics track in SBS
};

//
class TSBSECalCluster : public TObject {
 public:
  TSBSECalCluster(double E, double Xpos, double Ypos, double t = 0, int detflag = 10, double Xmax = 0, double Ymax = 0);
  TSBSECalCluster();
  ~TSBSECalCluster(){};
  
  const double GetEnergy() const {return fEnergy;};
  const double GetXPos() const {return fXPos;};
  const double GetYPos() const {return fYPos;};
  const double GetXMax() const {return fXMax;};
  const double GetYMax() const {return fYMax;};
  const double GetTime() const {return fTime;};
  const Int_t  GetDetFlag() const {return fDetFlag;};
  
  void SetEnergy(double E){fEnergy = E;};
  void SetXPos(double X){fXPos = X;};
  void SetYPos(double Y){fYPos = Y;};
  void SetXMax(double X){fXMax = X;};
  void SetYMax(double Y){fYMax = Y;};
  void SetTime(double t){fTime = t;};
  void SetDetFlag(int flag){fDetFlag = flag;};
  
 protected:
  Double_t fEnergy;
  Double_t fXPos;// in transport coordinates
  Double_t fYPos;// in transport coordinates
  Double_t fXMax;// in transport coordinates
  Double_t fYMax;// in transport coordinates
  Double_t fTime;
  Int_t fDetFlag;//mmm... 
  
  ClassDef(TSBSECalCluster,2)  // A MC physics track in SBS
};

class TSBSScintCluster : public TSBSECalCluster {
 public:
  TSBSScintCluster(int plane, double E, double Xpos, double Ypos, double t = 0, int detflag = 30, double Xmax = 0, double Ymax = 0);
  TSBSScintCluster();
  ~TSBSScintCluster(){};
  
  const Int_t  GetPlane() const {return fPlane;};
  
  void SetPlane(int plane){fPlane = plane;};
  
 private:
  Int_t fPlane;
  
  ClassDef(TSBSScintCluster,1)  // A MC physics track in SBS
};



//-----------------------------------------------------------------------------
// Kludgy hardcoded parameters necessary because I can't get ROOT to allocate
// arrays dynamically via TTree::GetEntry
//#define MC_MAXC  10000
//#define MC_MAXS  20000
#define MC_MAXSAMP 10

#define treeName "digtree"
#define eventBranchName "event"

class TSBSSimEvent : public TObject {
public:
  TSBSSimEvent();                 // Default constructor, for ROOT I/O
  TSBSSimEvent( UInt_t ntracks ); // Construct and initialize fMCTracks
  virtual ~TSBSSimEvent();

  virtual void Clear( Option_t* opt="" );
  virtual void Print( Option_t* opt="" ) const;
  TSBSSimTrack* AddTrack( Int_t number, Int_t pid,
			  const TVector3& vertex, const TVector3& momentum, 
			  const TVector3& vertexAtTarget, const TVector3& momentumAtTarget);

  Int_t GetNclust()  const { return fGEMClust.size(); }
  Int_t GetNstrips() const { return fGEMStrips.size(); }
  Int_t GetNtracks() const;

  // Event identification
  Int_t     fRunID;               // Run number
  Int_t     fEvtID;               // Event number

  Double_t  fWeight;              // Event weight

  // MC tracks
  TClonesArray*   fMCTracks;      //-> Physics tracks

  // Cluster variables (MC generated)
  // Each "cluster" is a MC hit along with the digitized charge avalance data
  // Note: fNSignal <= fClust.size() (equal if no background)
  // The first fNSignal elements in the array of clusters are from the signal
  Int_t     fNSignal;             // Number of clusters from trigger track (signal)

  Bool_t    fSectorsMapped;       // Sectors are mapped to 0
  Int_t     fSignalSector;        // Sector of primary signal particle

  struct GEMCluster {
    Short_t   fID;        // Cluster number, cross-ref to GEMStrip
    // MC hit data
    Short_t   fSector;    // Sector number
    Short_t   fPlane;     // Plane number
    Short_t   fModule;    // Module number
    Short_t   fRealSector;// Real sector number (may be !=fSector if mapped)
    Int_t     fSource;    // MC data set source (0 = signal, >0 background)
    Int_t     fType;      // GEANT particle type (1 = primary)
    Int_t     fTRID;      // GEANT particle counter
    Int_t     fPID;       // PDG ID of particle generating the cluster
    TVector3  fP;         // Momentum of particle generating the cluster in the lab [GeV]
    TVector3  fPspec;     // Momentum of particle generating the cluster in the spec [GeV]
    TVector3  fXEntry;    // Track at chamber entrance in lab coords [m]
    TVector3  fMCpos;     // Approx. truth position of hit in lab [m]
    TVector3  fHitpos;    // fMCpos in Tracker frame [m]
    // Digitization results for this hit
    Float_t   fCharge;    // Charge of avalanche
    Float_t   fTime;      // Arrival time at electronics
    Int_t     fSize[2];   // Number of strips in cluster per axis
    Int_t     fStart[2];  // Number of first strip in cluster per axis
    Float_t   fXProj[2];  // fMCpos along projection axis [m]
    TVector3  fVertex;    // Vertex
  };

  std::vector<GEMCluster> fGEMClust;  // All MC-generated clusters in the GEMs

  // Digitized strip amplitude data
  struct DigiGEMStrip {
    Short_t   fSector;    // Sector number
    Short_t   fPlane;     // Plane number
    Short_t   fModule;    // Module number
    Short_t   fProj;      // Readout coordinate ("x" = 0, "y" = 1)
    Short_t   fChan;      // Channel number
    Short_t   fSigType;   // Accumulated signal types (BIT(0) = signal)
    Float_t   fCharge;    // Total charge in strip
    Float_t   fTime1;     // Time of first sample
                          //   relative to event start in target (TBC)
    UShort_t  fNsamp;     // Number of ADC samples
    Int_t     fADC[MC_MAXSAMP]; // [fNsamp] ADC samples
    //Int_t     fCommonMode[MC_MAMSAMP];// Real Common mode added to strip----going to work on next, needs to digitize all strips// seems no need to add strip to strip offset since it is just adding a constant in digitization and subtracting same known constant in analysis. "Strip specific pedestal rms and common mode are important"
    TArrayS   fClusters;  // Clusters ID contributing to signal on this strip
    TArrayI   fClusterRatio[MC_MAXSAMP];
    TArrayD   fStripWeightInCluster;
  };
  
  std::vector<DigiGEMStrip> fGEMStrips; // Digitized strip amplitudes of the GEMs
  
  std::vector<TSBSECalCluster> fECalClusters; // ECal clusters
  std::vector<TSBSScintCluster> fScintClusters; // Scint clusters
  
  ClassDef(TSBSSimEvent, 5) // Simulated data for one event
};

#endif
