#ifndef INCLUDE_CN_STEREOCENTER_H
#define INCLUDE_CN_STEREOCENTER_H

#include "Stereocenter.h"
#include "steric_uniqueness/Assignment.h"
#include "symmetry_information/Symmetries.h"

using namespace std::string_literals;

namespace MoleculeManip {

// Forward-declare Molecule to avoid dependency
class Molecule;

namespace Stereocenters {

class CNStereocenter : public Stereocenter {
private:
/* Typedefs */
  using AssignmentType = UniqueAssignments::Assignment;

/* State */
  const Symmetry::Name _symmetry;
  // Central atom of the Stereocenter, const on assignment
  const AtomIndexType _centerAtom; 
  // The current state of assignment (if or not, and if so, which)
  boost::optional<unsigned> _assignment;
  // Mapping between next neighbor atom index and symbolic ligand character
  std::map<AtomIndexType, char> _neighborCharMap;
  // Mapping between next neighbor atom index to permutational symmetry position
  std::map<AtomIndexType, unsigned> _neighborSymmetryPositionMap;
  // Vector of rotationally unique Assignments
  std::vector<AssignmentType> _uniqueAssignments;
  
/* Private members */
  /*!
   * Reduce substituent atoms at central atom to a mapping of their indices to
   * a symbolic ligand character. e.g.
   * map{4 -> 'C', 6 -> 'A', 9 -> 'A', 14 -> 'B'}
   */
  std::map<
    AtomIndexType,
    char
  > _reduceSubstituents(
    const std::vector<AtomIndexType>& rankedSubstituentNextAtomIndices,
    const std::set<
      std::pair<
        AtomIndexType,
        AtomIndexType
      >
    >& equalSubstituentPairsSet
  ) const;
  /*!
   * Reduce a mapping of atom indices to symbolic ligand characters to a 
   * character vector
   */
  std::vector<char> _reduceNeighborCharMap(
    const std::map<
      AtomIndexType,
      char
    >& neighborCharMap
  );

public:
/* Constructors */
  CNStereocenter(
    // The symmetry of this Stereocenter
    const Symmetry::Name symmetry,
    // The atom this Stereocenter is centered on
    const AtomIndexType& centerAtom,
    // A partially ordered list of substituents, low to high (TODO check)
    const std::vector<AtomIndexType> partiallySortedSubstituents,
    // A set of pairs denoting which substituents are equal priority
    const std::set<
      std::pair<AtomIndexType, AtomIndexType>
    > equalPairsSet
  ) : _symmetry(symmetry),
      _centerAtom(centerAtom) {
    // TODO continue

  }

/* Modification */
  virtual void assign(const unsigned& assignment) override final;

/* Information */
  virtual double angle(
    const AtomIndexType& i,
    const AtomIndexType& j,
    const AtomIndexType& k
  ) const override final;
  virtual boost::optional<unsigned> assigned() const override final;
  virtual unsigned assignments() const override final;
  virtual std::vector<ChiralityConstraintPrototype> chiralityConstraints() const override final;
  virtual std::set<AtomIndexType> involvedAtoms() const override final;
  virtual std::string type() const override final;

/* Operators */
  friend std::basic_ostream<char>& MoleculeManip::operator << (
    std::basic_ostream<char>& os,
    const std::shared_ptr<MoleculeManip::Stereocenters::Stereocenter>& stereocenterPtr
  );
};

std::map<
  AtomIndexType,
  char
> CNStereocenter::_reduceSubstituents(
  const std::vector<AtomIndexType>& rankedSubstituentNextAtomIndices,
  const std::set<
    std::pair<
      AtomIndexType,
      AtomIndexType
    >
  >& equalSubstituentPairsSet
) const {
  /* the algorithm returns pairs of ligands that are equal (due to some 
   * custom sorting function shenanigans, which is binary). We want to 
   * condense that information into sets of equal ligands, so we restructure
   * the set of pairs to a vector of non-overlapping sets:
   * e.g. set{pair{1, 3}, pair{1, 4}, pair{2, 5}} 
   *  -> vector{set{1, 3, 4}, set{2, 5}}
   */
  auto setsVector = StdlibTypeAlgorithms::makeIndividualSets(
    equalSubstituentPairsSet
  );

  // Add lone substituents to setsVector
  for(const auto& index: rankedSubstituentNextAtomIndices) {
    // if the current substituent index is not in any of the sets
    if(!std::accumulate(
      setsVector.begin(),
      setsVector.end(),
      false,
      [&index](const bool& carry, const std::set<AtomIndexType>& set) {
        return (
          carry
          || set.count(index) == 1
        );
      }
    )) {
      // add a single-atom set
      setsVector.push_back(
        std::set<AtomIndexType>{index}
      );
    }
  }

  /* so now we have e.g.
   * setsVector = vector{set{1, 4}, set{2}, set{3}};
   * rankedSubstituentNextAtomIndices = vector{ 2, 1, 4, 3};
   * -> reduce to {A, B, B, C}
   */

  // create a mapping between indices and ligand symbols
  std::map<
    AtomIndexType,
    char
  > indexSymbolMap;

  const char initialChar = 'A';
  for(const auto& index : rankedSubstituentNextAtomIndices) {
    // find position in setsVector
    unsigned posInSetsVector = 0;
    while(
      setsVector[posInSetsVector].count(index) == 0 
      && posInSetsVector < setsVector.size()
    ) {
      posInSetsVector++;
    }
    indexSymbolMap[index] = initialChar + posInSetsVector;
  }


  return indexSymbolMap;

  /* TODO no use of connectivity information as of yet to determine whether 
   * ligands are bridged!
   */
}

std::vector<char> CNStereocenter::_reduceNeighborCharMap(
  const std::map<
    AtomIndexType,
    char
  >& neighborCharMap
) {
  std::vector<char> ligandSymbols;
  for(const auto& indexCharPair: neighborCharMap) {
    ligandSymbols.push_back(indexCharPair.second);
  }

  std::sort(
    ligandSymbols.begin(),
    ligandSymbols.end()
  );

  return ligandSymbols;
}

void CNStereocenter::assign(const unsigned& assignment) {
  assert(assignment < _uniqueAssignments.size());
  if(!_assignment) { // unassigned previously
    // assign as normal
    _assignment = assignment;

    /* save a mapping of next neighbor indices to symmetry positions after
     * assigning (AtomIndexType -> unsigned).
     *
     * First get the symmetry position mapping (char -> unsigned)
     * this is e.g. map{'A' -> vector{0,2,3}, 'B' -> vector{1}}
     */
    auto charSymmetryPositionsMap = _uniqueAssignments[
      assignment
    ].getCharMap();

    /* assign next neighbor indices using _neighborCharMap, which stores
     * neighbor's AtomIndexType -> 'A' char mapping,
     * e.g. map{4 -> 'A', 16 -> 'B', 23 -> 'A', 26 -> 'A'}
     */
    for(const auto& indexCharPair: _neighborCharMap) {
      assert(
        charSymmetryPositionsMap.at(
          indexCharPair.second // the current index's character, e.g. 'A'
        ).size() > 0 // meaning there are symmetry positions left to assign
      );

      /* reference for better readability: the current character's symmetry
       * positions list:
       */
      std::vector<unsigned>& symmetryPositionsList = charSymmetryPositionsMap.at(
        indexCharPair.second // current character
      );

      // assign in the map
      _neighborSymmetryPositionMap[
        indexCharPair.first
      ] = symmetryPositionsList.at( 
        0 // the first of the available symmetry positions for that char
      );

      // remove that first symmetry position
      symmetryPositionsList.erase(
        symmetryPositionsList.begin()
      );
    }

    // DONE, now _neighborSymmetryPositionMap has a mapping
  } else {
    // just assign, explicitly do NOT change the mapping to symmetry positions
    _assignment = assignment;
  }
}

std::string CNStereocenter::type() const {
  std::string returnString = "CNStereocenter on "s 
    + std::to_string(_centerAtom) + " ("s + Symmetry::name(_symmetry) +"): "s;

  if(_assignment) returnString += std::to_string(_assignment.value());
  else returnString += "u";

  returnString += "/"s + std::to_string(assignments());

  return returnString;
}

std::set<AtomIndexType> CNStereocenter::involvedAtoms() const {
  return {_centerAtom};
}

unsigned CNStereocenter::assignments() const {
  return _uniqueAssignments.size();
}

boost::optional<unsigned> CNStereocenter::assigned() const {
  return _assignment;
}

std::vector<
  Stereocenter::ChiralityConstraintPrototype
> CNStereocenter::chiralityConstraints() const {
  std::vector<ChiralityConstraintPrototype> prototypes;

  // TODO Extract which chirality constraints are needed from Symmetry
  // Unknown which chirality constraints are required for which Symmetry
  // -> Investigate

  // return prototypes;
}

} // eo nested namespace Stereocenters

} // eo namespace

#endif