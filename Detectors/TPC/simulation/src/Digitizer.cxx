/// \file Digitizer.cxx
/// \brief Digitizer for the TPC
#include "TPCSimulation/Digitizer.h"
#include "TPCSimulation/Point.h"
#include "TPCSimulation/PadResponse.h"

#include "TPCBase/Mapper.h"

#include "TRandom.h"
#include "TF1.h"
#include "TClonesArray.h"
#include "TCollection.h"
#include "TMath.h"

#include "FairLogger.h"

#include <cmath>
#include <iostream>

ClassImp(AliceO2::TPC::Digitizer)

using namespace AliceO2::TPC;

Digitizer::Digitizer():
TObject(),
mPolya(nullptr),
mDigitContainer(nullptr),
mHitContainer(nullptr)
{}

Digitizer::~Digitizer(){
  delete mDigitContainer;
  delete mHitContainer;
  delete mPolya;
}

void Digitizer::init(){
  mDigitContainer = new DigitContainer();
  //Double_t SigmaOverMu = 0.78;
  //Double_t kappa = 1/(SigmaOverMu*SigmaOverMu);
  //Double_t s = 1/kappa;
  
  //char strPolya[1000];
  //snprintf(strPolya,1000,"1/(TMath::Gamma(%e)*%e) *pow(x/%e, (%e)) *exp(-x/%e)", kappa, s, s, kappa-1, s);
  //mPolya = new TF1("polya", "1/x", 0, 1000);
}

DigitContainer *Digitizer::Process(TClonesArray *points){
  // TODO should be parametrized
  Float_t wIon = 37.3e-6;
  Float_t attCoef = 250.;
  Float_t OxyCont = 5.e-6;
  Float_t driftV = 2.58;
  Float_t zBinWidth = 0.19379844961;
  
  mHitContainer = new HitContainer();
  mDigitContainer->reset();

  Float_t posEle[4] = {0,0,0,0};

  const Mapper& mapper = Mapper::instance();
  
  for (TIter pointiter = TIter(points).Begin(); pointiter != TIter::End(); ++pointiter){
    Point *inputpoint = dynamic_cast<Point *>(*pointiter);
    
    posEle[0] = inputpoint->GetX();
    posEle[1] = inputpoint->GetY();
    posEle[2] = inputpoint->GetZ();
    posEle[3] = static_cast<int>(inputpoint->GetEnergyLoss()/wIon);
    Int_t count = 0;
    
    //Loop over electrons
    for(Int_t iEle=0; iEle < posEle[3]; ++iEle){
      
      // Attachment
      Float_t attProb = attCoef * OxyCont * getTime(posEle[2]);
      if((gRandom->Rndm(0)) < attProb) continue;

      // Drift and Diffusion
      ElectronDrift(posEle);

      // remove electrons that end up outside the active volume
      if(TMath::Abs(posEle[2]) > 250) continue;

      const GlobalPosition3D posElePad (posEle[0], posEle[1], posEle[2]);
      const DigitPos digiPadPos = mapper.findDigitPosFromGlobalPosition(posElePad);

      if(!digiPadPos.isValid()) continue;
      
      // GEM amplification
      // Gain values taken from TDR addendum - to be put someplace else
      Int_t nEleGEM1 = SingleGEMAmplification(1, 9.1);
      Int_t nEleGEM2 = SingleGEMAmplification(nEleGEM1, 0.88);
      Int_t nEleGEM3 = SingleGEMAmplification(nEleGEM2, 1.66);
      Int_t nEleGEM4 = SingleGEMAmplification(nEleGEM3, 144);
      
      // Loop over all individual pads with signal due to pad response function
      vector< PadResponse*> padResponse = getPadResponse(posEle[0], posEle[1]);
      for(auto iterPRF = padResponse.begin(); iterPRF != padResponse.end(); ++iterPRF){
        Int_t pad = digiPadPos.getPadPos().getPad() + (*iterPRF)->getPad();
        Int_t row = digiPadPos.getPadPos().getRow() + (*iterPRF)->getRow();
        Double_t weight = (*iterPRF)->getWeight();
        
        mHitContainer->addHit(digiPadPos.getCRU().number(), row, pad, getTimeBin(posEle[2]), nEleGEM4*weight);
      }
      // end of loop over individual pads
    }
    //end of loop over electrons
  }
  // end of loop over points
  
  mHitContainer->getHits(mPadHit);
  delete mHitContainer;
  
  for(vector<PadHit*>::iterator iter = mPadHit.begin(); iter != mPadHit.end(); ++iter){
    vector<PadHitTime*> mTimeHits = (*iter)->getTimeHit(); //retrieve time bin hit on that pad
    
    for(vector<PadHitTime*>::iterator iterTime = mTimeHits.begin(); iterTime != mTimeHits.end(); ++iterTime){
      
      // loop over individual time bins and apply time response function
      Float_t startTime = getTimeFromBin((*iterTime)->getTime())+0.5*zBinWidth;
      for(Float_t bin = 0; bin<5; ++bin){
        Double_t signal = 55*Gamma4(startTime+bin*zBinWidth, startTime, (*iterTime)->getCharge());
        mDigitContainer->addDigit((*iter)->getCRU(), (*iter)->getRow(), (*iter)->getPad(), getTimeBinFromTime(startTime+(bin-0.5)*zBinWidth), ADCvalue(signal));
      }
    }
  }

  return mDigitContainer;
}


