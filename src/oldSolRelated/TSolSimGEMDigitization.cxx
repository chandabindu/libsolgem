#include "TSolSimGEMDigitization.h"

#include "TCanvas.h"
#include "TFile.h"
#include "TMath.h"
#include "TTree.h"
#include "TClonesArray.h"

#include "TSolEVIOFile.h"  // needed for gendata class def
#include "TSolGEMData.h"
#include "TSolGEMVStrip.h"
#include "TSolSpec.h"
#include "TSolGEMChamber.h"
#include "TSolGEMPlane.h"
#include "TSolSimAux.h"
#include "TSolSimEvent.h"
#include "TSolDBManager.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <utility>

using namespace std;

static TSolDBManager* manager = TSolDBManager::GetInstance();
//for some reasons, if these parameters are declared as flags in the .h, it doesn't work...
Int_t    TSolSimGEMDigitization::fDoCrossTalk = 0;
Int_t    TSolSimGEMDigitization::fNCStripApart = 0;
Double_t TSolSimGEMDigitization::fCrossFactor = 0.;
Double_t TSolSimGEMDigitization::fCrossSigma = 0.;

// Chamber number -> sector/plane helper functions

inline
static void ChamberToSector( Short_t chamber, Short_t& sector, Short_t& plane )
{
  // Conversion from chamber index to sector/plane indices.
  // The meaning of the chamber index is not defined anywhere else this code.
  // The only requirement is that the database for TSolSpec matches whatever
  // is defined in the MC used to generate the input.
  // The database floating around at this time (February 2013) uses the definition
  // ich = is + nsectors*ipl (is = sector, ipl = plane).
  // The number of sectors is implied to be 30.

  div_t d = div( chamber, manager->GetNSector() );
  sector = d.rem;
  plane  = d.quot;
}

inline
static UInt_t MapSector( UInt_t chamber )
{
  // Convert the true chamber index to one with sector = 0
  return manager->GetNSector() * UInt_t(chamber/manager->GetNSector());
}

// Auxiliary class

TSolDigitizedPlane::TSolDigitizedPlane (UShort_t nstrip,
					UShort_t nsample,
					Int_t threshold )
  : fNSamples(nsample), fNStrips(nstrip), fThreshold(threshold)
{
  fType = new Short_t[nstrip];
  fCharge = new Float_t[nstrip];
  fTime = new Float_t[nstrip];
  fTotADC = new Int_t[nstrip];
  fOverThr = new Short_t[nstrip];

  fStripADC.Set(fNSamples*fNStrips);
  fStripClusters.resize(fNStrips);
  fRan.SetSeed(0);
  Clear();
};

TSolDigitizedPlane::~TSolDigitizedPlane()
{
  delete[] fType;
  delete[] fCharge;
  delete[] fTime;
  delete[] fTotADC;
  delete[] fOverThr;
};

void
TSolDigitizedPlane::Clear()
{
  fStripADC.Reset();
  memset( fType,   0, fNStrips*sizeof(Short_t) );
  memset( fTotADC, 0, fNStrips*sizeof(Int_t) );
  memset( fCharge, 0, fNStrips*sizeof(Float_t) );

  for (Int_t i = 0; i < fNStrips; i++) {
    fTime[i] = 9999.;
    fStripClusters[i].clear();
  }
  fNOT = 0;
}

void
TSolDigitizedPlane::Cumulate (const TSolGEMVStrip *vv, Short_t type,
			      Short_t clusterID )
{
  if (vv) {
    for( Int_t j=0; j < vv->GetSize(); j++ ) {
      Int_t idx = vv->GetIdx(j);
      assert( idx >= 0 && idx < fNStrips );
      fType[idx] |= type;
      fTime[idx] = (fTime[idx] < vv->GetTime()) ? fTime[idx] : vv->GetTime();
      fCharge[idx] += vv->GetCharge(j);
      bool was_below = !( fTotADC[idx] > fThreshold );
      for( UInt_t k=0; k<fNSamples; k++ ) {
	Int_t nnn = vv->GetADC(j,k);
	assert( nnn >= 0 );
	if( nnn == 0 ) continue;
	Int_t iadc = idx*fNSamples+k;
	fStripADC[iadc] = fStripADC[iadc] + nnn;
	fTotADC[idx] += nnn;
      }
      if( was_below && fTotADC[idx] > fThreshold ) {
	assert( fNOT < fNStrips );
	fOverThr[fNOT] = idx;
	++fNOT;
      }
      fStripClusters[idx].push_back(clusterID);
    }
    
    //do cross talk if requested, a big signal along the strips 
    //will induce a smaller signal as the bigger one going to the APV, 
    //the smaller signal will appear on strips that is 
    //about 32 channels away from the big signal
    if (!TSolSimGEMDigitization::fDoCrossTalk) return;
    Int_t isLeft = fRan.Uniform(1.) < 0.5 ? -1 : 1;
    Double_t factor = TSolSimGEMDigitization::fCrossFactor +
      fRan.Gaus(0., TSolSimGEMDigitization::fCrossSigma);
    if (factor <= 0.) return; //no induced signal
    
    for( Int_t j=0; j < vv->GetSize(); j++ ) {
      Int_t idx = vv->GetIdx(j);
      assert( idx >= 0 && idx < fNStrips );
      
      Int_t idxInduce = idx + isLeft*TSolSimGEMDigitization::fNCStripApart;
      if (idxInduce < 0 || idxInduce >= fNStrips ) continue; //outside the readout
      
      SETBIT(fType[idxInduce], kInducedStrip);
      //same time as the main signal strip
      fTime[idxInduce] = (fTime[idx] < vv->GetTime()) ? fTime[idx] : vv->GetTime();
      fCharge[idxInduce] += factor*vv->GetCharge(j);
      bool was_below = !( fTotADC[idxInduce] > fThreshold );
      for( UInt_t k=0; k<fNSamples; k++ ) {
	Int_t nnn = vv->GetADC(j,k);
	assert( nnn >= 0 );
	nnn *= factor;
	if( nnn == 0 ) continue;
	Int_t iadc = idxInduce*fNSamples+k;
	fStripADC[iadc] = fStripADC[iadc] + nnn;
	fTotADC[idxInduce] += nnn;
      }
      if( was_below && fTotADC[idxInduce] > fThreshold ) {
	assert( fNOT < fNStrips );
	fOverThr[fNOT] = idxInduce;
	++fNOT;
      }
    }
  }
};


