#include "TSBSSpec.h"
#include "TSBSGEMChamber.h"
#include <iostream>

using namespace std;

TSBSSpec::TSBSSpec(const char* name, const char* desc )
    :THaSpectrometer(name,desc) {

  // We don't need run db (not yet at least)
  fProperties &= ~kNeedsRunDB;
  return;
}

TSBSSpec::~TSBSSpec()
{
  // Destructor: delete all plane objects

  for( vector<TSBSGEMChamber*>::iterator it = fChambers.begin();
       it != fChambers.end(); ++it ) {
    delete *it;
  }
}

Int_t 
TSBSSpec::AddGEM (TSBSGEMChamber* pdet)
{
  // Add a detector to the internal lists of spectrometer detectors.
  // The detector object must be allocated and deleted by the caller.
  // Duplicate detector names are not allowed.

  fChambers.push_back(pdet);
  return 0;
}

Int_t TSBSSpec::CoarseTrack(){
    // Needs work
    return 0;
}

Int_t TSBSSpec::CoarseReconstruct(){
    return 0;
}

Int_t TSBSSpec::Track(){
    return 0;
}

Int_t TSBSSpec::Reconstruct(){
    return 0;
}

Int_t TSBSSpec::FindVertices(TClonesArray &){
    return 0;
}

void
TSBSSpec::Print(Option_t*) const
{
  cout << "Hello, I'm a spectrometer named " << GetName() << endl;
	
  for( vector<TSBSGEMChamber*>::const_iterator it = fChambers.begin();
       it != fChambers.end(); ++it ) {
    (*it)->Print();
  }
}