void Digitizer::ElectronDrift(Float_t *posEle){
  // TODO parameters to be stored someplace else
  Float_t DiffT = 0.0209;
  Float_t DiffL = 0.0221;
  
  Float_t driftl=posEle[2];
  if(driftl<0.01) driftl=0.01;
  driftl=TMath::Sqrt(driftl);
  Float_t sigT = driftl*DiffT;
  Float_t sigL = driftl*DiffL;
  posEle[0]=gRandom->Gaus(posEle[0],sigT);
  posEle[1]=gRandom->Gaus(posEle[1],sigT);
  posEle[2]=gRandom->Gaus(posEle[2],sigL);
}


Float_t Digitizer::GEMAmplification(){
  // TODO parameters to be stored someplace else
  Float_t gain = 2000;
  
  Float_t rn=TMath::Max(gRandom->Rndm(0),1.93e-22);
  Float_t signal = static_cast<int>(-(gain) * TMath::Log(rn));
  return signal;
}


Int_t Digitizer::SingleGEMAmplification(Int_t nEle, Float_t gain){
//   return static_cast<int>(static_cast<float>(nEle)*gain*mPolya->GetRandom());
  return static_cast<int>(static_cast<float>(nEle)*gain);
}


vector< PadResponse*> Digitizer::getPadResponse(Float_t xabs, Float_t yabs){
  vector < PadResponse* >  mPadResponse;

  // purely projective for the time being!
  PadResponse *padHit = new PadResponse(0, 0, 1);
  mPadResponse.push_back(padHit);
  return mPadResponse;
}



Int_t Digitizer::ADCvalue(Float_t nElectrons){
  // TODO parameters to be stored someplace else
  Float_t ADCSat = 1024;
  Float_t Qel = 1.602e-19;
  Float_t ChipGain = 20;
  Float_t ADCDynRange = 2000;
  
  Int_t adcValue = static_cast<int>(nElectrons*Qel*1.e15*ChipGain*ADCSat/ADCDynRange);
  // saturation is applied at a later stage
  return adcValue;
}


const Int_t Digitizer::getTimeBin(Float_t zPos){
  // TODO parameters to be stored someplace else
  Float_t driftV = 2.58;
  Double_t zBinWidth = 0.19379844961;
  
  Float_t timeBin = (250.-TMath::Abs(zPos))/(driftV*zBinWidth);
  return static_cast<int>(timeBin);
}


const Int_t Digitizer::getTimeBinFromTime(Float_t time){
  // TODO parameters to be stored someplace else
  Double_t zBinWidth = 0.19379844961;
  
  Float_t timeBin = time/zBinWidth;
  return static_cast<int>(timeBin);
}


const Float_t Digitizer::getTimeFromBin(Int_t timeBin){
  // TODO parameters to be stored someplace else
  Double_t zBinWidth = 0.19379844961;
  
  Float_t time = static_cast<float>(timeBin)*zBinWidth;
  return time;
}


const Double_t Digitizer::getTime(Float_t zPos){
  // TODO parameters to be stored someplace else
  Float_t driftV = 2.58;
  
  Double_t time = (250.-TMath::Abs(zPos))/driftV;
  return time;
}


Double_t Digitizer::Gamma4(Double_t time, Double_t startTime, Double_t ADC){
  // TODO parameters to be stored someplace else
  Double_t peakingTime = 160e-3; // all times are in us
  
  if (time<0) return 0;
  return ADC*TMath::Exp(-4.*(time-startTime)/peakingTime)*TMath::Power((time-startTime)/peakingTime,4);
}