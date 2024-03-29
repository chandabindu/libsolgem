#ifndef __TSolSimDecoder_h
#define __TSolSimDecoder_h

/////////////////////////////////////////////////////////////////////
//
//   TSolSimDecoder
//
/////////////////////////////////////////////////////////////////////

#include "SimDecoder.h"
#include "TSolSimEvent.h"
#include "ha_compiledata.h"
#include <cassert>
#include <map>

class THaCrateMap;

//-----------------------------------------------------------------------------
// Helper classes for making decoded event data available via global variables

class TSolSimGEMHit : public TObject {
public:
  TSolSimGEMHit() {}
  TSolSimGEMHit( const TSolSimEvent::GEMCluster& cl );

  virtual void Print( const Option_t* opt="" ) const;

  Double_t P() const { return fP.Mag(); }
  Double_t X() const { return fMCpos.X(); }
  Double_t Y() const { return fMCpos.Y(); }
  Double_t Z() const { return fMCpos.Z(); }
  Double_t R()      const { return fMCpos.Perp(); }
  Double_t Theta()  const { return fMCpos.Theta(); }
  Double_t Phi()    const { return fMCpos.Phi(); }

  Int_t     fID;        // Cluster number, cross-ref to GEMStrip
  // MC hit data
  Int_t     fSector;    // Sector number
  Int_t     fPlane;     // Plane number
  Int_t     fRealSector;// Real sector number (may be !=fSector if mapped)
  Int_t     fSource;    // MC data set source (0 = signal, >0 background)
  Int_t     fType;      // GEANT particle counter (1 = primary)
  Int_t     fPID;       // PDG ID of particle generating the cluster
  TVector3  fP;         // Momentum of particle generating the cluster
  TVector3  fXEntry;    // Track at chamber entrance in lab coords [m]
  TVector3  fMCpos;     // Approx. truth position of hit in lab [m]
  TVector3  fHitpos;    // fMCpos in Tracker frame [m]
  // Digitization results for this hit
  Float_t   fCharge;    // Charge of avalanche
  Float_t   fTime;      // Arrival time at electronics (w/o ToF)
  Int_t     fUSize;     // Number of strips in u-cluster
  Int_t     fUStart;    // Number of first strip in u-cluster
  Float_t   fUPos;      // fMCpos along u-projection axis [m]
  Int_t     fVSize;     // Number of strips in v-cluster
  Int_t     fVStart;    // Number of first strip in v-cluster
  Float_t   fVPos;      // fMCpos along v-projection axis [m]

  ClassDef(TSolSimGEMHit,1) // A Monte Carlo hit at a GEM tracking chamber
};

//-----------------------------------------------------------------------------
class TSolSimBackTrack : public TObject {
public:
  TSolSimBackTrack() {}
  TSolSimBackTrack( const TSolSimEvent::GEMCluster& cl );

  Double_t X()         const { return fOrigin.X(); }
  Double_t Y()         const { return fOrigin.Y(); }
  Double_t P()         const { return fMomentum.Mag(); }
  Double_t ThetaT()    const { return fMomentum.Px()/fMomentum.Pz(); }
  Double_t PhiT()      const { return fMomentum.Py()/fMomentum.Pz(); }
  Double_t R()         const { return fOrigin.Perp(); }
  Double_t Theta()     const { return fOrigin.Theta(); }
  Double_t Phi()       const { return fOrigin.Phi(); }
  Double_t ThetaDir()  const { return fMomentum.Theta(); }
  Double_t PhiDir()    const { return fMomentum.Phi(); }
  Double_t HX()        const { return fHitpos.X(); }
  Double_t HY()        const { return fHitpos.Y(); }

  virtual void Print( const Option_t* opt="" ) const;

  Int_t    GetType()    const { return fType; }
  Int_t    GetSource()  const { return fSource; }
  Int_t    GetHitBits() const { return fHitBits; }
  Int_t    GetUfailBits() const { return fUfailBits; }
  Int_t    GetVfailBits() const { return fVfailBits; }
  void     SetHitBit( UInt_t i )  { SETBIT(fHitBits,i); }
  void     SetHitBits( UInt_t i ) { fHitBits = i; }
  void     SetUfailBits( UInt_t i ) { fUfailBits = i; }
  void     SetVfailBits( UInt_t i ) { fVfailBits = i; }
  Bool_t   TestHitBit( UInt_t i ) { return TESTBIT(fHitBits,i); }
  Int_t    Update( const TSolSimEvent::GEMCluster& cl );

private:

