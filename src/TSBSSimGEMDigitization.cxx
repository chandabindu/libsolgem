#include "TSBSSimGEMDigitization.h"

#include "TCanvas.h"
#include "TFile.h"
#include "TMath.h"
#include "TTree.h"
#include "TClonesArray.h"

#include "TSBSGeant4File.h"  // needed for g4sbsgendata class def
#include "TSBSGEMSimHitData.h"
#include "TSBSGEMHit.h"
#include "TSBSSpec.h"
#include "TSBSGEMChamber.h"
#include "TSBSGEMPlane.h"
#include "TSBSSimAuxi.h"
#include "TSBSSimEvent.h"
#include "TSBSDBManager.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <utility>
#include <time.h>


using namespace std;

static TSBSDBManager* manager = TSBSDBManager::GetInstance();
//for some reasons, if these parameters are declared as flags in the .h, it doesn't work...
Int_t    TSBSSimGEMDigitization::fDoCrossTalk = 0;
Int_t    TSBSSimGEMDigitization::fNCStripApart = 0;
Double_t TSBSSimGEMDigitization::fCrossFactor = 0.;
Double_t TSBSSimGEMDigitization::fCrossSigma = 0.;


// Auxiliary class

TSBSDigitizedPlane::TSBSDigitizedPlane (UShort_t nstrip,
					UShort_t nsample,
					Int_t threshold )
  : fNSamples(nsample), fNStrips(nstrip), fThreshold(threshold)
{
  // initialization of all arrays
  fType = new Short_t[nstrip];
  fCharge = new Float_t[nstrip];
  fTime = new Float_t[nstrip];
  fTotADC = new Int_t[nstrip];
  fOverThr = new Short_t[nstrip];

  fStripADC.Set(fNSamples*fNStrips);
  fStripClusters.resize(fNStrips);
  fStripWeightInCluster.resize(fNStrips);
  for(int i=0; i<nsample; i++)
    fStripClusterADC[i].resize(fNStrips);
  Clear();
};

TSBSDigitizedPlane::~TSBSDigitizedPlane()
{
  delete[] fType;
  delete[] fCharge;
  delete[] fTime;
  delete[] fTotADC;
  delete[] fOverThr;
};

void
TSBSDigitizedPlane::Clear()
{
  fStripADC.Reset();
  memset( fType,   0, fNStrips*sizeof(Short_t) );
  memset( fTotADC, 0, fNStrips*sizeof(Int_t) );
  memset( fCharge, 0, fNStrips*sizeof(Float_t) );

  for (Int_t i = 0; i < fNStrips; i++) {
    fType[i]   =  0;
    fCharge[i] =  0;
    fTime[i]   = 9999.;
    fStripClusters[i].clear();
    fStripWeightInCluster[i].clear();
  for(int its=0; its<fNSamples; its++)
    fStripClusterADC[its][i].clear();
  }
  fNOT = 0;
}

