 //
// An EDAnalyzer module that reads back the hits created by the calorimeter and produces an ntuple
//
// $Id: CaloDigiInspect_module.cc,v 1.4 2014/08/01 20:57:44 echenard Exp $
// $Author: echenard $
// $Date: 2014/08/01 20:57:44 $
//
// Original author Bertrand Echenard
//

#include "CLHEP/Units/SystemOfUnits.h"
#include "CalorimeterGeom/inc/Calorimeter.hh"
#include "CalorimeterGeom/inc/DiskCalorimeter.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "GeometryService/inc/GeometryService.hh"

#include "MCDataProducts/inc/SimParticleCollection.hh"
#include "MCDataProducts/inc/CaloShowerCollection.hh"
#include "RecoDataProducts/inc/CaloCrystalHitCollection.hh"
#include "RecoDataProducts/inc/CaloRecoDigiCollection.hh"
#include "MCDataProducts/inc/CaloHitMCTruthAssn.hh"
//#include "RecoDataProducts/inc/CaloRecoDigi.hh"
//#include "MCDataProducts/inc/CaloShower.hh"

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Selector.h"
#include "art/Framework/Principal/Provenance.h"
#include "cetlib/exception.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Utilities/InputTag.h"

#include "TDirectory.h"
#include "TH2F.h"
#include "TH1F.h"

#include <cmath>
#include <iostream>
#include <string>
#include <map>
#include <vector>



namespace mu2e {


   class CaloDigiInspect : public art::EDAnalyzer {

      public:

         explicit CaloDigiInspect(fhicl::ParameterSet const& pset);
         virtual ~CaloDigiInspect() { }

         virtual void beginJob();
         virtual void analyze(const art::Event& e);



      private:
         
	 typedef  art::Ptr<CaloShower> CaloShowerPtr;
	  
         std::string _caloShowerModuleLabel;
         std::string _caloDigisModuleLabel;
         std::string _caloCrystalHitModuleLabel;
         std::string _caloHitTruthModuleLabel;
         int _diagLevel;


         TH1F *_hShowerE,*_hShowerT,*_hShowerNoE,*_hShowerNoT,*_hHitE,*_hHitT,*_hHitNoE,*_hHitNoT;
         TH1F *_hDeltaT,*_hEpileUp,*_hresolE,*_hresolT,*_hresolT2;
         
	 TH2F *_hDeltaTE;

   };


   CaloDigiInspect::CaloDigiInspect(fhicl::ParameterSet const& pset) :
      art::EDAnalyzer(pset),
      _caloShowerModuleLabel(pset.get<std::string>("caloShowerModuleLabel")),
      _caloDigisModuleLabel(pset.get<std::string>("caloDigiModuleLabel")),
      _caloCrystalHitModuleLabel(pset.get<std::string>("caloCrystalHitModuleLabel")),
      _caloHitTruthModuleLabel(pset.get<std::string>("caloHitTruthModuleLabel")),
      _diagLevel(pset.get<int>("diagLevel",0))
   {
   }




   void CaloDigiInspect::beginJob(){

	art::ServiceHandle<art::TFileService> tfs;
	
	_hShowerE     = tfs->make<TH1F>("hShowerE",    "Energy shower matched",     100,    0., 50.   );
	_hShowerT     = tfs->make<TH1F>("hShowerT",    "Time shower matched",       100,    0., 2000. );
	_hShowerNoE   = tfs->make<TH1F>("hShowerNoE",  "Energy shower NOT matched", 100,    0., 10.   );
	_hShowerNoT   = tfs->make<TH1F>("hShowerNoT",  "Time shower NOT matched",   100,    0., 2000. );
	_hHitE        = tfs->make<TH1F>("hHitE",       "Energy Hit matched",        100,    0., 50.   );
	_hHitT        = tfs->make<TH1F>("hHitT",       "Time Hit matched",          100,    0., 2000. );
	_hHitNoE      = tfs->make<TH1F>("hHitNoE",     "Energy Hit NOT matched",    100,    0., 50.   );
	_hHitNoT      = tfs->make<TH1F>("hHitNoT",     "Time Hit NOT matched",      100,    0., 2000. );

	_hEpileUp     = tfs->make<TH1F>("ePileUp",     "Pile Up energy",            100,     0.,  50. );
	_hDeltaT      = tfs->make<TH1F>("deltaT",      "DeltaTime hit-showerd",     100,   -50., 200. );
	_hDeltaTE     = tfs->make<TH2F>("deltaTE",     "DeltaTime vs energy hit-showerd", 100,   -50., 200.,100,0,50  );
	_hresolE      = tfs->make<TH1F>("hResolE",     "Energy resolution",      100,    -0.2,0.2 );
	_hresolT      = tfs->make<TH1F>("hResolT",     "Time resolution",        100,    -5, 5 );
	_hresolT2     = tfs->make<TH1F>("hResolT2",    "Time resolution",        100,    -2, 2 );

   }