UShort_t
TSolDigitizedPlane::Threshold( Int_t thr )
{
  // Find number of strips over threshold 'thr'
  // and build index table for GetIdxOverThr.
  // This needs to be called only if one wants a change the threshold value.

  fNOT = 0;
  fThreshold = thr;

  for (UInt_t j = 0; j < fNStrips; j++)
    {
      if (fTotADC[j] > thr)
	{
	  fOverThr[fNOT] = j;
	  fNOT++;
	}
    }

  return fNOT;
};


TSolSimGEMDigitization::TSolSimGEMDigitization( const TSolSpec& spect,
						const char* name )
  : THaAnalysisObject(name, "GEM simulation digitizer"),
    fDoMapSector(false), fSignalSector(0), fDP(0),  fdh(0), fNChambers(0), fNPlanes(0),
    fRNIon(0), fOFile(0), fOTree(0), fEvent(0)
{
  Init();
  Initialize (spect);
  fRIon.resize(fMaxNIon);
  
  fEvent = new TSolSimEvent(5);
}

TSolSimGEMDigitization::~TSolSimGEMDigitization()
{
  DeleteObjects();
}

void TSolSimGEMDigitization::DeleteObjects()
{
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip)
	delete fDP[ic][ip];
      delete[] fDP[ic];
    }
  delete[] fDP;       fDP = 0;
  delete[] fdh;       fdh = 0;
  delete[] fNPlanes;  fNPlanes = 0;

  delete fOFile;      fOFile = 0;
  delete fOTree;      fOTree = 0;
  delete fEvent;      fEvent = 0;
}

void
TSolSimGEMDigitization::Initialize(const TSolSpec& spect)
{
  // Initialize digitization structures based on parameters from given
  // spectrometer

  // Avoid memory leaks in case of reinitialization
  DeleteObjects();

  fNChambers = spect.GetNChambers();
  fDP = new TSolDigitizedPlane**[fNChambers];
  fNPlanes = new UInt_t[fNChambers];
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      fNPlanes[ic] = spect.GetChamber(ic).GetNPlanes();
      fDP[ic] = new TSolDigitizedPlane*[fNPlanes[ic]];
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip) {
	fDP[ic][ip] =
	  new TSolDigitizedPlane( spect.GetChamber(ic).GetPlane(ip).GetNStrips(),
				  fEleSamplingPoints, // # ADC samples
				  0 );                // threshold is zero for now
      }
    }
  fdh = NULL;
  
  // Estimated max size of the charge collection area in AvaModel
  Double_t pitch = 0.4; // [mm]
  Double_t f = ( 2 * fAvalancheFiducialBand * 0.1 /* fRSMax */ ) / pitch + 6 /* track slope */;
  Int_t est_area = TMath::Nint( fYIntegralStepsPerPitch * f*f );
  est_area = 128 * TMath::CeilNint( est_area/128. );
  fSumA.reserve(est_area);

  fDADC.resize(fEleSamplingPoints);
  fFilledStrips = true;
}

Int_t
TSolSimGEMDigitization::ReadDatabase (const TDatime& date)
{
  FILE* file = OpenFile (date);
  if (!file) return kFileError;

  const DBRequest request[] =
    {
      { "gasionwidth",               &fGasWion,                   kDouble },
      { "gasdiffusion",              &fGasDiffusion,              kDouble },
      { "gasdriftvelocity",          &fGasDriftVelocity,          kDouble },
      { "avalanchefiducialband",     &fAvalancheFiducialBand,     kDouble },
      { "avalanchechargestatistics", &fAvalancheChargeStatistics, kInt    },
      { "gainmean",                  &fGainMean,                  kDouble },
      { "gain0",                     &fGain0,                     kDouble },
      { "triggeroffset",             &fTriggerOffset,             kDouble },
      { "triggerjitter",             &fTriggerJitter,             kDouble },
      { "elesamplingpoints",         &fEleSamplingPoints,         kInt    },
      { "elesamplingperiod",         &fEleSamplingPeriod,         kDouble },
      { "pulsenoisesigma",           &fPulseNoiseSigma,           kDouble },
      { "pulsenoiseperiod",          &fPulseNoisePeriod,          kDouble },
      { "pulsenoiseampconst",        &fPulseNoiseAmpConst,        kDouble },
      { "pulsenoiseampsigma",        &fPulseNoiseAmpSigma,        kDouble },
      { "adcoffset",                 &fADCoffset,                 kDouble },
      { "adcgain",                   &fADCgain,                   kDouble },
      { "adcbits",                   &fADCbits,                   kInt    },
      { "gatewidth",                 &fGateWidth,                 kDouble },
      { "pulseshapetau0",            &fPulseShapeTau0,            kDouble },
      { "pulseshapetau1",            &fPulseShapeTau1,            kDouble },
      { "zrout",                     &fRoutZ,                     kDouble },
      { "use_tracker_frame",         &fUseTrackerFrame,           kInt    },
      { "entrance_ref",              &fEntranceRef,               kDouble },
      { "avalateraluncertainty",     &fLateralUncertainty,        kDouble },
      { "max_ion",                   &fMaxNIon,                   kUInt   },
      { "y_integral_step_per_pitch", &fYIntegralStepsPerPitch,    kUInt   },
      { "x_integral_step_per_pitch", &fXIntegralStepsPerPitch,    kUInt   },
      { "avalanche_range",           &fSNormNsigma,               kDouble },
      { "ava_model",                 &fAvaModel,                  kInt    },
      { "ava_gain",                  &fAvaGain,                   kDouble },
      { "do_crosstalk",              &fDoCrossTalk,               kInt    },
      { "crosstalk_mean",            &fCrossFactor,               kDouble },
      { "crosstalk_sigma",           &fCrossSigma,                kDouble },
      { "crosstalk_strip_apart",     &fNCStripApart,              kInt    },
     { 0 }
    };

  Int_t err = LoadDB (file, date, request, fPrefix);
  fclose(file);
  if (err)
    return kInitError;

  if( fEleSamplingPoints < 0 || fEleSamplingPoints > 10 )
    fEleSamplingPoints = 10;
  if( fADCbits < 1 || fADCbits > MAX_ADCBITS ) {
    Error("ReadDatabase", "Invalid parameter adcbits = %d", fADCbits );
    return kInitError;
  }
  fAvalancheFiducialBand = TMath::Abs(fAvalancheFiducialBand);

  return kOK;
}

