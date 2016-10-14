#ifndef INCLUDE_GRAPH_FEATURES_EZ_STEREOCENTER_H
#define INCLUDE_GRAPH_FEATURES_EZ_STEREOCENTER_H

#include "GraphFeature.h"
#include "Molecule.h"
#include <experimental/optional>

/* TODO largely unfinished */

namespace MoleculeManip {

namespace GraphFeatures {

class EZStereocenter : public GraphFeature {
private:
  const std::pair<AtomIndexType, AtomIndexType> _atomPair;
  const Molecule* _molPtr;
  std::experimental::optional<bool> _isZ;

public:
  EZStereocenter() = delete;
  EZStereocenter(
    const std::pair<AtomIndexType, AtomIndexType> atomPair,
    Molecule* molPtr
  ) :
    _atomPair(atomPair),
    _molPtr(molPtr)
  {};

  virtual void assign(const Assignment& assignment) override final {
  
  }

  virtual std::string type() const override final {
    return "EZStereocenter";
  }

  virtual std::set<AtomIndexType> involvedAtoms() const override final {
    return {_atomPair.first, _atomPair.second};
  }

  virtual std::vector<DistanceConstraint> distanceConstraints() const override final {
    
  }



};

} // eo namespace GraphFeatures

} // eo namespace

#endif