   void CaloDigiInspect::analyze(const art::Event& event) {


       //Get handle to the calorimeter
       art::ServiceHandle<GeometryService> geom;
       if( ! geom->hasElement<Calorimeter>() ) return;
       Calorimeter const & cal = *(GeomHandle<Calorimeter>());

       art::Handle<CaloShowerCollection> caloShowerHandle;
       event.getByLabel(_caloShowerModuleLabel, caloShowerHandle);
       const CaloShowerCollection& caloShowers(*caloShowerHandle);      

       //art::Handle<CaloRecoDigiCollection> recoCaloDigisHandle;
       //event.getByLabel(_caloDigisModuleLabel, recoCaloDigisHandle);
       //const CaloRecoDigiCollection& recoCaloDigis(*recoCaloDigisHandle);

       art::Handle<CaloCrystalHitCollection> CaloCrystalHitHandle;
       event.getByLabel(_caloCrystalHitModuleLabel, CaloCrystalHitHandle);
       const CaloCrystalHitCollection& CaloCrystalHits(*CaloCrystalHitHandle);


       art::Handle<CaloHitMCTruthAssns> caloHitTruthHandle;
       event.getByLabel(_caloHitTruthModuleLabel, caloHitTruthHandle);
       const CaloHitMCTruthAssns& caloHitTruth(*caloHitTruthHandle);




       std::set<const CaloCrystalHit*> hitSet;
       std::set<const CaloShower*> showerSet;
       std::vector<int> nShower(cal.nCrystal(),0);
       std::vector<int> nShowerReco(cal.nCrystal(),0);
       std::vector<int> nHit(cal.nCrystal(),0);
       std::vector<int> nHitReco(cal.nCrystal(),0);
       int nHits(0),nShowers(0);



       //look at the matched hits - MC shower 
       for (auto i=caloHitTruth.begin(), ie = caloHitTruth.end(); i !=ie; ++i)
       {       
             const auto& crystalHitPtr = i->first;
            //const auto& sim = i->second;
	     const CaloShowerPtr& caloShowerPtr = caloHitTruth.data(i);	     
	     
	     hitSet.insert(crystalHitPtr.get());
	     showerSet.insert(caloShowerPtr.get());
             
	     _hDeltaT->Fill(caloShowerPtr->time()-crystalHitPtr->time());
	     _hDeltaTE->Fill(caloShowerPtr->time()-crystalHitPtr->time(),caloShowerPtr->energy());
	     
	     if (caloShowerPtr->time()-crystalHitPtr->time()>10) _hEpileUp->Fill(caloShowerPtr->energy());
	     
	     if (caloShowerPtr->energy() > 1 && caloShowerPtr->time()-crystalHitPtr->time()<5)
	     {
	        _hresolE->Fill(1.0-crystalHitPtr->energyDep()/caloShowerPtr->energy());
	        _hresolT->Fill(caloShowerPtr->time()-crystalHitPtr->time());
	        if (caloShowerPtr->energy() > 10) _hresolT2->Fill(caloShowerPtr->time()-crystalHitPtr->time());
		
	     }
       }
       
       
       //analyze the MC showers
       for (const auto& caloShower : caloShowers)
       {             
	    if (caloShower.time() < 500) continue;
	    
	    nShower[caloShower.crystalId()] +=1;	     
	    ++nShowers;     
	    
	    if (showerSet.find(&caloShower) != showerSet.end())
	    {
	        _hShowerE->Fill(caloShower.energy());
	        _hShowerT->Fill(caloShower.time());
		nShowerReco[caloShower.crystalId()] +=1;
	    }
	    else
	    {
	        _hShowerNoE->Fill(caloShower.energy());
	        _hShowerNoT->Fill(caloShower.time());
	    }
	    
	    
       }


       //analyze the reconstructed hits
       for (const auto& CaloCrystalHit : CaloCrystalHits)
       {             
	    
	    ++nHits;     
	    nHit[CaloCrystalHit.id()] +=1;	
	    
	    if (hitSet.find(&CaloCrystalHit) != hitSet.end())
	    {
	        _hHitE->Fill(CaloCrystalHit.energyDep());
	        _hHitT->Fill(CaloCrystalHit.time());
	        nHitReco[CaloCrystalHit.id()] +=1;		
	    }
	    else
	    {
	        _hHitNoE->Fill(CaloCrystalHit.energyDep());
	        _hHitNoT->Fill(CaloCrystalHit.time());
		
		std::cout<<"Unmatched Reco hit at event id="<<event.id()<<"  crystal id="<<CaloCrystalHit.id()
		         <<"  eDep="<<CaloCrystalHit.energyDep()<<"  time="<<CaloCrystalHit.time()<<std::endl;
	    }

	    
       }

   }







}  

DEFINE_ART_MODULE(mu2e::CaloDigiInspect);