Int_t
TSolSimGEMDigitization::Digitize (const TSolGEMData& gdata, const TSolSpec& spect)
{
  // Digitize event after clearing all previous digitization results.

  fEvent->Clear();
  fSignalSector = 0;  // safe default, will normally be overridden in AdditiveDigitize

  for (UInt_t ic = 0; ic < fNChambers; ++ic) {
    for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip)
      fDP[ic][ip]->Clear();
  }
  fFilledStrips = true;

  return AdditiveDigitize( gdata, spect );
}

Int_t
TSolSimGEMDigitization::AdditiveDigitize (const TSolGEMData& gdata, const TSolSpec& spect)
{
  // Digitize event. Add results to any existing digitized data.

  UInt_t nh = gdata.GetNHit();

  // For signal data, determine the sector of the primary track
  bool is_background = gdata.GetSource() != 0;
  if( fDoMapSector && !is_background ) {
    //originally the fSignalSector is determine from the phi angle of the track
    //at vertex, this is good if there is no field. When there is, we cannot do it
    //that way. So I changed it to the following. But still it doesn't work in there
    //are more than 1 primary signal particle, hopefully we don't need to consider this
    //-- Weizhi

    Int_t ntrk = fEvent->GetNtracks();
    if( ntrk == 0 && nh > 0 ) {
      Warning("Digitize", "Signal data without a primary track?");
    } else if( ntrk > 0 ) {
      if( ntrk > 1 )
	Warning("Digitize", "Multiple primary tracks in signal run?");

      TSolSimTrack* trk = static_cast<TSolSimTrack*>( fEvent->fMCTracks->At(0) );
      if( trk ) {
        //fSignalSector = gdata.GetSigSector();// CHECK ?
	Double_t ph = trk->PPhi();
	// Assumes phi doesn't change between vertex and GEMs (no field) and the
	// nominal angle (i.e. without offset) of sector 0 is 0 degrees
	fSignalSector = TMath::FloorNint(ph*manager->GetNSector()/TMath::TwoPi() + 0.5);
	if( fSignalSector < 0 ) fSignalSector += manager->GetNSector();
      } else
	Error("Digitize", "Null track pointer? Should never happen. Call expert.");
    }
  }
  if( nh == 0 ) return 0;

  // Map sectors of any background data to the signal sector, if so requested
  bool map_backgr = fDoMapSector && is_background;

  // Randomize the event time for background events
  UInt_t vsize = ( map_backgr ) ? manager->GetNSector() : 1;
  vector<Float_t> event_time(vsize);
  vector<bool> time_set(vsize,false);
  UInt_t itime = 0;

  for (UInt_t ih = 0; ih < nh; ++ih) {
    UInt_t igem = gdata.GetHitChamber (ih);
    if (igem >= fNChambers)
      continue;

    Short_t itype = (gdata.GetParticleType(ih)==1) ? 1 : 2; // primary = 1, secondaries = 2
    Short_t isect, iplane;
    ChamberToSector( igem, isect, iplane );
    if( fDoMapSector && !is_background && isect != fSignalSector )
      // If mapping sectors, skip signal hits that won't end up in sector 0
      continue;
    
    TVector3 vv1 = gdata.GetHitEntrance (ih);
    TVector3 vv2 = gdata.GetHitExit (ih);

    // These vectors are in the lab frame, we need them in the chamber frame
    // Also convert to mm

    TVector3 offset = spect.GetChamber(igem).GetOrigin() * 1000.0;
    Double_t angle = spect.GetChamber(igem).GetAngle();
    vv1 -= offset;
    vv2 -= offset;
    vv1.RotateZ (-angle);
    vv2.RotateZ (-angle);

    IonModel (vv1, vv2, gdata.GetHitEnergy(ih) );

    // Generate randomized event time (for background) and trigger time jitter
    if( map_backgr ) {
      // If mapping sectors, treat the hits from each sector like coming from
      // a separate event. As a result, each sector gets its own random event_time.
      // If not mapping sectors, itime = 0, and all hits get the same time offset.
      itime = isect;
    }
    if( !time_set[itime] ) {
      // Trigger time jitter, including an arbitrary offset to align signal timing
      Double_t trigger_jitter = fTrnd.Gaus(fTriggerOffset, fTriggerJitter);
      if( is_background ) {
	// For background data, uniformly randomize event time between
	// -fGateWidth to +75 ns (assuming 3 useful 25 ns samples).
	event_time[itime] = fTrnd.Uniform(fGateWidth + 3*fEleSamplingPeriod)
	  - fGateWidth - trigger_jitter;
      } else {
	// Signal events occur at t = 0, smeared only by the trigger jitter
	event_time[itime] = -trigger_jitter;
      }
      time_set[itime] = true;
    }
    // Time of the leading edge of this hit's avalance relative to the trigger
    Double_t time_zero = event_time[itime] + gdata.GetHitTime(ih) + fRTime0*1e9;

    if (fRNIon > 0) {
      fdh = AvaModel (igem, spect, vv1, vv2, time_zero);
    }
    // Record MC hits in output event
    //Short_t id = SetTreeHit (ih, spect, fdh, gdata, time_zero);
    Short_t id = SetTreeHit (ih, spect, gdata, time_zero);

    // Record digitized strip signals in output event
    if (fdh) {
      // If requested via fDoMapSector, accumulate all data in sector 0
      if( fDoMapSector ) {
	igem = MapSector(igem);
	if( !is_background ) {
	  assert( !fEvent->fGEMClust.empty() );
	  igem += fEvent->fGEMClust.back().fSector;
	}
      }
      for (UInt_t j = 0; j < 2; j++) {
	fDP[igem][j]->Cumulate (fdh[j], itype, id );
      }
    }
  }
  fFilledStrips = false;
  return 0;
}