void
TSBSDigitizedPlane::Cumulate (const TSBSGEMHit *vv, Short_t type,
			      Short_t clusterID )
{
  // cumulate hits (strips signals)
  if (vv) {
    //if(vv->GetSize()>20) {cout<<vv->GetSize();getchar();}
    for( Int_t j=0; j < vv->GetSize(); j++ ) {
      Double_t tempSumADC=0;
      Int_t idx = vv->GetIdx(j);
      assert( idx >= 0 && idx < fNStrips );
      fType[idx] |= type;
      fTime[idx] = (fTime[idx] < vv->GetTime()) ? fTime[idx] : vv->GetTime();
      fCharge[idx] += vv->GetCharge(j);
      bool was_below = !( fTotADC[idx] > fThreshold );
      for( UInt_t k=0; k<fNSamples; k++ ) {
	Int_t nnn = vv->GetADC(j,k);
	fStripClusterADC[k][idx].push_back(nnn);// new
	//cout << nnn << " ";
	assert( nnn >= 0 );
	if( nnn == 0 ) continue;
	Int_t iadc = idx*fNSamples+k;
	//cout << fStripADC[iadc] << " ";
	fStripADC[iadc] = fStripADC[iadc] + nnn;

	//cout << fStripADC[iadc] << " ";
	fTotADC[idx] += nnn;
	tempSumADC   += nnn;
      }//cout << endl;
      if( was_below && fTotADC[idx] > fThreshold ) {
	assert( fNOT < fNStrips );
	fOverThr[fNOT] = idx;
	++fNOT;
      }
      fStripWeightInCluster[idx].push_back(tempSumADC/vv->GetTotalADC());
      //cout<<((Double_t)fTotADC[idx])/vv->GetTotalADC()<<endl; getchar();
      fStripClusters[idx].push_back(clusterID);

    }
    
    //do cross talk if requested, a big signal along the strips 
    //will induce a smaller signal as the bigger one going to the APV, 
    //the smaller signal will appear on strips that is 
    //about 32 channels away from the big signal
    if (!TSBSSimGEMDigitization::fDoCrossTalk) return;
    Int_t isLeft = fRan.Uniform(1.) < 0.5 ? -1 : 1;
    Double_t factor = TSBSSimGEMDigitization::fCrossFactor +
      fRan.Gaus(0., TSBSSimGEMDigitization::fCrossSigma);
    if (factor <= 0.) return; //no induced signal
    
    for( Int_t j=0; j < vv->GetSize(); j++ ) {
      Int_t idx = vv->GetIdx(j);
      assert( idx >= 0 && idx < fNStrips );
      
      Int_t idxInduce = idx + isLeft*TSBSSimGEMDigitization::fNCStripApart;
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
TSBSDigitizedPlane::Threshold( Int_t thr )
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


TSBSSimGEMDigitization::TSBSSimGEMDigitization( const TSBSSpec& spect,
						const char* name)
  : THaAnalysisObject(name, "GEM simulation digitizer"),
    fDoMapSector(false), fSignalSector(0), fDP(0), fdh(0), fNChambers(0), fNPlanes(0),
    fRNIon(0), fOFile(0), fOTree(0), fEvent(0)
{
  Init();
  Initialize (spect);
  fRIon.resize(fMaxNIon);
  fTriggerOffset.resize(manager->GetNChamber());

  fEvent = new TSBSSimEvent(5);
}

TSBSSimGEMDigitization::~TSBSSimGEMDigitization()
{
  fEvent->Clear("all");
  DeleteObjects();
}

void TSBSSimGEMDigitization::DeleteObjects()
{
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip){
	delete fDP[ic][ip];
	//delete fdh[ip]
      }
      delete[] fDP[ic];
    }
  delete[] fDP;       fDP = 0;
  delete[] fdh;       fdh = 0;
  delete[] fNPlanes;  fNPlanes = 0;

  delete fOFile;      fOFile = 0;
  delete fOTree;      fOTree = 0;
  // fEvent->Clear("all");
  delete fEvent;      fEvent = 0;
}

void
TSBSSimGEMDigitization::Initialize(const TSBSSpec& spect)
{
  // Initialize digitization structures based on parameters from given
  // spectrometer

  // Avoid memory leaks in case of reinitialization
  DeleteObjects();

  fNChambers = spect.GetNChambers();
  fDP = new TSBSDigitizedPlane**[fNChambers];
  fNPlanes = new UInt_t[fNChambers];
  for (UInt_t ic = 0; ic < fNChambers; ++ic)
    {
      fNPlanes[ic] = spect.GetChamber(ic).GetNPlanes();
      fDP[ic] = new TSBSDigitizedPlane*[fNPlanes[ic]];
      for (UInt_t ip = 0; ip < fNPlanes[ic]; ++ip) {
	fDP[ic][ip] =
	  new TSBSDigitizedPlane( spect.GetChamber(ic).GetPlane(ip).GetNStrips(),
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
  // fSumA.reserve(est_area);

  fDADC.resize(fEleSamplingPoints);
  fFilledStrips = true;
}

Int_t
TSBSSimGEMDigitization::ReadDatabase (const TDatime& date)
{
  FILE* file = OpenFile (date);
  if (!file) return kFileError;
  
  vector<Double_t>* offset = 0;

  try{
    offset = new vector<Double_t>;
    const DBRequest request[] =
      {
	{ "gasionwidth",               &fGasWion,                   kDouble },
	{ "gasdiffusion",              &fGasDiffusion,              kDouble },
	{ "gasdriftvelocity",          &fGasDriftVelocity,          kDouble },
	{ "avalanchefiducialband",     &fAvalancheFiducialBand,     kDouble },
	{ "avalanchechargestatistics", &fAvalancheChargeStatistics, kInt    },
	{ "gainmean",                  &fGainMean,                  kDouble },
	{ "gain0",                     &fGain0,                     kDouble },
	{ "triggeroffset",             offset,                      kDoubleV},
	//{ "triggeroffset",             &fTriggerOffset,             kDouble },
	{ "triggerjitter",             &fTriggerJitter,             kDouble },
	{ "apv_time_jitter",           &fAPVTimeJitter,             kDouble },
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
    
    cout << manager->GetNChamber() << "  " << offset->size() << endl;
    assert((Int_t)offset->size() == manager->GetNChamber());
    for (UInt_t i=0; i<offset->size(); i++){
      fTriggerOffset.push_back(offset->at(i));
    }
    
    delete offset;
  }  catch(...) {
    delete offset;
    fclose(file);
    throw;
  }
  
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
TSBSSimGEMDigitization::Digitize (const TSBSGEMSimHitData& gdata, const TSBSSpec& spect)
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
TSBSSimGEMDigitization::AdditiveDigitize (const TSBSGEMSimHitData& gdata, const TSBSSpec& spect)
{
  
  // Digitize event. Add results to any existing digitized data.
  
  UInt_t nh = gdata.GetNHit();

  // For signal data, determine the sector of the primary track
  bool is_background = gdata.GetSource() != 0;
  if( nh == 0 ) {
    //cout << "no hit, doing nothing " << endl;
    return 0;
  }
  
  // Randomize the event time for background events
  Float_t event_time=0,time_zero=0;
  // Trigger time jitter, This should be a fixed value for different hits in a certain event.
  Double_t trigger_jitter = fTrnd.Gaus(0,fTriggerJitter);
  //fTrnd.Uniform(-fAPVTimeJitter/2, fAPVTimeJitter/2);

  for (UInt_t ih = 0; ih < nh; ++ih) {  
    UInt_t igem = gdata.GetHitChamber (ih);
    //    UInt_t imodule = gdata.GetHitModule(ih);
    UInt_t iplane = gdata.GetHitPlane(ih);
    //cout<<igem<<":"<<imodule<<":"<<iplane<<endl;
    if (igem >= fNChambers)
      {
	cout<<"GEM ID out of range set in database"<<endl;
	continue;
      }
    Short_t itype = (gdata.GetParticleType(ih)==1) ? 1 : 2; // primary = 1, secondaries = 2
    // if(gdata.GetParticleType(ih)!=1){cout<<"x"<<endl;getchar();}

    // These vectors are in the spec frame, we need them in the chamber frame
    TVector3 vv1 = gdata.GetHitEntrance (ih);
    TVector3 vv2 = gdata.GetHitExit (ih);
    
    if(abs(vv1.X()-vv2.X())>50 || abs(vv1.Y()-vv2.Y())>50){//in mm
      //cout<<abs(vv1.X()-vv2.X())<<endl;
      //getchar();
      continue;
    }
    IonModel (vv1, vv2, gdata.GetHitEnergy(ih) );
  
    // Get Signal Start Time 'time_zero'
    if( is_background ) {
      // For background data, uniformly randomize event time between
      // -fGateWidth to +75 ns (assuming 3 useful 25 ns samples).
      // Not using HitTime from simulation file but randomize HitTime to cycle use background files
      event_time = fTrnd.Uniform((-fGateWidth+2*fEleSamplingPeriod), 8*fEleSamplingPeriod);
    } else {
      // Signal events occur at t = 0, 
      event_time = gdata.GetHitTime(ih);
    }
    //  cout<<event_time<<"  "<<ih<<endl;
    // Adding drift time and trigger_jitter
    time_zero = event_time - fTriggerOffset[iplane] + fRTime0*1e9 - trigger_jitter;
   
#if DBG_AVA > 0
    if(time_zero>200.0)
      cout << "time_zero " << time_zero 
	   << "; evt time " << event_time 
	   << "; hit time " << gdata.GetHitTime(ih)
	   << "; drift time " << fRTime0*1e9
	   << endl;
#endif
    if (fRNIon > 0) {
  
      fdh = AvaModel (igem, spect, vv1, vv2, time_zero);
  
    }
    // for (UInt_t j = 0; j < 2; j++) {
    //   cout << "after filling (j = " << j << ") : " << endl;
    //   fdh[j]->Print();
    // }
    //cout << ih << " t_0 " << time_zero << " RNIon " << fRNIon << ", fdh " << fdh << endl;
    // vv1.Print();
    // vv2.Print();
    
    // Record MC hits in output event
    // Short_t id = SetTreeHit (ih, spect, fdh, gdata, time_zero);
    Short_t id = SetTreeHit (ih, spect, gdata, time_zero);
    
    // Record digitized strip signals in output event
    if (fdh) {
      
      //cout << " igem = " << igem << " iplane = " << iplane << endl;
      for (UInt_t j = 0; j < 2; j++) {
	// cout << "before cumulate (j = " << j << ") : " << endl;
	// fdh[j]->Print();
	//cout << j << " digitized planes: number of strips " << fDP[igem][j]->GetNStrips() << ", fdh.size = " 
	//   << fdh[j]->GetSize() << " , fdh[]->GetIndex(0) " << fdh[j]->GetIdx(0) << endl;
	fDP[igem][j]->Cumulate (fdh[j], itype, id );
	// cout << "after cumulate (j = " << j << ") : " << endl;
	// fdh[j]->Print();

	delete fdh[j]; //Danning: This line is newly added, because one needs to release memory fdh points if one really wants to points fdh to NULL in the following line
	               //         On the other hand, fdh seems to be specific to a certain GEM thus shouldn't be member of TSBSSimGEMDigitization and should
	               //         only exist within this function(additiveDigitize)  ???
      }
      delete fdh;      //Danning: added along with the line above, it's needed because the code will try to point fdh to another address for next hits on gem chamber
      fdh = NULL; //Danning: shouldn't delete pointer before delete the memory it points, or leading to memory leak
    }
    
  }
  fFilledStrips = false;
  return 0;
}

void
TSBSSimGEMDigitization::NoDigitize (const TSBSGEMSimHitData& gdata, const TSBSSpec& spect) // do not digitize event, just fill the tree
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
TSBSSimGEMDigitization::IonModel(const TVector3& xi,
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
  cout << "E lost = " << elost << ", " << fRNIon << " ions" << endl;
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
    
    //cout << " rout Z  (mm?) " << fRoutZ << ", LD (mm?) " << LD << " vseg Z (mm?) " << vseg.Z()  << endl;
    //  cout << " travelling length (mm?) " << LL << ", travelling time:  " <<  ttime << endl;
    
    fRTime0 = TMath::Min(ttime, fRTime0); // minimum traveling time [s]

    ip.SNorm = TMath::Sqrt(2.*fGasDiffusion*ttime); // spatial sigma on readout [mm]
    // cout<<"ip.SNorm: "<<ip.SNorm<<endl;
    //  cout<<"vseg: "<<vseg.X()<<" deltaX: "<<vseg.X()*lion<<endl;
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
    cout << " x, y = " << ip.X << ", " << ip.Y << " snorm = "
	 << ip.SNorm/fSNormNsigma << " charge " << ip.Charge << endl;
    cout << "fRTime0 = " << fRTime0 << endl;
    cout << "fRion size " << fRIon.size() << " " << i << endl;
#endif
    
    fRIon[i] = ip;
  }
  return;
}

//-------------------------------------------------------
// Helper functions for integration in AvaModel
#if 0
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
Bool_t IsInActiveArea( const TSBSGEMPlane& pl, Double_t xc, Double_t yc )
{
  
  pl.StripToSpec(xc,yc);
  return pl.GetBox().Contains(xc,yc);
}
#endif

//.......................................................
// avalanche model
//

TSBSGEMHit **
TSBSSimGEMDigitization::AvaModel(const Int_t ic,
				 const TSBSSpec& spect,
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

#if DBG_AVA > 0
  cout << "fRSMax, nsigma " << fRSMax << " " << nsigma << endl;
#endif

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

  const TSBSGEMChamber& chamber = spect.GetChamber(ic);
  Double_t glx = (chamber.GetPlane(0).GetStripLowerEdge(0)+chamber.GetPlane(0).GetSPitch()/2.0) * 1000.0;
  Double_t gly = (chamber.GetPlane(1).GetStripLowerEdge(0)+chamber.GetPlane(1).GetSPitch()/2.0) * 1000.0;
  Double_t gux = (chamber.GetPlane(0).GetStripUpperEdge(chamber.GetPlane(0).GetNStrips()-1)
		  -chamber.GetPlane(0).GetSPitch()/2.0) * 1000.0;
  Double_t guy = (chamber.GetPlane(1).GetStripUpperEdge(chamber.GetPlane(1).GetNStrips()-1)
		  -chamber.GetPlane(1).GetSPitch()/2.0) * 1000.0;
  
  if (x1<glx || x0>gux ||
      y1<gly || y0>guy) { // out of the sector's bounding box
    cerr << __FILE__ << " " << __FUNCTION__ << ": out of sector, "
	 << "chamber " << ic << " sector " << ic%30 << " plane " << ic/30 << endl
	 << "Following relations should hold:" << endl
	 << "(x1 " << x1 << ">glx " << glx << ") (x0 " << x0 << "<gux " << gux << ")" << endl
	 << "(y1 " << y1 << ">gly " << gly << ") (y0 " << y0 << "<guy " << guy << ")" << endl;
    return 0;
  }

  //bool bb_clipped = (x0<glx||y0<gly||x1>gux||y1>guy);
  if(x0<glx) x0=glx;
  if(y0<gly) y0=gly;
  if(x1>gux) x1=gux;
  if(y1>guy) y1=guy;

  // Loop over chamber planes
  
  TSBSGEMHit **virs;
  virs = new TSBSGEMHit *[fNPlanes[ic]];
  for (UInt_t ipl = 0; ipl < fNPlanes[ic]; ++ipl){
#if DBG_AVA > 0
    cout << "coordinate " << ipl << " =========================" << endl;
#endif
    
    // Compute strips affected by the avalanche

    const TSBSGEMPlane& pl = chamber.GetPlane(ipl);

    // Positions in strip frame
    Double_t xs0 = x0 * 1e-3; Double_t ys0 = y0 * 1e-3;
    pl.PlaneToStrip (xs0, ys0);
    xs0 *= 1e3; ys0 *= 1e3;
    Double_t xs1 = x1 * 1e-3; Double_t ys1 = y1 * 1e-3;
    pl.PlaneToStrip (xs1, ys1);
    xs1 *= 1e3; ys1 *= 1e3;
#if DBG_AVA > 0
    cout << "glx gly gux guy " << glx << " " << gly << " " << gux << " " << guy << endl;
    cout << "xs0 ys0 xs1 ys1 " << xs0 << " " << ys0 << " " << xs1 << " " << ys1 << endl;
#endif
    //cout<<"xxxx : "<<(xs0/2+xs1/2)*1e-3<<" y: "<<(ys0/2+ys1/2)*1e-3<<endl;
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
	   << endl << endl;
#endif
      if( ipl == 1 ) {
	delete virs[0];
      }
      delete [] virs;
      return 0;
    }
    //bool clipped = ( iL < 0 || iU < 0 );
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

    // We should also allow x to have variable bin size based on the db
    // the new avalanche model (Cauchy-Lorentz) has a very sharp full width
    // half maximum, so if the bin size is too large, it can introduce
    // fairly large error on the charge deposition. Setting fXIntegralStepsPerPitch
    // to 1 will go back to the original version -- Weizhi Xiong

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
    
    Int_t sumASize = nx * ny;
    //  cout<<nx<<" : "<<ny<<" Nstrips: "<<nstrips<<endl;
    Double_t fSumA[sumASize];
    for(Int_t i=0; i<sumASize; i++){
      fSumA[i] = 0;
    }
    
    //    fSumA.resize(nx*ny);
    //memset (&fSumA[0], 0, fSumA.size() * sizeof (Double_t));
  
 
    for (UInt_t i = 0; i < fRNIon; i++){
      Double_t frxs = fRIon[i].X * 1e-3;
      Double_t frys = fRIon[i].Y * 1e-3;


      pl.PlaneToStrip (frxs, frys);
      frxs *= 1e3; frys *= 1e3;
     
      //  cout<<"IonStrip: "<<pl.GetStrip(frxs*1e-3,frys*1e-3)<<endl;
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
      Double_t eff_sigma_square = r2/(fSNormNsigma*fSNormNsigma);
      Double_t eff_sigma = TMath::Sqrt(eff_sigma_square);
      Double_t current_ion_amplitude = fAvaGain*ggnorm*(1./(TMath::Pi()*eff_sigma))*(eff_sigma*eff_sigma);


      // xc and yc are center of current bin
      
      // Loop over bins
      Int_t min_binNb_x = max(ix-dx,0);
      Int_t min_binNb_y = max(iy-dy,0);
      Int_t max_binNb_x = min(ix+dx+1,nx);
      Int_t max_binNb_y = min(iy+dy+1,ny);
      Int_t jx = min_binNb_x;
      Double_t xc = xl + (jx+0.5) * xbw;
      for (; jx < max_binNb_x; ++jx, xc += xbw){
	Double_t xd2 = frxs-xc; xd2 *= xd2;
	//if( xd2 > r2 ){
	//  if( (xc - frxs)>0 )
	//    break;
	//  else
	//    continue;
	//}
	Int_t jx_base = jx * ny;
	Int_t jy = min_binNb_y;
	Double_t yc = yb + (jy+0.5) * ybw;
	
	for (; jy < max_binNb_y; ++jy, yc += ybw){
	  Double_t yd2 = frys-yc; yd2 *= yd2;
	  //if( yd2 > r2 ){
	  //  if( (yc - frys)>0 )
	  //    break;
	  //  else
	  //    continue;
	  // }
	  if( xd2 + yd2 <= r2 ) {
	    //if( (clipped || bb_clipped) && !IsInActiveArea(pl,xc*1e-3,yc*1e-3) )
	    //continue;
	    
	    /*	    switch (fAvaModel){
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
*/
	      // Cauchy-Larentz: 
	    fSumA[jx_base+jy] += current_ion_amplitude / ((xd2+yd2)+eff_sigma_square);
	      
	    //fAvaGain*ggnorm*(1./(TMath::Pi()*eff_sigma))*(eff_sigma*eff_sigma)

	    //    cout<<"eff_sigma: "<<eff_sigma<<"  "<<fAvaGain*ggnorm*(1./(TMath::Pi()*eff_sigma))*(eff_sigma*eff_sigma)/((xd2+yd2)+eff_sigma*eff_sigma)<<endl;
	      //  cout<<setw(4)<<(Int_t)(ggnorm*(1./(TMath::Pi()*eff_sigma))*(eff_sigma*eff_sigma)/((xd2+yd2)+eff_sigma*eff_sigma));
	      //}
	  }
	}//cout<<endl;
      }//cout<<"##########################################################################"<<endl<<endl;getchar();
    }
   
#if DBG_AVA > 0
    cout << "t0 = " << t0 << " plane " << ipl 
	 << endl;
#endif

    virs[ipl] = new TSBSGEMHit(nx,fEleSamplingPoints);
    virs[ipl]->SetTime(t0);
    virs[ipl]->SetHitCharge(fRTotalCharge);
    
    Int_t ai=0;
    Double_t area = xbw * ybw;

    //when we integrate in order to get the signal pulse, we want all charge
    //deposition on the area of a single strip -- Weizhi
    
    //cout << "number of strips: " << nstrips << ", number of samples " << fEleSamplingPoints << " area: " << area << endl;
    
    // if(nstrips>0){cout<<"nstrips: "<<nstrips<<" Nion: "<<fRNIon<<endl;}
    double total_us = 0, total_pos = 0;
    for (Int_t j = 0; j < nstrips; j++){
      //  cout<<"strip: "<<iL+j<<":    ";
      Int_t posflag = 0;
      Double_t us = 0.;
      for (UInt_t k=0; k<fXIntegralStepsPerPitch; k++){
	Double_t integralY_tmp = 0;
	int kx = (j * fXIntegralStepsPerPitch + k) * ny;
	for( Int_t jy = ny; jy != 0; --jy )
	  integralY_tmp += fSumA[kx++];
	
	us += integralY_tmp * area;
	//	us += IntegralY( fSumA, j * fXIntegralStepsPerPitch + k, nx, ny ) * area;
	//if(us>0)cout << "k " << k << ", us " << us << endl;
      }

      total_us += us;
      total_pos += (iL+j)*0.0004*us;
      // cout <<setw(6)<< (Int_t)(us/100);
      //  cout<<iL+j<<" : "<<us<<endl;
      //generate the random pedestal phase and amplitude
      // Double_t phase = fTrnd.Uniform(0., fPulseNoisePeriod);
      // Double_t amp = fPulseNoiseAmpConst + fTrnd.Gaus(0., fPulseNoiseAmpSigma);
      
      for (Int_t b = 0; b < fEleSamplingPoints; b++){
	Double_t pulse =
	  TSBSSimAuxi::PulseShape (fEleSamplingPeriod * b - t0,
				   us,
				   fPulseShapeTau0,
				   fPulseShapeTau1 );

	Short_t dadc = TSBSSimAuxi::ADCConvert( pulse,
						fADCoffset,
						fADCgain,
						fADCbits );
#if DBG_AVA > 0
	cout << "strip number " << j << ", sampling number " << b << ", t0 = " << t0 << endl
	     << "pulse = " << pulse << ", (val - off)/gain = " 
	     << (pulse-fADCoffset)/fADCgain << ", dadc = " << dadc << endl;
#endif
	fDADC[b] = dadc;
	posflag += dadc;
	//cout <<setw(6)<< dadc;
      }//cout <<"  "<<t0<< endl;

      

      if (posflag > 0) { // store only strip with signal
	for (Int_t b = 0; b < fEleSamplingPoints; b++){
	  virs[ipl]->AddSampleAt (fDADC[b], b, ai);
	}
	virs[ipl]->AddStripAt (iL+j, ai);
	virs[ipl]->AddChargeAt (us, ai);
	ai++;
      }

      //  cout<<endl;
    }//getchar();
    
    
    //    cout<<"avg pos(in m): "<<pl.GetSBeg() + total_pos/total_us<<endl;
    
    //cout<<"nstrips: "<<pl.GetNStrips()<<" begin: "<<pl.GetSBeg()<<"  diff in(um)"<<(pl.GetSBeg() + total_pos/total_us - (xs0/2+xs1/2)*1e-3)*1e6<<endl;
    //    getchar();
    //cout << "number of strips with signal " << ai << endl;
    
    virs[ipl]->SetSize(ai);
  }
  
  return virs;
}
//___________________________________________________________________________________
inline Double_t TSBSSimGEMDigitization::GetPedNoise(Double_t &phase, Double_t& amp, Int_t& isample)
{
  Double_t thisPhase = phase + isample*fEleSamplingPeriod;
  return fTrnd.Gaus(5*fPulseNoiseSigma, fPulseNoiseSigma) //fTrnd.Gaus(0., fPulseNoiseSigma)
    + amp*sin(2.*TMath::Pi()/fPulseNoisePeriod*thisPhase);
}
//___________________________________________________________________________________
void
TSBSSimGEMDigitization::Print(Option_t*) const
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
  cout << "    Trigger offsets: "; //<< fTriggerOffset 
  for(int i = 0; i<manager->GetNChamber(); i++)cout << fTriggerOffset[i] << " ";
  cout << endl;
  cout << "    Trigger jitter: " << fTriggerJitter << endl;
  cout << "    Sampling Period: " << fEleSamplingPeriod << endl;
  cout << "    Sampling Points: " << fEleSamplingPoints   << endl;
  cout << "    Pulse Noise width: " << fPulseNoiseSigma << endl;
  cout << "    ADC offset: " << fADCoffset << endl;
  cout << "    ADC gain: " << fADCgain << endl;
  cout << "    ADC bits: " << fADCbits << endl;
  cout << "    Gate width: " << fGateWidth << endl;

  cout << "  Pulse shaping parameters:" << endl;
  cout << "    Pulse shape tau0: " << fPulseShapeTau0 << endl;
  cout << "    Pulse shape tau1: " << fPulseShapeTau1 << endl;
}

void
TSBSSimGEMDigitization::PrintCharges() const
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
TSBSSimGEMDigitization::PrintSamples() const
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
TSBSSimGEMDigitization::InitTree (const TSBSSpec& spect, const TString& ofile)
{
  fOFile = new TFile( ofile, "RECREATE");

  if (fOFile == 0 || fOFile->IsZombie() )
    {
      cerr << "Error: cannot open output file " << ofile << endl;
      delete fOFile; fOFile = 0;
      return;
    }

  fOTree = new TTree( treeName, "Tree of digitized values");
  fOTree -> SetMaxTreeSize(100000000000);
  //fOTree -> SetMaxTreeSize(1000);


  // create the tree variables

  fOTree->Branch( eventBranchName, "TSBSSimEvent", &fEvent );

}

void
TSBSSimGEMDigitization::SetTreeEvent (const TSBSGEMSimHitData& tsgd,
				      const TSBSGeant4File& f, Int_t evnum )
{
  // Set overall event info.
  fEvent->Clear("all");
  fEvent->fRunID = tsgd.GetRun();
  // FIXME: still makes sense if background added?
  fEvent->fEvtID = (evnum < 0) ? tsgd.GetEvent() : evnum;
  for( UInt_t i=0; i<f.GetNGen(); ++i ) {
    const g4sbsgendata* gd = f.GetGenData(i);
    //cout << "TSBSSimGEMDigitization::SetTreeEvent: PID = " << gd->GetPID() << endl;
    fEvent->AddTrack( gd->GetTRID(), gd->GetPID(),
		      gd->GetV(), // Vertex coordinates in [m]
		      gd->GetP(),  // Momentum in [GeV]
		      gd->GetVertexAtTarget(),
		      gd->GetMomentumAtTarget());
  }
  
  
  for( UInt_t i=0; i<f.GetClusterSize(); i++ ) {
    TSBSECalCluster* clus = f.GetCluster(i);
    
    double Time = 0.0;
    if(tsgd.GetSource()==0){
      // if signal, by definition the cluster timing determines the trigger time
      Time = fTrnd.Gaus(0,fTriggerJitter);
    }else{
      //fTrnd.
      Time = fTrnd.Uniform(-50.0, 50.0)+fTrnd.Gaus(0,fTriggerJitter);
      //We assume a Gate width of +- 50 ns for the time being
    }
    clus->SetTime(Time);
    fEvent->fECalClusters.push_back(*clus);
    delete clus;
  }
  
  for( UInt_t i=0; i<f.GetSciClusterSize(); i++ ) {
    TSBSScintCluster* clus = f.GetSciCluster(i);
    
    double Time = 0.0;
    if(tsgd.GetSource()==0){
      // if signal, by definition the cluster timing determines the trigger time
      Time = fTrnd.Gaus(0,fTriggerJitter);
    }else{
      //fTrnd.
      Time = fTrnd.Uniform(-50.0, 50.0)+fTrnd.Gaus(0,fTriggerJitter);
      //We assume a Gate width of +- 50 ns for the time being
    }
    clus->SetTime(Time);
    fEvent->fScintClusters.push_back(*clus);
    delete clus;
  }
   //cout<<"nloop: "<<f.GetNGen()<<"   ntracks: "<<fEvent->GetNtracks()<<endl;
  //getchar();
  // FIXED: one GenData per event: signal, primary particle
  if( f.GetNGen() > 0 )
    fEvent->fWeight = f.GetGenData(0)->GetWeight();
  
  fEvent->fSectorsMapped = fDoMapSector;
  //fEvent->fSignalSector = tsgd.GetSigSector();//CHECK ?
  fEvent->fSignalSector = fSignalSector;
}

Short_t
TSBSSimGEMDigitization::SetTreeHit (const UInt_t ih,
				    const TSBSSpec& spect,
				    //TSBSGEMHit* const *dh,
				    const TSBSGEMSimHitData& tsgd,
				    Double_t t0 )
{
  // Sets the variables in fEvent->fGEMClust describing a hit
  // This is later used to fill the tree.
  
  TSBSSimEvent::GEMCluster clust;
  
  UInt_t igem = tsgd.GetHitChamber(ih);
  clust.fPlane = manager->GetPlaneID(igem);
  clust.fModule = manager->GetModuleID(igem);
  clust.fSector   = 0;// disabling sector info for now, working to remove fSector//clust.fRealSector; // May change if mapped, see below
  clust.fSource   = tsgd.GetSource();  // Source of this hit (0=signal, >0 background)
  clust.fType     = tsgd.GetParticleType(ih);   // =1->primary,     >1 ->secondary
  clust.fTRID     = tsgd.GetTrackID(ih);   // GEANT particle counter
  clust.fPID      = tsgd.GetParticleID(ih); // PDG PID
  clust.fP        = tsgd.GetMomentum(ih); // [MeV] // Momentum vector in spec frame, transformed at (1); 
  clust.fPspec    = tsgd.GetMomentum(ih)    * 1e-3; // [GeV]
  clust.fXEntry   = tsgd.GetHitEntrance(ih) * 1e-3; // [m] // in plane frame
  // The best estimate of the "true" hit position is the center of the
  // ionization region

  // UInt_t hitbit_dum = 0;
  // SETBIT(hitbit_dum, clust.fPlane);
  // cout << "Plane number " << clust.fPlane; 
  // cout << ", hitbit: " << std::hex << hitbit_dum;
  // cout << std::dec << endl;
  
  //clust.fMCpos    = tsgd.GetHitEntrance(ih); // [mm] for transformation tests only
  clust.fMCpos    = (tsgd.GetHitEntrance(ih)+tsgd.GetHitExit(ih)) * 5e-1; // [mm] 
  // Position of hit in spec (transport) frame, transformed at (2)
  clust.fHitpos   = (tsgd.GetHitEntrance(ih)+tsgd.GetHitExit(ih)) * 5e-1; // [mm] 
  // Position of the hit in tracker frame: no need to transform
  
  // if (fdh != NULL && fdh[0] != NULL)
  //  clust.fCharge = fdh[0]->GetHitCharge();
  //else
  //  clust.fCharge = 0;
  clust.fCharge = tsgd.GetHitEnergy(ih);
  clust.fTime   = t0;  // [ns]

  const TSBSGEMChamber& ch = spect.GetChamber(igem);
  
  ch.SpecToLab(clust.fP);// (1)
  ch.PlaneToSpec(clust.fMCpos); // (2)
  //ch.PlaneToHallCenter(clust.fMCpos); // (2')
  //ch.PlaneToSpec(clust.fHitpos); // (3')
  clust.fP = (clust.fP)*1.0e-3;
  clust.fMCpos = (clust.fMCpos)*1.0e-3;
  clust.fHitpos = (clust.fHitpos)*1.0e-3;

  // cout << " TSBSSimGEMDigitization.cxx::SetTreeHit  hit in plane " << clust.fPlane << " (clust.fHitPos):  " << endl;
  // clust.fHitpos.Print();
  // cout << " => (clust.fMCPos): " << endl;
  // clust.fMCpos.Print();
  // //hitpos_temp.Print();
  // cout << "igem " << igem << ", plane " << clust.fPlane << ", module " << clust.fModule << endl;
  //clust.fHitpos = hitpos_temp;
  
  
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
    const TSBSGEMPlane& pl = ch.GetPlane(j);
    Double_t proj_angle = pl.GetSAngle();
    TVector3 hitpos(clust.fHitpos);
    hitpos.RotateZ(-proj_angle);
    clust.fXProj[j] = hitpos.X();
  }

  clust.fID     = fEvent->fGEMClust.size()+1;
  clust.fVertex = tsgd.GetVertex (ih) * 1e-3;//[m]
  

  
  /*
  cout << endl << "Cluster ID " << clust.fID << ", sector (realsector) " 
       << clust.fSector << " " << clust.fRealSector
       << ", source " << clust.fSource << ", plane " << clust.fPlane << endl;
  cout << "particle type (primary==0) " << clust.fType << ", G4 PID " << clust.fPID << endl;
  cout << "momentum (GeV) in lab frame: " << clust.fP.X() << " " << clust.fP.Y() << " "  << clust.fP.Z() << " norm ("  << tsgd.GetMomentum(ih).Mag()*1e-3 << ")" << endl;
  //cout << "momentum (GeV) in spec frame: " << clust.fPspec.X() << " " << clust.fPspec.Y() << " "  << clust.fPspec.Z() << endl;
  cout << "vertex (m)" << clust.fVertex.X() << " " << clust.fVertex.Y() << " "  << clust.fVertex.Z() << endl;
  cout << "hit charge (?): " << clust.fCharge << ", time (ns): " << clust.fTime << endl;
  cout << "corresponding hit energy (eV): " << tsgd.GetHitEnergy(ih) << endl;
  cout << "position: at entrance (m): " << clust.fXEntry.X() << " " << clust.fXEntry.Y() << " "  << clust.fXEntry.Z() << endl;
  cout << "position: in lab (m): " << clust.fMCpos.X() << " " << clust.fMCpos.Y() << " "  << clust.fMCpos.Z() << endl;
  cout << "position: in tracker frame (m): " << clust.fHitpos.X() << " " << clust.fHitpos.Y() << " "  << clust.fHitpos.Z() << endl;
  cout << "Strips sizes (1, 2): " << clust.fSize[0] << " " << clust.fSize[1] 
       << ", starts (1, 2): " << clust.fStart[0] << " " << clust.fStart[1]
       << ", Xproj (1, 2): " << clust.fXProj[0] << " " << clust.fXProj[1] << endl << endl;
  */
    
  fEvent->fGEMClust.push_back( clust );
  
  //cout << "cluster plane " << clust.fPlane << ", cluster type " << clust.fType << ", cluster source " << clust.fSource << endl;
  
  if( clust.fType == 1 && clust.fSource == 0 )
    fEvent->fNSignal++;
  
  //cout << "Event cluster size " <<  fEvent->fGEMClust.size() << ", Event signal size " << fEvent->fNSignal << endl;
  
  return clust.fID;
}

void
TSBSSimGEMDigitization::SetTreeStrips()
{
  // Sets the variables in fEvent->fGEMStrips describing strip signals
  // This is later used to fill the tree.
  
  fEvent->fGEMStrips.clear();

  TSBSSimEvent::DigiGEMStrip strip;
  Double_t saturation = static_cast<Double_t>( (1<<fADCbits)-1 )-1300;
  for (UInt_t ich = 0; ich < GetNChambers(); ++ich) {
    
    strip.fSector=0;
    strip.fPlane=manager->GetPlaneID(ich);
    strip.fModule=manager->GetModuleID(ich);
    // cout<<ich<<" : "<<(3*strip.fPlane+strip.fModule)<<endl;
    
    //cout << "ich " << ich << " strip sector " <<  strip.fSector << " strip plane " << strip.fPlane << endl;
    
    // The "plane" here is actually the projection (= readout coordinate).
    // TSBSGEMChamber::ReadDatabase associates the name suffix "x" with
    // the first "plane", and "y", with the second. However, strip angles can
    // be different for each chamber, so what's "x" in one chamber may very
    // well be something else, like "x'", in another. These angles don't even
    // have to match anything in the Monte Carlo.
    for (UInt_t ip = 0; ip < GetNPlanes (ich); ++ip) {
      strip.fProj = (Short_t) ip;
      strip.fNsamp = TMath::Min((UShort_t)MC_MAXSAMP,
				(UShort_t)GetNSamples(ich, ip));
      
      if(1)
	{
	  UInt_t nstrips = GetNStrips(ich,ip);
	  for (UInt_t istrip = 0; istrip < nstrips; istrip++) {
	    Short_t idx = istrip;
	    strip.fChan = idx;

	    //setting strip sample adc and adding pedestal noise
	    for (UInt_t ss = 0; ss < strip.fNsamp; ++ss){
	      strip.fADC[ss] = GetADC(ich, ip, idx, ss);
	      // cout << strip.fADC[ss] << " ";
	       strip.fADC[ss] += fTrnd.Gaus(0, fPulseNoiseSigma);//allowing negative value, before implementing common mode;
	      // cout << strip.fADC[ss] << " ";
	      saturation = 4000;
	      if(strip.fADC[ss]>saturation)strip.fADC[ss]=saturation;
	      const vector<Int_t>& sclust = GetStripClusterADC(ich, ip, idx, ss);
	      
	      strip.fClusterRatio[ss].Set( sclust.size(), &sclust[0] );
	      //     if(sclust.size()!=0){
	      //	 cout<<idx<<" "<<ss<<" "<<sclust.size()<<" == "<<strip.fClusterRatio[ss].GetSize()<<" == "<<GetStripClusters(ich, ip, idx).size()<<endl; getchar();
	      //    }
	    }//cout << endl;
	    /*	    if(GetTotADC(ich, ip, idx)==0)
	      {
		cout<<"Type: "<<GetType(ich, ip, idx)<<" Charge: "<<GetCharge(ich, ip, idx)<<" Time: "
		    <<GetTime(ich, ip, idx)<<" cluster: "<<GetStripClusters(ich, ip, idx).size();
		getchar();
		continue;
	      }
	    */
	    strip.fSigType = GetType(ich, ip, idx);
	    strip.fCharge  = GetCharge(ich, ip, idx);
	    strip.fTime1   = GetTime(ich, ip, idx);
	
	    const vector<Short_t>& sc = GetStripClusters(ich, ip, idx);
	    strip.fClusters.Set( sc.size(), &sc[0] );
	    const vector<Double_t>& swc = GetStripWeightInCluster(ich, ip, idx);
	    strip.fStripWeightInCluster.Set( swc.size(), &swc[0] );

	    fEvent->fGEMStrips.push_back( strip );
	  }
	}
      else{
	//modify this so recording all strips? 
	UInt_t nover = GetNOverThr(ich, ip);
	for (UInt_t iover = 0; iover < nover; iover++) {
	  Short_t idx = GetIdxOverThr(ich, ip, iover);
	  strip.fChan = idx;

	  //setting strip sample adc and adding pedestal noise
	  for (UInt_t ss = 0; ss < strip.fNsamp; ++ss){
	    strip.fADC[ss] = GetADC(ich, ip, idx, ss);
	    // cout << strip.fADC[ss] << " ";
	    strip.fADC[ss] += fTrnd.Gaus(0, fPulseNoiseSigma);//allowing negative value, before implementing common mode;
	    // cout << strip.fADC[ss] << " ";
	    if(strip.fADC[ss]>saturation)strip.fADC[ss]=saturation;

	    const vector<Int_t>& sclust = GetStripClusterADC(ich, ip, idx, ss);
	    strip.fClusterRatio[ss].Set( sclust.size(), &sclust[0] );
	    

	  }//cout << endl;

	  strip.fSigType = GetType(ich, ip, idx);
	  strip.fCharge  = GetCharge(ich, ip, idx);
	  strip.fTime1   = GetTime(ich, ip, idx);
	
	  const vector<Short_t>& sc = GetStripClusters(ich, ip, idx);
	  strip.fClusters.Set( sc.size(), &sc[0] );
	  
	  fEvent->fGEMStrips.push_back( strip );
	}
      }
    }
  }
  fFilledStrips = true;
}

void
TSBSSimGEMDigitization::FillTree ()
{
  if( !fFilledStrips )
    SetTreeStrips();

  //cout << "Fill tree " << fOFile << " " << fOTree << endl;
  //fOFile = fOTree->GetCurrentFile();//CHECK ?
  if (fOFile && fOTree
      // added this line to not write events where there are no entries

      // Remove for background study
      //      && fEvent->fGEMStrips.size() > 0 && fEvent->fGEMClust.size() > 0

      )
    {
      fOFile->cd();
      //fEvent->Print("all"); 
      fOTree->Fill();
    }
}

void
TSBSSimGEMDigitization::WriteTree () const
{
  //cout << "write tree " << fOFile << " " << fOTree << endl;
  
  if (fOFile && fOTree) {
    fOFile->cd();
    fOTree->Write();
  }
}

void
TSBSSimGEMDigitization::CloseTree () const
{
  if (fOFile) fOFile->Close();
}

