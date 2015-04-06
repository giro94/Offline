// Andrei Gaponenko, 2015

#include <string>
#include <memory>
#include <array>
#include <vector>
#include <algorithm>

#include "cetlib/exception.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Utilities/InputTag.h"
#include "CLHEP/Vector/ThreeVector.h"

#include "G4Track.hh"
#include "G4Step.hh"
#include "G4VProcess.hh"

#include "Mu2eG4/inc/IMu2eG4Cut.hh"
#include "Mu2eG4/inc/Mu2eG4ResourceLimits.hh"
#include "MCDataProducts/inc/ProcessCode.hh"
#include "Mu2eG4/inc/SimParticleHelper.hh"
#include "MCDataProducts/inc/StepPointMCCollection.hh"
#include "Mu2eG4/inc/getPhysicalVolumeOrThrow.hh"
#include "MCDataProducts/inc/PDGCode.hh"

namespace mu2e {
  namespace Mu2eG4Cuts {
    using namespace std;
    typedef std::vector<fhicl::ParameterSet> PSVector;

    //================================================================
    // A common implementation for some of the required IMu2eG4Cut methods
    class IOHelper: virtual public IMu2eG4Cut {
    public:
      virtual void declareProducts(art::EDProducer *parent) override;
      virtual void finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) override;
      virtual void beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) override;
      virtual void put(art::Event& event) override;

    protected:
      explicit IOHelper(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& mu2elimits)
        : steppingOutputName_(pset.get<string>("write", ""))
        , preSimulatedHitTag_(pset.get<art::InputTag>("preSimulatedHits", art::InputTag()))
        , spHelper_()
        , mu2elimits_(&mu2elimits)
        , overflowWarningPrinted_(false)
      {}

      std::string steppingOutputName_;
      std::unique_ptr<StepPointMCCollection> steppingOutput_;
      art::InputTag preSimulatedHitTag_;
      CLHEP::Hep3Vector mu2eOrigin_;
      const SimParticleHelper *spHelper_;
      const Mu2eG4ResourceLimits *mu2elimits_;
      bool overflowWarningPrinted_; // in the current event