void
TSolSimGEMDigitization::NoDigitize (const TSolGEMData& gdata, const TSolSpec& spect) // do not digitize event, just fill the tree
{
  //  if (!fEvCleared)  //?
    fEvent->Clear();
  UInt_t nh = gdata.GetNHit();

  for (UInt_t ih = 0; ih < nh; ++ih)
    {
      UInt_t igem = gdata.GetHitChamber (ih);
      if (igem >= fNChambers)
	continue;
      
      // Short_t id =
      //SetTreeHit (ih, spect, fdh, gdata, 0.0);
      SetTreeHit (ih, spect, gdata, 0.0);
    }
  SetTreeStrips ();
}



//.......................................................
// ionization Model
//

void
TSolSimGEMDigitization::IonModel(const TVector3& xi,
				 const TVector3& xo,
				 const Double_t elost ) // eV
{
#define DBG_ION 0

  TVector3 vseg = xo-xi; // mm

  // DEBUG  TRandom3 rnd(0);
  TRandom3& rnd = fTrnd;

  // ---- extract primary ions from Poisson
  fRNIon = rnd.Poisson(elost/fGasWion);

  if (fRNIon <=0)
    return;

#if DBG_ION > 0
  cout << "E lost = " << elost << ", " << fRNIon << " ions";
#endif
  if (fRNIon > fMaxNIon) {
#if DBG_ION > 0
    cout << __FUNCTION__ << ": WARNING: too many primary ions " << fRNIon << " limit to "
	 << fMaxNIon << endl;
#endif
    fRNIon = fMaxNIon;
  }

  fRSMax = 0.;
  fRTotalCharge = 0;
  fRTime0 = 999999.; // minimum time of drift

  for (UInt_t i=0;i<fRNIon;i++) { // first loop used to evaluate quantities
    IonPar_t ip;

    Double_t lion = rnd.Uniform(0.,1.); // position of the hit along the track segment (fraction)
    
    //In principle, the lateral uncertainty should have been put in the Ava model, but not here
    //But since we are not simulating the details of the avalanche, I think it is ok (Weizhi)
    ip.X = vseg.X()*lion+xi.X() + rnd.Gaus(0., fLateralUncertainty);
    ip.Y = vseg.Y()*lion+xi.Y() + rnd.Gaus(0., fLateralUncertainty);

    // Note the definition of fRoutZ is the distance from xi.Z() to xrout.Z():
    //        xi               xo   xrout
    // |<-LD->|<-----vseg----->|    |
    // |<-------fRoutZ---------|--->|
    // |      |<-lion*vseg->   |    |
    // |      |             <--LL-->|

    Double_t LD = TMath::Abs(xi.Z() - fEntranceRef);//usually should be 0,
                                            //unless particle is produced inside the gas layer

    Double_t LL = TMath::Abs(fRoutZ - LD - vseg.Z()*lion);
    Double_t ttime = LL/fGasDriftVelocity; // traveling time from the drift gap to the readout

    fRTime0 = TMath::Min(ttime, fRTime0); // minimum traveling time [s]

    ip.SNorm = TMath::Sqrt(2.*fGasDiffusion*ttime); // spatial sigma on readout [mm]

    if( fAvalancheChargeStatistics == 1 ) {
      Double_t gnorm = fGainMean/TMath::Sqrt(fGain0); // overall gain TBC
      ip.Charge = rnd.Gaus(fGainMean, gnorm); // Gaussian distribution of the charge
    }
    else {
      ip.Charge = rnd.Exp(fGainMean); // Furry distribution
    }

    if( ip.Charge > 0 )
      fRTotalCharge += ip.Charge;
    else
      ip.Charge = 0;

    fRSMax = TMath::Max(ip.SNorm, fRSMax);

    // Derived quantities needed by the numerical integration in AvaModel
    ip.SNorm *= fSNormNsigma;
    ip.R2 = ip.SNorm * ip.SNorm;
    ip.ggnorm = ip.Charge * TMath::InvPi() / ip.R2; // normalized charge

#if DBG_ION > 1
    printf("z coords %f %f %f %f lion %f LL %lf\n",
	   xi.Z(), xo.Z(), vseg.Z(), lion, LL);
    printf("ttime = %e\n", ttime);
#endif
#if DBG_ION > 0
    cout << "x, y = " << ip.X << ", " << ip.Y << " snorm = "
	 << ip.SNorm/fSNormNsigma << " charge " << ip.Charge << endl;
    cout << "fRTime0 = " << fRTime0 << endl;
#endif

    fRIon[i] = ip;
  }
}

//-------------------------------------------------------
// Helper functions for integration in AvaModel
inline static
Double_t IntegralY( Double_t* a, Int_t ix, Int_t nx, Int_t ny )
{
  register double sum = 0.;
  register int kx = ix*ny;
  for( Int_t jy = ny; jy != 0; --jy )
    sum += a[kx++];
  return sum;
}

inline static
Bool_t IsInActiveArea( const TSolGEMPlane& pl, Double_t xc, Double_t yc )
{
  pl.StripToLab(xc,yc);
  return pl.GetWedge().Contains(xc,yc);
}

//.......................................................
// avalanche model
//