  Int_t    fType;        // GEANT particle number
  Int_t    fPID;         // Track particle ID (PDG)
  Int_t    fSector;      // Sector where this track detected
  Int_t    fSource;      // MC data set source (0 = signal, >0 background)
  TVector3 fOrigin;      // Position at first plane in lab frame (m)
  TVector3 fHitpos;      // Position at first plane in Tracker frame [m]
  TVector3 fMomentum;    // Momentum (GeV)
  Int_t    fHitBits;     // Bitpattern of plane nos hit by this back track
  Int_t    fUfailBits;   // Bitpattern of plane nos without digitized hits in U
  Int_t    fVfailBits;   // Bitpattern of plane nos without digitized hits in V

  ClassDef(TSolSimBackTrack,2) // MC track registered at first tracking chamber
};

//-----------------------------------------------------------------------------
// SoLID simulation decoder class
class TSolSimDecoder : public Podd::SimDecoder {
 public:
  //constructor may be inputed a data file to input some of the paramaters used by SimDecoder
  //NB: if the second file path does not select a valid file, default parameters will be used.
  TSolSimDecoder(const char* filedbpath = "");
  virtual ~TSolSimDecoder();
  
#if ANALYZER_VERSION_CODE >= 67072 // ANALYZER_VERSION(1,6,0)
  virtual Int_t LoadEvent( const UInt_t* evbuffer );
#else
  virtual Int_t LoadEvent( const Int_t* evbuffer );
#endif
  virtual void  Clear( Option_t* opt="" );
  virtual Int_t DefineVariables( THaAnalysisObject::EMode mode =
				 THaAnalysisObject::kDefine );
  virtual Podd::MCHitInfo GetMCHitInfo( Int_t crate, Int_t slot, Int_t chan, Double_t& mccharge) const;

  Int_t  GetNBackTracks() const { return fBackTracks->GetLast()+1; }

  TSolSimBackTrack* GetBackTrack( Int_t i ) const {
    TObject* obj = fBackTracks->UncheckedAt(i);
    assert( dynamic_cast<TSolSimBackTrack*>(obj) );
    return static_cast<TSolSimBackTrack*>(obj);
  }
  TSolSimGEMHit* GetGEMHit( Int_t i ) const {
    TObject* obj = fMCHits->UncheckedAt(i);
    assert( dynamic_cast<TSolSimGEMHit*>(obj) );
    return static_cast<TSolSimGEMHit*>(obj);
  }

  // Workaround for fubar THaEvData
#if ANALYZER_VERSION_CODE >= 67072  // ANALYZER_VERSION(1,6,0)
  static Int_t GetMAXSLOT() { return Decoder::MAXSLOT; }
#else
  static Int_t GetMAXSLOT() { return MAXSLOT; }
#endif

protected:
  typedef std::map<Int_t,Int_t> StripMap_t;

  // Event-by-event data
  TClonesArray*   fBackTracks; //-> Primary particle tracks at first chamber
  StripMap_t      fStripMap;   //! Map ROCKey -> index of corresponding strip

#if ANALYZER_VERSION_CODE >= 67072  // ANALYZER_VERSION(1,6,0)
  Int_t DoLoadEvent( const UInt_t* evbuffer );
#else
  Int_t DoLoadEvent( const Int_t* evbuffer );
#endif

  // void  StripToROC( Int_t s_plane, Int_t s_sector, Int_t s_proj, Int_t s_chan,
  //		    Int_t& crate, Int_t& slot, Int_t& chan ) const;
  Int_t StripFromROC( Int_t crate, Int_t slot, Int_t chan ) const;
  // Int_t MakeROCKey( Int_t crate, Int_t slot, Int_t chan ) const;

  ClassDef(TSolSimDecoder,0) // Decoder for simulated SoLID spectrometer data
};


#endif