      void addHit(const G4Step *aStep);
    };

    void IOHelper::declareProducts(art::EDProducer *parent) {
      if(!steppingOutputName_.empty()) {
        parent->produces<StepPointMCCollection>(steppingOutputName_);
      }
    }

    void IOHelper::beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) {
      spHelper_ = &spHelper;
      overflowWarningPrinted_ = false;
      if(!steppingOutputName_.empty()) {
        steppingOutput_ = make_unique<StepPointMCCollection>();
        if(preSimulatedHitTag_ != art::InputTag()) {
          const auto& inhits = evt.getValidHandle<StepPointMCCollection>(preSimulatedHitTag_);
          steppingOutput_->reserve(inhits->size());
          for(const auto& hit: *inhits) {
            steppingOutput_->emplace_back(hit);
          }
        }

      }
    }

    void IOHelper::put(art::Event& evt) {
      if(steppingOutput_) {
        evt.put(std::move(steppingOutput_), steppingOutputName_);
      }
    }

    void IOHelper::finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) {
      mu2eOrigin_ = mu2eOriginInWorld;
    }

    void IOHelper::addHit(const G4Step *aStep) {
      if(steppingOutput_->size() < mu2elimits_->maxStepPointCollectionSize()) {

        G4VProcess const* process = aStep->GetPostStepPoint()->GetProcessDefinedStep();
        if(!process) {
          throw cet::exception("GEANT4")<<"ProcessDefinedStep: process not specified for particle "
                                        << aStep->GetTrack()->GetParticleDefinition()->GetParticleName()
                                        << " in file "<<__FILE__<<" line "<<__LINE__
                                        <<" function "<<__func__<<"()\n";
        }

        ProcessCode endCode = ProcessCode::findByName(process->GetProcessName());

        // The point's coordinates are saved in the mu2e coordinate system.
        steppingOutput_->
          push_back(StepPointMC(spHelper_->particlePtr(aStep->GetTrack()),
                                aStep->GetPreStepPoint()->GetTouchableHandle()->GetCopyNumber(),
                                aStep->GetTotalEnergyDeposit(),
                                aStep->GetNonIonizingEnergyDeposit(),
                                aStep->GetPreStepPoint()->GetGlobalTime(),
                                aStep->GetPreStepPoint()->GetProperTime(),
                                aStep->GetPreStepPoint()->GetPosition() - mu2eOrigin_,
                                aStep->GetPreStepPoint()->GetMomentum(),
                                aStep->GetStepLength(),
                                endCode
                                ));
      }
      else {
        if(!overflowWarningPrinted_) {
          overflowWarningPrinted_ = true;
          mf::LogWarning("G4") << "Maximum number of entries reached in steppingOutput collection "
                               << steppingOutputName_ << ": " << steppingOutput_->size() << endl;
        }
      }
    }


    //================================================================
    class Union: virtual public IMu2eG4Cut,
                 public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      // Sequences need a different implementation
      virtual void declareProducts(art::EDProducer *parent) override;
      virtual void beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) override;
      virtual void put(art::Event& event) override;
      virtual void finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) override;

      explicit Union(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      std::vector<std::unique_ptr<IMu2eG4Cut> > cuts_;
    };

    Union::Union(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
    {
      PSVector pars = pset.get<PSVector>("pars");
      for(const auto& p: pars) {
        cuts_.emplace_back(createMu2eG4Cuts(p, lim));
      }
    }

    bool Union::steppingActionCut(const G4Step *step) {
      bool result = false;
      for(const auto& cut : cuts_) {
        if(cut->steppingActionCut(step)) {
          result = true;
          if(steppingOutput_) {
            addHit(step);
          }
          break;
        }
      }
      return result;
    }

    bool Union::stackingActionCut(const G4Track *trk) {
      bool result = false;
      for(const auto& cut : cuts_) {
        if(cut->stackingActionCut(trk)) {
          result = true;
          break;
        }
      }
      return result;
    }

    void Union::declareProducts(art::EDProducer *parent) {
      IOHelper::declareProducts(parent);
      for(auto& cut: cuts_) {
        cut->declareProducts(parent);
      }
    }

    void Union::finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) {
      IOHelper::finishConstruction(mu2eOriginInWorld);
      for(auto& cut: cuts_) {
        cut->finishConstruction(mu2eOriginInWorld);
      }
    }

    void Union::beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) {
      IOHelper::beginEvent(evt, spHelper);
      for(auto& cut: cuts_) {
        cut->beginEvent(evt, spHelper);
      }
    }

    void Union::put(art::Event& evt) {
      IOHelper::put(evt);
      for(auto& cut: cuts_) {
        cut->put(evt);
      }
    }

    //================================================================
    class Intersection: virtual public IMu2eG4Cut,
                        public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      // Sequences need a different implementation
      virtual void declareProducts(art::EDProducer *parent) override;
      virtual void beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) override;
      virtual void put(art::Event& event) override;
      virtual void finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) override;

      explicit Intersection(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      std::vector<std::unique_ptr<IMu2eG4Cut> > cuts_;
    };

    Intersection::Intersection(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
    {
      PSVector pars = pset.get<PSVector>("pars");
      for(const auto& p: pars) {
        cuts_.emplace_back(createMu2eG4Cuts(p, lim));
      }
    }

    bool Intersection::steppingActionCut(const G4Step *step) {
      bool result = true;
      for(const auto& cut : cuts_) {
        if(!cut->steppingActionCut(step)) {
          result = false;
          break;
        }
      }
      if(result && steppingOutput_) {
        addHit(step);
      }

      return result;
    }

    bool Intersection::stackingActionCut(const G4Track *trk) {
      bool result = true;
      for(const auto& cut : cuts_) {
        if(!cut->stackingActionCut(trk)) {
          result = false;
          break;
        }
      }
      return result;
    }

    void Intersection::declareProducts(art::EDProducer *parent) {
      IOHelper::declareProducts(parent);
      for(auto& cut: cuts_) {
        cut->declareProducts(parent);
      }
    }

    void Intersection::finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) {
      IOHelper::finishConstruction(mu2eOriginInWorld);
      for(auto& cut: cuts_) {
        cut->finishConstruction(mu2eOriginInWorld);
      }
    }

    void Intersection::beginEvent(const art::Event& evt, const SimParticleHelper& spHelper) {
      IOHelper::beginEvent(evt, spHelper);
      for(auto& cut: cuts_) {
        cut->beginEvent(evt, spHelper);
      }
    }

    void Intersection::put(art::Event& evt) {
      IOHelper::put(evt);
      for(auto& cut: cuts_) {
        cut->put(evt);
      }
    }

    //================================================================
    class Plane: virtual public IMu2eG4Cut,
                 public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit Plane(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      std::array<double,3> normal_;
      double offset_;

      bool cut_impl(const CLHEP::Hep3Vector& pos);
    };

    Plane::Plane(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
      , offset_()
    {
      // FIXME: use pset.get<array>() when it is available
      vector<double> n{pset.get<vector<double> >("normal")};
      if(n.size() != 3) {
        throw std::runtime_error("SteppingCut::Plane(): normal should be a vector of 3 doubles. Error in pset = "+pset.to_string());
      }
      //std::copy(n.begin(), n.end(), &normal_[0]);
      std::copy(n.begin(), n.end(), normal_.begin());

      vector<double> x0{pset.get<vector<double> >("point")};
      if(x0.size() != 3) {
        throw cet::exception("CONFIG")<<"SteppingCut::Plane(): normal should be a vector of 3 doubles. "
                                      <<"Error in pset = "<<pset.to_string()<<"\n";

      }
      // the cut:
      //             (x-x0)*normal >= 0
      // rewrite as
      //
      //              x*normal >= x0*normal =: offset

      offset_ = std::inner_product(normal_.begin(), normal_.end(), x0.begin(), 0.);
    }

    bool Plane::cut_impl(const CLHEP::Hep3Vector& pos) {
      const bool result =
        (pos.x()*normal_[0] + pos.y()*normal_[1] + pos.z()*normal_[2] >= offset_);

      return result;
    }

    bool Plane::steppingActionCut(const G4Step *step) {
      const CLHEP::Hep3Vector& pos = step->GetPostStepPoint()->GetPosition();
      const bool result = cut_impl(pos);
      if(result && steppingOutput_) {
        addHit(step);
      }
      return result;
    }

    bool Plane::stackingActionCut(const G4Track *trk) {
      const CLHEP::Hep3Vector& pos = trk->GetPosition();
      return cut_impl(pos);
    }

    //================================================================
    class VolumeCut: virtual public IMu2eG4Cut,
                     public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit VolumeCut(const fhicl::ParameterSet& pset, bool negate, const Mu2eG4ResourceLimits& lim);
      virtual void finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) override;
    private:
      std::vector<std::string> volnames_;
      bool negate_;

      // cached pointers to physical volumes on the list
      typedef std::set<const G4VPhysicalVolume*> KillerVolumesCache;
      KillerVolumesCache killerVolumes_;

      bool cut_impl(const G4Track* trk);
    };

    VolumeCut::VolumeCut(const fhicl::ParameterSet& pset, bool negate, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
      , volnames_(pset.get<std::vector<std::string> >("pars"))
      , negate_(negate)
    {}

    void VolumeCut::finishConstruction(const CLHEP::Hep3Vector& mu2eOriginInWorld) {
      IOHelper::finishConstruction(mu2eOriginInWorld);
      for(const auto& vol: volnames_) {
        killerVolumes_.insert(getPhysicalVolumeOrThrow(vol));
      }
    }

    bool VolumeCut::cut_impl(const G4Track* trk) {
      KillerVolumesCache::const_iterator p = killerVolumes_.find(trk->GetVolume());
      bool result = ( p != killerVolumes_.end() );
      if(negate_) result = !result;
      return result;
    }

    bool VolumeCut::steppingActionCut(const G4Step *step) {
      const bool result = cut_impl(step->GetTrack());
      if(result && steppingOutput_) {
        addHit(step);
      }
      return result;
    }

    bool VolumeCut::stackingActionCut(const G4Track *trk) {
      return cut_impl(trk);
    }

    //================================================================
    class ParticleIdCut: virtual public IMu2eG4Cut,
                         public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit ParticleIdCut(const fhicl::ParameterSet& pset, bool negate, const Mu2eG4ResourceLimits& lim);
    private:
      std::vector<int> pdgIds_;
      bool negate_;
      bool cut_impl(const G4Track* trk);
    };

    ParticleIdCut::ParticleIdCut(const fhicl::ParameterSet& pset, bool negate, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
      , pdgIds_(pset.get<std::vector<int> >("pars"))
      , negate_(negate)
    {
      std::sort(pdgIds_.begin(), pdgIds_.end());
    }

    bool ParticleIdCut::cut_impl(const G4Track* trk) {
      const int id(trk->GetDefinition()->GetPDGEncoding());
      bool found = std::binary_search(pdgIds_.begin(), pdgIds_.end(), id);
      return negate_ ?  !found : found;
    }

    bool ParticleIdCut::steppingActionCut(const G4Step *step) {
      const bool result = cut_impl(step->GetTrack());
      if(result && steppingOutput_) {
        addHit(step);
      }
      return result;
    }

    bool ParticleIdCut::stackingActionCut(const G4Track *trk) {
      return cut_impl(trk);
    }

    //================================================================
    class KineticEnergy: virtual public IMu2eG4Cut,
                         public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit KineticEnergy(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      double cut_;
      bool cut_impl(const G4Track* trk);
    };

    KineticEnergy::KineticEnergy(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
      , cut_(pset.get<double>("cut"))
    {}

    bool KineticEnergy::cut_impl(const G4Track* trk) {
      return trk->GetKineticEnergy() < cut_;
    }

    bool KineticEnergy::steppingActionCut(const G4Step *step) {
      const bool result = cut_impl(step->GetTrack());
      if(result && steppingOutput_) {
        addHit(step);
      }
      return result;
    }

    bool KineticEnergy::stackingActionCut(const G4Track *trk) {
      return cut_impl(trk);
    }

    //================================================================
    class PrimaryOnly: virtual public IMu2eG4Cut,
                       public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit PrimaryOnly(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      bool cut_impl(const G4Track* trk);
    };

    PrimaryOnly::PrimaryOnly(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
    {}

    bool PrimaryOnly::cut_impl(const G4Track* trk) {
      return (trk->GetParentID() != 0);
    }

    bool PrimaryOnly::steppingActionCut(const G4Step *step) {
      const bool result = cut_impl(step->GetTrack());
      if(result && steppingOutput_) {
        addHit(step);
      }
      return result;
    }

    bool PrimaryOnly::stackingActionCut(const G4Track *trk) {
      return cut_impl(trk);
    }

    //================================================================
    class Constant: virtual public IMu2eG4Cut,
                    public IOHelper
    {
    public:
      virtual bool steppingActionCut(const G4Step  *step);
      virtual bool stackingActionCut(const G4Track *trk);

      explicit Constant(bool val, const Mu2eG4ResourceLimits& lim);
      explicit Constant(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim);
    private:
      bool value_;
    };

    Constant::Constant(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim)
      : IOHelper(pset, lim)
      , value_{pset.get<bool>("value")}
    {}

    Constant::Constant(bool val, const Mu2eG4ResourceLimits& lim) : IOHelper(fhicl::ParameterSet(), lim), value_(val) {}

    bool Constant::steppingActionCut(const G4Step *step) {
      if(steppingOutput_) {
        addHit(step);
      }
      return value_;
    }

    bool Constant::stackingActionCut(const G4Track *) {
      return value_;
    }

    //================================================================
  } // end namespace Mu2eG4Cuts

  //================================================================
  std::unique_ptr<IMu2eG4Cut> createMu2eG4Cuts(const fhicl::ParameterSet& pset, const Mu2eG4ResourceLimits& lim) {
    using namespace Mu2eG4Cuts;

    if(pset.is_empty()) return make_unique<Constant>(false, lim); // no cuts

    const string cuttype =  pset.get<string>("type");

    if(cuttype == "union") return make_unique<Union>(pset, lim);
    if(cuttype == "intersection") return make_unique<Intersection>(pset, lim);
    if(cuttype == "plane") return make_unique<Plane>(pset, lim);

    if(cuttype == "inVolume") return make_unique<VolumeCut>(pset, false, lim);
    if(cuttype == "notInVolume") return make_unique<VolumeCut>(pset, true, lim);

    if(cuttype == "pdgId") return make_unique<ParticleIdCut>(pset, false, lim);
    if(cuttype == "notPdgId") return make_unique<ParticleIdCut>(pset, true, lim);

    if(cuttype == "kineticEnergy") return make_unique<KineticEnergy>(pset, lim);
    if(cuttype == "primary") return make_unique<PrimaryOnly>(pset, lim);

    if(cuttype == "constant") return make_unique<Constant>(pset, lim);

    throw cet::exception("CONFIG")<< "mu2e::createMu2eG4Cuts(): can not parse pset = "<<pset.to_string()<<"\n";
  }

} // end namespace mu2e