TSolGEMVStrip **
TSolSimGEMDigitization::AvaModel(const Int_t ic,
				 const TSolSpec& spect,
				 const TVector3& xi,
				 const TVector3& xo,
				 const Double_t t0)
{
#define DBG_AVA 0

#if DBG_AVA > 0
  cout << "Chamber " << ic << "----------------------------------" << endl;
  cout << "In  " << xi.X() << " " << xi.Y() << " " << xi.Z() << endl;
  cout << "Out " << xo.X() << " " << xo.Y() << " " << xo.Z() << endl;
#endif

  // xi, xo are in chamber frame, in mm

  Double_t nsigma = fAvalancheFiducialBand; // coverage factor

  Double_t x0,y0,x1,y1; // lower and upper corners of avalanche diffusion area

  if (xi.X()<xo.X()) {
    x0 = xi.X()-nsigma*fRSMax;
    x1 = xo.X()+nsigma*fRSMax;
  } else {
    x1 = xi.X()+nsigma*fRSMax;
    x0 = xo.X()-nsigma*fRSMax;
  }

  if (xi.Y()< xo.Y()) {
    y0 = xi.Y()-nsigma*fRSMax;
    y1 = xo.Y()+nsigma*fRSMax;
  } else {
    y1 = xi.Y()+nsigma*fRSMax;
    y0 = xo.Y()-nsigma*fRSMax;
  }

  // Check if any part of the avalanche region is in the active area of the sector.
  // Here, "active area" means the chamber's *bounding box*, which is
  // larger than the wedge's active area (section of a ring)

  const TSolGEMChamber& chamber = spect.GetChamber(ic);
  Double_t glx = chamber.GetLowerEdgeX() * 1000.0;
  Double_t gly = chamber.GetLowerEdgeY() * 1000.0;
  Double_t gux = chamber.GetUpperEdgeX() * 1000.0;
  Double_t guy = chamber.GetUpperEdgeY() * 1000.0;

  if (x1<glx || x0>gux ||
      y1<gly || y0>guy) { // out of the sector's bounding box
    cerr << __FILE__ << " " << __FUNCTION__ << ": out of sector, "
	 << "chamber " << ic << " sector " << ic%30 << " plane " << ic/30 << endl
	 << "Following relations should hold:" << endl
	 << "(x1 " << x1 << ">glx " << glx << ") (x0 " << x0 << "<gux " << gux << ")" << endl
	 << "(y1 " << y1 << ">gly " << gly << ") (y0 " << y0 << "<guy " << guy << ")" << endl
	 << "r " << sqrt(x0*x0+y0*y0) << " phi " << atan(y0/x0)*TMath::RadToDeg() << endl;
    return 0;
  }

  bool bb_clipped = (x0<glx||y0<gly||x1>gux||y1>guy);
  if(x0<glx) x0=glx;
  if(y0<gly) y0=gly;
  if(x1>gux) x1=gux;
  if(y1>guy) y1=guy;

  // Loop over chamber planes

  TSolGEMVStrip **virs;
  virs = new TSolGEMVStrip *[fNPlanes[ic]];
  for (UInt_t ipl = 0; ipl < fNPlanes[ic]; ++ipl){
#if DBG_AVA > 0
    cout << "coordinate " << ipl << " =========================" << endl;
#endif
    
    // Compute strips affected by the avalanche

    const TSolGEMPlane& pl = chamber.GetPlane(ipl);

    // Positions in strip frame
    Double_t xs0 = x0 * 1e-3; Double_t ys0 = y0 * 1e-3;
    pl.PlaneToStrip (xs0, ys0);
    xs0 *= 1e3; ys0 *= 1e3;
    Double_t xs1 = x1 * 1e-3; Double_t ys1 = y1 * 1e-3;
    pl.PlaneToStrip (xs1, ys1);
    xs1 *= 1e3; ys1 *= 1e3;

#if DBG_AVA > 0
    cout << "xs0 ys0 xs1 ys1 " << xs0 << " " << ys0 << " " << xs1 << " " << ys1 << endl;
#endif

    Int_t iL = pl.GetStrip (xs0 * 1e-3, ys0 * 1e-3);
    Int_t iU = pl.GetStrip (xs1 * 1e-3, ys1 * 1e-3);

    // Check for (part of) the avalanche area being outside of the strip region
    if( iL < 0 && iU < 0 ) {
      // All of the avalanche outside -> nothing to do
      // TODO: what if this happens for only one strip coordinate (ipl)?
#if DBG_AVA > 0
      cerr << __FILE__ << " " << __FUNCTION__ << ": out of active area, "
	   << "chamber " << ic << " sector " << ic%30 << " plane " << ic/30 << endl
	   << "iL_raw " << pl.GetStripUnchecked(xs0*1e-3) << " "
	   << "iU_raw " << pl.GetStripUnchecked(xs1*1e-3) << endl
	   << "r " << sqrt(x0*x0+y0*y0) << " phi " << atan(y0/x0)*TMath::RadToDeg()
	   << endl << endl;
#endif
      if( ipl == 1 ) delete virs[0];
      delete [] virs;
      return 0;
    }
    bool clipped = ( iL < 0 || iU < 0 );
    if( iL < 0 )
      iL = pl.GetStripInRange( xs0 * 1e-3 );
    else if( iU < 0 )
      iU = pl.GetStripInRange( xs1 * 1e-3 );

    if (iL > iU)
      swap( iL, iU );

    //
    // Bounds of rectangular avalanche region, in strip frame
    //

    // Limits in x are low edge of first strip to high edge of last
#if DBG_AVA > 0
    cout << "iL gsle " << iL << " " << pl.GetStripLowerEdge (iL) << endl;
    cout << "iU gsue " << iU << " " << pl.GetStripUpperEdge (iU) << endl;
#endif
    Double_t xl = pl.GetStripLowerEdge (iL) * 1000.0;
    Double_t xr = pl.GetStripUpperEdge (iU) * 1000.0;

    // Limits in y are y limits of track plus some reasonable margin
    // We do this in units of strip pitch for convenience (even though
    // this is the direction orthogonal to the pitch direction)

    // Use y-integration step size of 1/10 of strip pitch (in mm)
    Double_t yq = pl.GetSPitch() * 1000.0 / fYIntegralStepsPerPitch;
    Double_t yb = ys0, yt = ys1;
    if (yb > yt)
      swap( yb, yt );
    yb = yq * TMath::Floor (yb / yq);
    yt = yq * TMath::Ceil  (yt / yq);

    //We should also allow x to have variable bin size based on the db
    //the new avalanche model (Cauchy-Lorentz) has a very sharp full width
    //half maximum, so if the bin size is too large, it can introduce
    //fairly large error on the charge deposition. Setting fXIntegralStepsPerPitch
    //to 1 will go back to the original version -- Weizhi Xiong

    Int_t nstrips = iU - iL + 1;
    Int_t nx = (iU - iL + 1) * fXIntegralStepsPerPitch;
    Int_t ny = TMath::Nint( (yt - yb)/yq );
#if DBG_AVA > 0
    cout << "xr xl yt yb nx ny "
	 << xr << " " << xl << " " << yt << " " << yb
	 << " " << nx << " " << ny << endl;
#endif
    assert( nx > 0 && ny > 0 );

    // define function, gaussian and sum of gaussian

    Double_t xbw = (xr - xl) / nx;
    Double_t ybw = (yt - yb) / ny;
#if DBG_AVA > 0
    cout << "xbw ybw " << xbw << " " << ybw << endl;
#endif
    fSumA.resize(nx*ny);
    memset (&fSumA[0], 0, fSumA.size() * sizeof (Double_t));
    for (UInt_t i = 0; i < fRNIon; i++){
      Double_t frxs = fRIon[i].X * 1e-3;
      Double_t frys = fRIon[i].Y * 1e-3;
      pl.PlaneToStrip (frxs, frys);
      frxs *= 1e3; frys *= 1e3;
      // bin containing center and # bins each side to process
      Int_t ix = (frxs-xl) / xbw;
      Int_t iy = (frys-yb) / ybw;
      Int_t dx = fRIon[i].SNorm / xbw  + 1;
      Int_t dy = fRIon[i].SNorm / ybw  + 1;
#if DBG_AVA > 1
      cout << "ix dx iy dy " << ix << " " << dx << " " << iy << " " << dy << endl;
#endif

      //
      // NL change:
      //
      // ggnorm is the avalance charge for the i^th ion, and R2 is the square of the radius of the diffusion 
      // circle, mutiplied by the kSNormNsigma factor: (ip.SNorm * ip.SNorm)*kSNormNsigma*kSNormNsigma. All 
      // strips falling within this circle are considered in charge summing. 
      //
      // The charge contribution to a given strip by the i^th ion is evaluated by a Lorentzian (or Gaussian)
      // distribution; the sigma for this distribution is eff_sigma, which is the actual avalance sigma. 
      //
      Double_t ggnorm = fRIon[i].ggnorm;
      Double_t r2 = fRIon[i].R2;
      Double_t eff_sigma = r2/(fSNormNsigma*fSNormNsigma);
      // xc and yc are center of current bin
      Int_t jx = max(ix-dx,0);
      Double_t xc = xl + (jx+0.5) * xbw;
      // Loop over bins
      for (; jx < min(ix+dx+1,nx); ++jx, xc += xbw){
	Double_t xd2 = frxs-xc; xd2 *= xd2;
	if( xd2 > r2 ) continue;
	Int_t jy = max(iy-dy,0);
	Double_t yc = yb + (jy+0.5) * ybw;
	
	for (; jy < min(iy+dy+1,ny); ++jy, yc += ybw){
	  Double_t yd2 = frys-yc; yd2 *= yd2;

	  if( xd2 + (frys-yc)*(frys-yc) <= r2 ) {
	    if( (clipped || bb_clipped) && !IsInActiveArea(pl,xc*1e-3,yc*1e-3) )
	      continue;
	    switch (fAvaModel){
	    case 0:
	      // Original Heavyside distribution 
	      fSumA[jx*ny+jy] += ggnorm;
	      break;
	    case 1:
	      // Gaussian with no extra multiplier
	      fSumA[jx*ny+jy] += 
		fAvaGain*ggnorm*exp(-1.*(xd2+yd2)/(2.*r2/(fSNormNsigma*fSNormNsigma)));
	      break;
	    default:
	      // Cauchy-Larentz: 
	      fSumA[jx*ny+jy] += 
		fAvaGain*ggnorm*(1./(TMath::Pi()*eff_sigma))*(eff_sigma*eff_sigma)
		/((xd2+yd2)+eff_sigma*eff_sigma);
	    }
	  }
	}
      }
    }

#if DBG_AVA > 0
    cout << "t0 = " << t0 << " plane " << ipl 
	 << endl;
#endif

    virs[ipl] = new TSolGEMVStrip(nx,fEleSamplingPoints);

    virs[ipl]->SetTime(t0);
    virs[ipl]->SetHitCharge(fRTotalCharge);

    Int_t ai=0;
    Double_t area = xbw * ybw;

    //when we integrate in order to get the signal pulse, we want all charge
    //deposition on the area of a single strip -- Weizhi
    for (Int_t j = 0; j < nstrips; j++){
      Int_t posflag = 0;
      Double_t us = 0.;
      for (UInt_t k=0; k<fXIntegralStepsPerPitch; k++){
	us += IntegralY( &fSumA[0], j * fXIntegralStepsPerPitch + k, nx, ny ) * area;
      }
            
      //generate the random pedestal phase and amplitude
      Double_t phase = fTrnd.Uniform(0., fPulseNoisePeriod);
      Double_t amp = fPulseNoiseAmpConst + fTrnd.Gaus(0., fPulseNoiseAmpSigma);

      for (Int_t b = 0; b < fEleSamplingPoints; b++){
	Double_t pulse =
	  TSolSimAux::PulseShape (fEleSamplingPeriod * b - t0,
				  us,
				  fPulseShapeTau0,
				  fPulseShapeTau1 );

	//nx is larger than the size of the strips that are actually being hit,
	//however, this way of adding noise will add signals to those strips that were not hit
	//and the cluster size will essentially equal to nx
	//not sure if this is what we what...
	// if( fPulseNoiseSigma > 0.)
	// pulse += fTrnd.Gaus(0., fPulseNoiseSigma);

	//add noise only to those strips that are hit,
	if( fPulseNoiseSigma > 0. && pulse > 0. )
	  pulse += GetPedNoise(phase, amp, b);

	Short_t dadc = TSolSimAux::ADCConvert( pulse,
					       fADCoffset,
					       fADCgain,
					       fADCbits );

	fDADC[b] = dadc;
	posflag += dadc;
      }
      if (posflag > 0) { // store only strip with signal
	for (Int_t b = 0; b < fEleSamplingPoints; b++)
	  virs[ipl]->AddSampleAt (fDADC[b], b, ai);
	virs[ipl]->AddStripAt (iL+j, ai);
	virs[ipl]->AddChargeAt (us, ai);
	ai++;
      }
    }

    virs[ipl]->SetSize(ai);
  }
  
  return virs;
}
//___________________________________________________________________________________
inline Double_t TSolSimGEMDigitization::GetPedNoise(Double_t &phase, Double_t& amp, Int_t& isample)
{
  Double_t thisPhase = phase + isample*fEleSamplingPeriod;
  return fTrnd.Gaus(0., fPulseNoiseSigma)
         + amp*sin(2.*TMath::Pi()/fPulseNoisePeriod*thisPhase);
}
//___________________________________________________________________________________
void
TSolSimGEMDigitization::Print() const
{
  cout << "GEM digitization:" << endl;
  cout << "  Gas parameters:" << endl;
  cout << "    Gas ion width: " << fGasWion << endl;
  cout << "    Gas diffusion: " << fGasDiffusion << endl;
  cout << "    Gas drift velocity: " << fGasDriftVelocity << endl;
  cout << "    Avalanche fiducial band: " << fAvalancheFiducialBand << endl;
  cout << "    Avalanche charge statistics: " << fAvalancheChargeStatistics << endl;
  cout << "    Gain mean: " << fGainMean << endl;
  cout << "    Gain 0: " << fGain0 << endl;

  cout << "  Electronics parameters:" << endl;
  cout << "    Trigger offset: " << fTriggerOffset << endl;
  cout << "    Trigger jitter: " << fTriggerJitter << endl;
  cout << "    Sampling Period: " << fEleSamplingPeriod << endl;
  cout << "    Sampling Points: " << fEleSamplingPoints   << endl;
  cout << "    ADC offset: " << fADCoffset << endl;
  cout << "    ADC gain: " << fADCgain << endl;
  cout << "    ADC bits: " << fADCbits << endl;
  cout << "    Gate width: " << fGateWidth << endl;

  cout << "  GEM pedestal noise parameters: "<<endl;
  cout << "    Pulse Noise period: " << fPulseNoisePeriod << endl;
  cout << "    Pulse Noise amplitude sigma: " << fPulseNoiseAmpSigma << endl;
  cout << "    Pulse Noise amplitude constant: " << fPulseNoiseAmpConst << endl;
  cout << "    Additional Pulse Noise width: " << fPulseNoiseSigma << endl;
  
  cout << "  Pulse shaping parameters:" << endl;
  cout << "    Pulse shape tau0: " << fPulseShapeTau0 << endl;
  cout << "    Pulse shape tau1: " << fPulseShapeTau1 << endl;
}

void
TSolSimGEMDigitization::PrintCharges() const
{
  cout << " Chb  Pln  Strip  Typ    ADC    Charge      Time\n";
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip)
	for (UInt_t ist = 0; ist < (UInt_t) GetNStrips(ic, ip); ++ist)
	  {
	    if (fDP[ic][ip]->GetCharge (ist) > 0)
	      cout << setw(4) << ic
		   << " " << setw(4) << ip
		   << " " << setw(6) << ist
		   << " " << setw(4) << GetType (ic, ip, ist)
		   << " " << setw(6) << GetTotADC (ic, ip, ist)
		   << fixed << setprecision(1)
		   << " " << setw(9) << GetCharge (ic, ip, ist)
		   << fixed << setprecision(3)
		   << " " << setw(9) << GetTime (ic, ip, ist)
		   << endl;
	  }
    }
}


void
TSolSimGEMDigitization::PrintSamples() const
{
  cout << " Chb  Pln  Strip  Typ    ADC samples \n";
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip)
	for (UInt_t ist = 0; ist < (UInt_t) GetNStrips (ic, ip); ++ist)
	  if (GetCharge (ic, ip, ist) > 0)
	    {
	      cout << setw(4) << ic
		   << " " << setw(4) << ip
		   << " " << setw(6) << ist
		   << " " << setw(4) << GetType (ic, ip, ist);
	      for (UInt_t is = 0; is < (UInt_t) GetNSamples (ic, ip); ++is)
		cout << " " << setw(6) << GetADC (ic, ip, ist, is);
	      cout << endl;
	    }
    }
}

// Tree methods
void
TSolSimGEMDigitization::InitTree (const TSolSpec& spect, const TString& ofile)
{
  fOFile = new TFile( ofile, "RECREATE");

  if (fOFile == 0 || fOFile->IsZombie() )
    {
      cerr << "Error: cannot open output file " << ofile << endl;
      delete fOFile; fOFile = 0;
      return;
    }

  fOTree = new TTree( treeName, "Tree of digitized values");

  // create the tree variables

  fOTree->Branch( eventBranchName, "TSolSimEvent", &fEvent );

}

void
TSolSimGEMDigitization::SetTreeEvent (const TSolGEMData& tsgd,
				      const TSolEVIOFile& f, Int_t evnum )
{
  // Set overall event info.
  fEvent->Clear("all");
  fEvent->fRunID = tsgd.GetRun();
  // FIXME: still makes sense if background added?
  fEvent->fEvtID = (evnum < 0) ? tsgd.GetEvent() : evnum;
  for( UInt_t i=0; i<f.GetNGen(); ++i ) {
    const gendata* gd = f.GetGenData(i);
    //TODO: get GEANT id?
    fEvent->AddTrack( i+1, gd->GetPID(),
		      gd->GetV()*1e-3, // Vertex coordinates in [m]
		      gd->GetP()*1e-3  // Momentum in [GeV]
		      );
  }
  // FIXME: either only one GenData per event, or multiple weights per event
  if( f.GetNGen() > 0 )
    fEvent->fWeight = f.GetGenData(0)->GetWeight();

  fEvent->fSectorsMapped = fDoMapSector;
  //fEvent->fSignalSector = tsgd.GetSigSector();//CHECK ?
  fEvent->fSignalSector = fSignalSector;
}

Short_t
TSolSimGEMDigitization::SetTreeHit (const UInt_t ih,
				    const TSolSpec& spect,
				    //TSolGEMVStrip* const *dh,
				    const TSolGEMData& tsgd,
				    Double_t t0 )
{
  // Sets the variables in fEvent->fGEMClust describing a hit
  // This is later used to fill the tree.

  TSolSimEvent::GEMCluster clust;

  UInt_t igem = tsgd.GetHitChamber(ih);
  ChamberToSector( igem, clust.fRealSector, clust.fPlane );
  clust.fSector   = clust.fRealSector; // May change if mapped, see below
  clust.fSource   = tsgd.GetSource();  // Source of this hit (0=signal, >0 background)
  clust.fType     = tsgd.GetParticleType(ih);   // GEANT particle counter
  clust.fTRID     = tsgd.GetTrackID(ih);   // GEANT particle counter
  clust.fPID      = tsgd.GetParticleID(ih); // PDG PID
  clust.fP        = tsgd.GetMomentum(ih)    * 1e-3; // [GeV]
  clust.fXEntry   = tsgd.GetHitEntrance(ih) * 1e-3; // [m]
  // The best estimate of the "true" hit position is the center of the
  // ionization region
  clust.fMCpos    = (tsgd.GetHitEntrance(ih)+tsgd.GetHitExit(ih)) * 5e-4; // [m]

  // Calculate hit position in the Tracker frame. This is fMCpos relative to
  // the origin of first plane of the sector, but rotated by the nominal
  // (non-offset) sector angle.
  // NB: assumes even sector spacing, clockwise numbering and sector 0 at 0 deg
  Double_t sector_angle = TMath::TwoPi()*clust.fSector/manager->GetNSector();
  if (!fUseTrackerFrame){
    clust.fHitpos = clust.fMCpos;// - spect.GetChamber(clust.fSector).GetOrigin(); //let's not use this tracker frame, Weizhi Xiong
  }else{
    clust.fHitpos = clust.fMCpos - spect.GetChamber(clust.fSector).GetOrigin();
  }
  clust.fHitpos.RotateZ(-sector_angle);

  if (fdh != NULL && fdh[0] != NULL)
    clust.fCharge = fdh[0]->GetHitCharge();
  else
    clust.fCharge = 0;
  clust.fTime   = t0;  // [ns]

  const TSolGEMChamber& ch = spect.GetChamber(igem);
  for (UInt_t j = 0; j < 2; j++) {
    if (fdh != NULL && fdh[j] != NULL)
      {
	clust.fSize[j]  = fdh[j]->GetSize();
	clust.fStart[j] = (clust.fSize[j] > 0) ? fdh[j]->GetIdx(0) : -1;
      }
    else
      {
	clust.fSize[j] = 0;
	clust.fStart[j] = 0;
      }
    const TSolGEMPlane& pl = ch.GetPlane(j);
    Double_t proj_angle = pl.GetAngle() + pl.GetSAngle() - sector_angle;
    TVector3 hitpos(clust.fHitpos);
    hitpos.RotateZ(-proj_angle);
    clust.fXProj[j] = hitpos.X();
  }

  clust.fID     = fEvent->fGEMClust.size()+1;
  clust.fVertex = tsgd.GetVertex (ih);

  if( fDoMapSector ) {
    // If sector mapping requested:
    // Signal sectors numbers are rotated by -fSignalSector so that
    // primary particle hits end up in sector 0 (not necessarily all
    // the secondaries, though!)
    Double_t rot;
    if( clust.fSource == 0 ) {
      rot = -TMath::TwoPi()*fSignalSector/manager->GetNSector();
      clust.fSector -= fSignalSector;
      if( clust.fSector < 0 )
	clust.fSector += manager->GetNSector();
    }
    else {
      // All background hits are mapped into sector 0
      rot = -sector_angle;
      clust.fSector = 0;
    }
    clust.fP.RotateZ(rot);
    clust.fXEntry.RotateZ(rot);
    clust.fMCpos.RotateZ(rot);
  }

  fEvent->fGEMClust.push_back( clust );

  if( clust.fPlane == 0 && clust.fType == 1 && clust.fSource == 0 )
    fEvent->fNSignal++;

  return clust.fID;
}

void
TSolSimGEMDigitization::SetTreeStrips()
{
  // Sets the variables in fEvent->fGEMStrips describing strip signals
  // This is later used to fill the tree.

  fEvent->fGEMStrips.clear();

  TSolSimEvent::DigiGEMStrip strip;
  for (UInt_t ich = 0; ich < GetNChambers(); ++ich) {
    ChamberToSector( ich, strip.fSector, strip.fPlane );

    // The "plane" here is actually the projection (= readout coordinate).
    // TSolGEMChamber::ReadDatabase associates the name suffix "x" with
    // the first "plane", and "y", with the second. However, strip angles can
    // be different for each chamber, so what's "x" in one chamber may very
    // well be something else, like "x'", in another. These angles don't even
    // have to match anything in the Monte Carlo.
    for (UInt_t ip = 0; ip < GetNPlanes (ich); ++ip) {
      strip.fProj = (Short_t) ip;
      strip.fNsamp = TMath::Min((UShort_t)MC_MAXSAMP,
				(UShort_t)GetNSamples(ich, ip));
      UInt_t nover = GetNOverThr(ich, ip);
      for (UInt_t iover = 0; iover < nover; iover++) {
	Short_t idx = GetIdxOverThr(ich, ip, iover);
	strip.fChan = idx;

	for (UInt_t ss = 0; ss < strip.fNsamp; ++ss)
	  strip.fADC[ss] = GetADC(ich, ip, idx, ss);

	strip.fSigType = GetType(ich, ip, idx);
	strip.fCharge  = GetCharge(ich, ip, idx);
	strip.fTime1   = GetTime(ich, ip, idx);

	const vector<Short_t>& sc = GetStripClusters(ich, ip, idx);
	strip.fClusters.Set( sc.size(), &sc[0] );

	fEvent->fGEMStrips.push_back( strip );
      }
    }
  }
  fFilledStrips = true;
}

void
TSolSimGEMDigitization::FillTree ()
{
  if( !fFilledStrips )
    SetTreeStrips();
  
  //fOFile = fOTree->GetCurrentFile();//CHECK ?
  if (fOFile && fOTree
      // added this line to not write events where there are no entries

      // Remove for background study
      //      && fEvent->fGEMStrips.size() > 0 && fEvent->fGEMClust.size() > 0

      )
    {
      fOFile->cd();
      fOTree->Fill();
    }
}

void
TSolSimGEMDigitization::WriteTree () const
{
  if (fOFile && fOTree) {
    fOFile->cd();
    fOTree->Write();
  }
}

void
TSolSimGEMDigitization::CloseTree () const
{
  if (fOFile) fOFile->Close();
}

