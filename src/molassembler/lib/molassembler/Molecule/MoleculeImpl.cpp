/*!@file
 * @copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 */

#include "molassembler/Molecule/MoleculeImpl.h"

#include "boost/graph/graphviz.hpp"
#include "boost/graph/isomorphism.hpp"
#include "boost/graph/graph_utility.hpp"
#include "chemical_symmetries/ConstexprProperties.h"
#include "Utils/Constants.h"
#include "Utils/Geometry/AtomCollection.h"
#include "Utils/Typenames.h"

#include "molassembler/Cycles.h"
#include "molassembler/Graph/Canonicalization.h"
#include "molassembler/Graph/GraphAlgorithms.h"
#include "molassembler/Modeling/CommonTrig.h"
#include "molassembler/Modeling/LocalGeometryModel.h"
#include "molassembler/Molecule/AtomEnvironmentHash.h"
#include "molassembler/Molecule/MolGraphWriter.h"
#include "molassembler/Molecule/RankingTree.h"
#include "molassembler/Options.h"
#include "molassembler/Stereopermutators/AbstractPermutations.h"
#include "molassembler/Stereopermutators/FeasiblePermutations.h"

namespace Scine {

namespace molassembler {

Utils::AtomCollection Molecule::Impl::applyCanonicalizationMap(
  const std::vector<AtomIndex>& canonicalizationIndexMap,
  const Utils::AtomCollection& atomCollection
) {
  const unsigned N = atomCollection.size();
  Utils::AtomCollection permuted(N);
  for(unsigned i = 0; i < N; ++i) {
    unsigned newIndex = canonicalizationIndexMap.at(i);
    permuted.setElement(newIndex, atomCollection.getElement(i));
    permuted.setPosition(newIndex, atomCollection.getPosition(i));
  }

  return permuted;
}

void Molecule::Impl::_tryAddAtomStereopermutator(
  AtomIndex candidateIndex,
  StereopermutatorList& stereopermutators
) const {
  // If there is already an atom stereopermutator on this index, stop
  if(stereopermutators.option(candidateIndex)) {
    return;
  }

  RankingInformation localRanking = rankPriority(candidateIndex);

  // Only non-terminal atoms may have permutators
  if(localRanking.sites.size() <= 1) {
    return;
  }

  Symmetry::Name symmetry = inferSymmetry(candidateIndex, localRanking).value_or_eval(
    [&]() {return LocalGeometry::firstOfSize(localRanking.sites.size());}
  );


  // Construct a Stereopermutator here
  auto newStereopermutator = AtomStereopermutator {
    _adjacencies,
    symmetry,
    candidateIndex,
    std::move(localRanking)
  };

  // Default assign the stereocenter if there is only one possible assignment
  if(newStereopermutator.numAssignments() == 1) {
    newStereopermutator.assign(0);
  }

  if(
    !disregardStereopermutator(
      newStereopermutator,
      graph().elementType(candidateIndex),
      graph().cycles(),
      Options::temperatureRegime
    )
  ) {
    stereopermutators.add(std::move(newStereopermutator));
  }
}

void Molecule::Impl::_tryAddBondStereopermutator(
  const BondIndex& bond,
  StereopermutatorList& stereopermutators
) const {
  // If there is already a bond stereopermutator on this edge, stop
  if(stereopermutators.option(bond)) {
    return;
  }

  AtomIndex source = bond.first;
  AtomIndex target = bond.second;

  auto sourceAtomStereopermutatorOption = stereopermutators.option(source);
  auto targetAtomStereopermutatorOption = stereopermutators.option(target);

  // There need to be assigned stereopermutators on both vertices
  if(
    !sourceAtomStereopermutatorOption
    || !targetAtomStereopermutatorOption
    || sourceAtomStereopermutatorOption->assigned() == boost::none
    || targetAtomStereopermutatorOption->assigned() == boost::none
  ) {
    return;
  }

  // Construct a Stereopermutator here
  BondStereopermutator newStereopermutator {
    _adjacencies.inner(),
    stereopermutators,
    bond
  };

  if(newStereopermutator.numStereopermutations() > 1) {
    stereopermutators.add(std::move(newStereopermutator));
  }
}

StereopermutatorList Molecule::Impl::_detectStereopermutators() const {
  StereopermutatorList stereopermutatorList;

#ifdef _OPENMP
  /* Ensure inner's properties are populated to avoid data races in its mutable
   * members
   */
  _adjacencies.inner().populateProperties();
#endif

  // Find AtomStereopermutators
  for(
    AtomIndex candidateIndex = 0;
    candidateIndex < graph().N();
    ++candidateIndex
  ) {
    _tryAddAtomStereopermutator(candidateIndex, stereopermutatorList);
  }

  // Find BondStereopermutators
  for(BondIndex bond : boost::make_iterator_range(graph().bonds())) {
    if(_isGraphBasedBondStereopermutatorCandidate(graph().bondType(bond))) {
      _tryAddBondStereopermutator(bond, stereopermutatorList);
    }
  }

  return stereopermutatorList;
}

void Molecule::Impl::_ensureModelInvariants() const {
  if(graph().inner().connectedComponents() > 1) {
    throw std::logic_error("Molecules must be a single connected component. The supplied graph has multiple");
  }

  if(graph().N() < 1) {
    throw std::logic_error("Molecules must consist of at least one atom!");
  }
}

bool Molecule::Impl::_isValidIndex(const AtomIndex index) const {
  return index < graph().N();
}

bool Molecule::Impl::_isGraphBasedBondStereopermutatorCandidate(
  BondType bondType
) const {
  return (
    bondType == BondType::Double
    || bondType == BondType::Triple
    || bondType == BondType::Quadruple
    || bondType == BondType::Quintuple
    || bondType == BondType::Sextuple
  );
}

void Molecule::Impl::_propagateGraphChange() {
  /* Two cases: If the StereopermutatorList is empty, we can just use detect to
   * find any new stereopermutators in the Molecule.
   */
  if(_stereopermutators.empty()) {
    _stereopermutators = _detectStereopermutators();
    return;
  }

  /* In the other case, we have to recheck absolutely everywhere because of the
   * IUPAC sequence rules. If a stereopermutator's ranking was affected and the
   * stereopermutator has a set assignment, we then need to find the assignment
   * that the previous ranking represented spatially in the new set of
   * assignments and assign the stereopermutator to that.
   */

  GraphAlgorithms::updateEtaBonds(_adjacencies.inner());

  // All graph access after this point must be const for thread safety
  const InnerGraph& inner = _adjacencies.inner();

  /*! @todo
   * Need state propagation for BondStereopermutators, anything else is madness
   */

  const Cycles& cycleData = graph().cycles();

  for(
    const InnerGraph::Vertex vertex :
    boost::make_iterator_range(inner.vertices())
  ) {
    auto stereopermutatorOption = _stereopermutators.option(vertex);
    RankingInformation localRanking = rankPriority(vertex);

    if(stereopermutatorOption) {
      // The atom has become terminal
      if(localRanking.sites.size() <= 1) {
        _stereopermutators.remove(vertex);
        continue;
      }

      // Has the ranking changed?
      if(localRanking == stereopermutatorOption->getRanking()) {
        continue;
      }

      // Are there adjacent bond stereopermutators?
      std::vector<BondIndex> adjacentBondStereopermutators;
      for(BondIndex bond : boost::make_iterator_range(_adjacencies.bonds(vertex))) {
        if(_stereopermutators.option(bond)) {
          adjacentBondStereopermutators.push_back(std::move(bond));
        }
      }

      // Suggest a symmetry if desired
      boost::optional<Symmetry::Name> newSymmetryOption;
      if(Options::symmetryTransition == SymmetryTransition::PrioritizeInferenceFromGraph) {
        newSymmetryOption = inferSymmetry(vertex, localRanking);
      }

      // Propagate the state
      auto oldAtomStereopermutatorStateOption = stereopermutatorOption -> propagate(
        _adjacencies,
        std::move(localRanking),
        newSymmetryOption
      );

      /* If the modified stereopermutator has only one assignment and is
       * unassigned due to the graph change, default-assign it
       */
      if(
        stereopermutatorOption -> numAssignments() == 1
        && stereopermutatorOption -> assigned() == boost::none
      ) {
        stereopermutatorOption -> assign(0);
      }

      // If the change makes the stereopermutator undesirable, remove it
      if(
        disregardStereopermutator(
          *stereopermutatorOption,
          inner.elementType(vertex),
          cycleData,
          Options::temperatureRegime
        )
      ) {
        _stereopermutators.remove(vertex);

        // And all the BondStereopermutators that are placed here too
        for(BondIndex bond : adjacentBondStereopermutators) {
          _stereopermutators.remove(bond);
        }

        continue;
      }

      /* If the chiral state for this atom stereopermutator was not successfully
       * propagated or it is now unassigned, then bond stereopermutators sharing
       * this atom stereopermutator must be removed. Bond stereopermutators can
       * only be undetermined if its constituting atom stereopermutators are
       * assigned.
       */
      if(!stereopermutatorOption->assigned()) {
        for(const BondIndex& bond : adjacentBondStereopermutators) {
          _stereopermutators.remove(bond);
        }

        continue;
      }

      /* If the chiral state for this atom stereopermutator was successfully
       * propagated and/or the permutator could be default-assigned, we can also
       * propagate adjacent BondStereopermutators.
       *
       * TODO we may have to keep track if assignments change within the
       * propagated bondstereopermutators, or if any bond stereopermutators
       * are removed, since this may cause another re-rank!
       */
      if(oldAtomStereopermutatorStateOption) {
        for(const BondIndex& bond : adjacentBondStereopermutators) {
          _stereopermutators.option(bond)->propagateGraphChange(
            *oldAtomStereopermutatorStateOption,
            *stereopermutatorOption,
            inner,
            _stereopermutators
          );
        }
      }
    } else {
      // There is no atom stereopermutator on this vertex, so try to add one
      _tryAddAtomStereopermutator(vertex, _stereopermutators);
    }
  }

  // Look for new bond stereopermutators
  for(BondIndex bond : boost::make_iterator_range(graph().bonds())) {
    if(_isGraphBasedBondStereopermutatorCandidate(graph().bondType(bond))) {
      _tryAddBondStereopermutator(bond, _stereopermutators);
    }
  }
}

/* Public members */
/* Constructors */
Molecule::Impl::Impl() noexcept
  : Impl(Scine::Utils::ElementType::H, Scine::Utils::ElementType::H, BondType::Single) {}

Molecule::Impl::Impl(const Scine::Utils::ElementType element) noexcept {
  InnerGraph& inner = _adjacencies.inner();
  inner.addVertex(element);
}

Molecule::Impl::Impl(
  const Scine::Utils::ElementType a,
  const Scine::Utils::ElementType b,
  const BondType bondType
) noexcept {
  // update _adjacencies
  InnerGraph& inner = _adjacencies.inner();
  InnerGraph::Vertex i = inner.addVertex(a);
  InnerGraph::Vertex j = inner.addVertex(b);
  inner.addEdge(i, j, bondType);
}

Molecule::Impl::Impl(OuterGraph graph)
  : _adjacencies(std::move(graph))
{
  // Initialization
  GraphAlgorithms::updateEtaBonds(_adjacencies.inner());
  _stereopermutators = _detectStereopermutators();
  _ensureModelInvariants();
}

Molecule::Impl::Impl(
  OuterGraph graph,
  const AngstromWrapper& positions,
  const boost::optional<
    std::vector<BondIndex>
  >& bondStereopermutatorCandidatesOptional
) : _adjacencies(std::move(graph))
{
  GraphAlgorithms::updateEtaBonds(_adjacencies.inner());
  _stereopermutators = inferStereopermutatorsFromPositions(
    positions,
    bondStereopermutatorCandidatesOptional
  );
  _ensureModelInvariants();
}

Molecule::Impl::Impl(
  OuterGraph graph,
  StereopermutatorList stereopermutators,
  const AtomEnvironmentComponents canonicalComponents
) : _adjacencies(std::move(graph)),
    _stereopermutators(std::move(stereopermutators)),
    _canonicalComponents(canonicalComponents)
{
  // Initialization
  _ensureModelInvariants();
}

/* Modifiers */
AtomIndex Molecule::Impl::addAtom(
  const Scine::Utils::ElementType elementType,
  const AtomIndex adjacentTo,
  const BondType bondType
) {
  if(!_isValidIndex(adjacentTo)) {
    throw std::out_of_range("Molecule::addAtom: Supplied atom index is invalid!");
  }

  const AtomIndex index = _adjacencies.inner().addVertex(elementType);
  addBond(index, adjacentTo, bondType);
  /* addBond handles the stereopermutator update on adjacentTo and also
   * performs a full-molecule rerank propagation.
   */

  return index;
}

BondIndex Molecule::Impl::addBond(
  const AtomIndex a,
  const AtomIndex b,
  const BondType bondType
) {
  if(!_isValidIndex(a) || !_isValidIndex(b)) {
    throw std::out_of_range("Molecule::addBond: A supplied index is invalid!");
  }

  if(a == b) {
    throw std::logic_error("Molecule::addBond: Cannot add a bond between identical indices!");
  }

  InnerGraph& inner = _adjacencies.inner();

  inner.addEdge(a, b, bondType);

  auto notifySubstituentAddition = [this](const AtomIndex toIndex) {
    /*! @todo Remove any BondStereopermutators on adjacent edges of toIndex (no
     * substituent addition/removal propagation possible yet)
     */
    for(
      const BondIndex& adjacentEdge :
      boost::make_iterator_range(_adjacencies.bonds(toIndex))
    ) {
      _stereopermutators.try_remove(adjacentEdge);
    }
  };

  notifySubstituentAddition(a);
  notifySubstituentAddition(b);

  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;

  return BondIndex {a, b};
}

void Molecule::Impl::applyPermutation(const std::vector<AtomIndex>& permutation) {
  _adjacencies.inner().applyPermutation(permutation);
  _stereopermutators.applyPermutation(permutation);
  _canonicalComponents = AtomEnvironmentComponents::None;
}

void Molecule::Impl::assignStereopermutator(
  const AtomIndex a,
  const boost::optional<unsigned>& assignmentOption
) {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Molecule::assignStereopermutator: Supplied index is invalid!");
  }

  auto stereopermutatorOption = _stereopermutators.option(a);

  if(!stereopermutatorOption) {
    throw std::out_of_range("assignStereopermutator: No stereopermutator at this index!");
  }

  if(
    assignmentOption
    && assignmentOption.value() >= stereopermutatorOption->numAssignments()
  ) {
    throw std::out_of_range("assignStereopermutator: Invalid assignment index!");
  }

  // No need to rerank and remove canonical components if nothing changes
  if(assignmentOption != stereopermutatorOption->assigned()) {
    stereopermutatorOption -> assign(assignmentOption);

    // A reassignment can change ranking! See the RankingTree tests
    _propagateGraphChange();
    _canonicalComponents = AtomEnvironmentComponents::None;
  }
}

void Molecule::Impl::assignStereopermutator(
  const BondIndex& edge,
  const boost::optional<unsigned>& assignmentOption
) {
  if(!_isValidIndex(edge.first) || !_isValidIndex(edge.second)) {
    throw std::out_of_range("Molecule::assignStereopermutator: Supplied bond atom indices is invalid!");
  }

  auto stereopermutatorOption = _stereopermutators.option(edge);

  if(!stereopermutatorOption) {
    throw std::out_of_range("assignStereopermutator: No stereopermutator at this bond!");
  }

  if(
    assignmentOption
    && assignmentOption.value() >= stereopermutatorOption->numAssignments()
  ) {
    throw std::out_of_range("assignStereopermutator: Invalid assignment index!");
  }

  // No need to rerank and remove canonical components if nothing changes
  if(assignmentOption != stereopermutatorOption->assigned()) {
    stereopermutatorOption -> assign(assignmentOption);

    // A reassignment can change ranking! See the RankingTree tests
    _propagateGraphChange();
    _canonicalComponents = AtomEnvironmentComponents::None;
  }
}

void Molecule::Impl::assignStereopermutatorRandomly(const AtomIndex a, random::Engine& engine) {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Molecule::assignStereopermutatorRandomly: Supplied index is invalid!");
  }

  auto stereopermutatorOption = _stereopermutators.option(a);

  if(!stereopermutatorOption) {
    throw std::out_of_range("assignStereopermutatorRandomly: No stereopermutator at this index!");
  }

  stereopermutatorOption->assignRandom(engine);

  // A reassignment can change ranking! See the RankingTree tests
  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

void Molecule::Impl::assignStereopermutatorRandomly(const BondIndex& e, random::Engine& engine) {
  auto stereopermutatorOption = _stereopermutators.option(e);

  if(!stereopermutatorOption) {
    throw std::out_of_range("assignStereopermutatorRandomly: No stereopermutator at this edge!");
  }

  stereopermutatorOption->assignRandom(engine);

  // A reassignment can change ranking! See the RankingTree tests
  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

std::vector<AtomIndex> Molecule::Impl::canonicalize(
  const AtomEnvironmentComponents componentBitmask
) {
  // Generate hashes according to the passed bitmask
  auto vertexHashes = hashes::generate(
    graph().inner(),
    stereopermutators(),
    componentBitmask
  );

  // Get a canonical labelling
  auto labelMap = canonicalAutomorphism(graph().inner(), vertexHashes);

  struct Inverse {
    std::vector<AtomIndex> permutation;

    inline explicit Inverse(const std::vector<int>& ante) {
      unsigned size = ante.size();
      permutation.resize(size);
      for(AtomIndex i = 0; i < size; ++i) {
        permutation.at(ante.at(i)) = i;
      }
    }

    inline AtomIndex operator() (const AtomIndex i) const {
      return permutation.at(i);
    }
  };

  Inverse inverse(labelMap);

  // Apply the labelling change to the graph and all stereopermutators
  applyPermutation(inverse.permutation);

  // Set the underlying canonical components
  _canonicalComponents = componentBitmask;

  return inverse.permutation;
}

void Molecule::Impl::removeAtom(const AtomIndex a) {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Molecule::removeAtom: Supplied index is invalid!");
  }

  if(!graph().canRemove(a)) {
    throw std::logic_error("Removing this atom disconnects the graph!");
  }

  InnerGraph& inner = _adjacencies.inner();

  std::vector<AtomIndex> previouslyAdjacentVertices;
  auto adjacencyIterators = inner.adjacents(a);
  std::copy(
    adjacencyIterators.first,
    adjacencyIterators.second,
    std::back_inserter(previouslyAdjacentVertices)
  );

  // Remove all edges to and from this vertex
  inner.clearVertex(a);

  // Any stereopermutator on this index must be dropped
  _stereopermutators.try_remove(a);

  // Any adjacent bond stereopermutators have to be dropped
  for(
    const BondIndex& adjacentEdge :
    boost::make_iterator_range(_adjacencies.bonds(a))
  ) {
    _stereopermutators.try_remove(adjacentEdge);
  }

  // Remove the vertex itself
  inner.removeVertex(a);

  /* Removing the vertex invalidates some vertex descriptors, which are used
   * liberally in the stereopermutator classes' state. We have to correct
   * all vertex descriptors across all permutators to ensure that chiral state
   * is modeled correctly.
   */
  _stereopermutators.propagateVertexRemoval(a);

  /* call removeSubstituent on all adjacent stereopermutators, with
   * removalPlaceholder as the 'which' parameter, which is what
   * propagateVertexRemoval replaces the removed index with in the
   * stereopermutators' internal state
   */
  for(const auto& indexToUpdate : previouslyAdjacentVertices) {
    if(auto stereopermutatorOption = _stereopermutators.option(indexToUpdate)) {
      auto localRanking = rankPriority(indexToUpdate);

      // If index becomes terminal, drop the stereopermutator
      if(localRanking.sites.size() <= 1) {
        _stereopermutators.remove(indexToUpdate);
        continue;
      }

      boost::optional<Symmetry::Name> newSymmetryOption;
      if(Options::symmetryTransition == SymmetryTransition::PrioritizeInferenceFromGraph) {
        newSymmetryOption = inferSymmetry(indexToUpdate, localRanking);
      }

      stereopermutatorOption->propagate(
        _adjacencies,
        std::move(localRanking),
        newSymmetryOption
      );
    }

    //! @todo BondStereopermutator update
    /*if(_stereopermutators.involving(indexToUpdate)) {
      if(_stereopermutators.at(indexToUpdate) -> type() == Stereopermutators::Type::AtomStereopermutator) {
      } else {
        std::dynamic_pointer_cast<Stereopermutators::BondStereopermutator>(
          _stereopermutators.at(indexToUpdate)
        ) -> removeSubstituent(
          indexToUpdate,
          Stereopermutators::Stereopermutator::removalPlaceholder
        );
      }
    }*/
  }

  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

void Molecule::Impl::removeBond(
  const AtomIndex a,
  const AtomIndex b
) {
  if(!_isValidIndex(a) || !_isValidIndex(b)) {
    throw std::out_of_range("Molecule::removeBond: Supplied index is invalid!");
  }

  InnerGraph& inner = _adjacencies.inner();

  auto edgeOption = inner.edgeOption(a, b);

  if(!edgeOption) {
    throw std::out_of_range("That bond does not exist!");
  }

  InnerGraph::Edge edgeToRemove = edgeOption.value();

  if(!inner.canRemove(edgeToRemove)) {
    throw std::logic_error("Removing this bond separates the molecule into two pieces!");
  }


  /* If there is an BondStereopermutator on this edge, we have to drop it explicitly,
   * since _propagateGraphChange cannot iterate over a now-removed edge.
   */
  _stereopermutators.try_remove(BondIndex {a, b});

  // Remove the edge
  inner.removeEdge(edgeToRemove);

  // Notify all immediately adjacent stereopermutators of the removal
  auto notifyRemoval = [&](const AtomIndex indexToUpdate) {
    if(auto stereopermutatorOption = _stereopermutators.option(indexToUpdate)) {
      auto localRanking = rankPriority(indexToUpdate);

      // In case the central atom becomes terminal, just drop the stereopermutator
      if(localRanking.sites.size() <= 1) {
        _stereopermutators.remove(indexToUpdate);
        return;
      }

      boost::optional<Symmetry::Name> newSymmetryOption;
      if(Options::symmetryTransition == SymmetryTransition::PrioritizeInferenceFromGraph) {
        newSymmetryOption = inferSymmetry(indexToUpdate, localRanking);
      }

      stereopermutatorOption->propagate(
        _adjacencies,
        std::move(localRanking),
        newSymmetryOption
      );
    }

    //! @todo propagation
    /*if(_stereopermutators.involving(indexToUpdate)) {
      if(_stereopermutators.at(indexToUpdate) -> type() == Stereopermutators::Type::AtomStereopermutator) {
      } else {
        std::dynamic_pointer_cast<Stereopermutators::BondStereopermutator>(
          _stereopermutators.at(indexToUpdate)
        ) -> removeSubstituent(
          indexToUpdate,
          removedIndex
        );
      }
    }*/
  };

  notifyRemoval(a);
  notifyRemoval(b);

  /* All other cases, where there may be BondStereopermutators or AtomStereopermutators
   * on a or b, should be handled correctly by _propagateGraphChange.
   */

  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

bool Molecule::Impl::setBondType(
  const AtomIndex a,
  const AtomIndex b,
  const BondType bondType
) {
  if(!_isValidIndex(a) || !_isValidIndex(b)) {
    throw std::out_of_range("Molecule::setBondType: A supplied index is invalid!");
  }

  if(bondType == BondType::Eta) {
    throw std::logic_error(
      "Do not manually change eta bond types, this dynamism is handled internally"
    );
  }

  InnerGraph& inner = _adjacencies.inner();

  auto edgeOption = inner.edgeOption(a, b);
  if(!edgeOption) {
    addBond(a, b, bondType);
    return false;
  }

  inner.bondType(edgeOption.value()) = bondType;
  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
  return true;
}

void Molecule::Impl::setElementType(
  const AtomIndex a,
  const Scine::Utils::ElementType elementType
) {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Molecule::setElementType: This index is invalid!");
  }

  _adjacencies.inner().elementType(a) = elementType;
  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

void Molecule::Impl::setGeometryAtAtom(
  const AtomIndex a,
  const Symmetry::Name symmetryName
) {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Molecule::setGeometryAtAtom: Supplied atom index is invalid");
  }

  auto stereopermutatorOption = _stereopermutators.option(a);

  // If there is no stereopermutator at this position yet, we have to create it
  if(!stereopermutatorOption) {
    RankingInformation localRanking = rankPriority(a);

    if(localRanking.sites.size() != Symmetry::size(symmetryName)) {
      throw std::logic_error(
        "Molecule::setGeometryAtAtom: The size of the supplied symmetry is not "
        " the same as the number of determined sites"
      );
    }

    // Add the stereopermutator irrespective of how many assignments it has
    auto newStereopermutator = AtomStereopermutator {
      _adjacencies,
      symmetryName,
      a,
      std::move(localRanking)
    };

    // Default-assign stereopermutators with only one assignment
    if(newStereopermutator.numAssignments() == 1) {
      newStereopermutator.assign(0u);
    }

    _stereopermutators.add(std::move(newStereopermutator));

    _propagateGraphChange();
    _canonicalComponents = AtomEnvironmentComponents::None;
    return;
  }

  if(
    Symmetry::size(stereopermutatorOption->getSymmetry())
    != Symmetry::size(symmetryName)
  ) {
    throw std::logic_error(
      "Molecule::setGeometryAtAtom: The size of the supplied symmetry is "
      "not the same as that of the existing stereopermutator's current symmetry!"
    );
  }

  if(stereopermutatorOption->getSymmetry() == symmetryName) {
    // Do nothing
    return;
  }

  stereopermutatorOption->setSymmetry(symmetryName, _adjacencies);
  if(stereopermutatorOption->numAssignments() == 1) {
    stereopermutatorOption->assign(0);
  }

  // Remove any adjacent bond stereopermutators since there is no propagation
  for(BondIndex bond : boost::make_iterator_range(_adjacencies.bonds(a))) {
    _stereopermutators.try_remove(bond);
  }

  _propagateGraphChange();
  _canonicalComponents = AtomEnvironmentComponents::None;
}

/* Information */
AtomEnvironmentComponents Molecule::Impl::canonicalComponents() const {
  return _canonicalComponents;
}

boost::optional<Symmetry::Name> Molecule::Impl::inferSymmetry(
  const AtomIndex index,
  const RankingInformation& ranking
) const {
  if(!_isValidIndex(index)) {
    throw std::out_of_range("Molecule::inferSymmetry: Supplied index is invalid!");
  }

  if(graph().degree(index) <= 1) {
    throw std::logic_error(
      "Molecule::inferSymmetry: No geometries exist for terminal atoms"
    );
  }

  return LocalGeometry::inferSymmetry(
    _adjacencies,
    index,
    ranking
  );
}

std::string Molecule::Impl::dumpGraphviz() const {
  MolGraphWriter propertyWriter(&_adjacencies.inner(), &_stereopermutators);

  std::stringstream graphvizStream;

  boost::write_graphviz(
    graphvizStream,
    _adjacencies.inner().bgl(),
    propertyWriter,
    propertyWriter,
    propertyWriter
  );

  return graphvizStream.str();
}

const OuterGraph& Molecule::Impl::graph() const {
  return _adjacencies;
}

const StereopermutatorList& Molecule::Impl::stereopermutators() const {
  return _stereopermutators;
}

StereopermutatorList Molecule::Impl::inferStereopermutatorsFromPositions(
  const AngstromWrapper& angstromWrapper,
  const boost::optional<
    std::vector<BondIndex>
  >& explicitBondStereopermutatorCandidatesOption
) const {
  const AtomIndex size = graph().N();
  StereopermutatorList stereopermutators;


  for(AtomIndex vertex = 0; vertex < size; vertex++) {
    RankingInformation localRanking = rankPriority(vertex, {}, angstromWrapper);

    // Skip terminal atoms
    if(localRanking.sites.size() <= 1) {
      continue;
    }

    Symmetry::Name dummySymmetry = LocalGeometry::firstOfSize(localRanking.sites.size());

    // Construct it
    auto stereopermutator = AtomStereopermutator {
      _adjacencies,
      dummySymmetry,
      vertex,
      std::move(localRanking)
    };

    stereopermutator.fit(_adjacencies, angstromWrapper);

    if(
      !disregardStereopermutator(
        stereopermutator,
        graph().elementType(vertex),
        graph().cycles(),
        Options::temperatureRegime
      )
    ) {
      stereopermutators.add(std::move(stereopermutator));
    }
  }

  auto tryInstantiateBondStereopermutator = [&](const BondIndex& bondIndex) -> void {
    // TODO this is suspiciously close to _tryAddBondStereopermutator

    auto sourceAtomStereopermutatorOption = stereopermutators.option(bondIndex.first);
    auto targetAtomStereopermutatorOption = stereopermutators.option(bondIndex.second);

    // There need to be assigned stereopermutators on both vertices
    if(
      !sourceAtomStereopermutatorOption
      || !targetAtomStereopermutatorOption
      || sourceAtomStereopermutatorOption->assigned() == boost::none
      || targetAtomStereopermutatorOption->assigned() == boost::none
    ) {
      return;
    }

    // Construct a Stereopermutator here
    BondStereopermutator newStereopermutator {
      _adjacencies.inner(),
      stereopermutators,
      bondIndex
    };

    newStereopermutator.fit(
      angstromWrapper,
      *sourceAtomStereopermutatorOption,
      *targetAtomStereopermutatorOption
    );

    if(newStereopermutator.assigned() != boost::none) {
      stereopermutators.add(std::move(newStereopermutator));
    }
  };

  // Is there an explicit list of bonds on which to try BondStereopermutator instantiation?
  if(explicitBondStereopermutatorCandidatesOption) {
    for(const BondIndex& bondIndex : *explicitBondStereopermutatorCandidatesOption) {
      tryInstantiateBondStereopermutator(bondIndex);
    }
  } else {
    // Every bond is a candidate
    for(const BondIndex& bondIndex : boost::make_iterator_range(graph().bonds())) {
      if(_isGraphBasedBondStereopermutatorCandidate(graph().bondType(bondIndex))) {
        tryInstantiateBondStereopermutator(bondIndex);
      }
    }
  }

  return stereopermutators;
}

bool Molecule::Impl::canonicalCompare(
  const Molecule::Impl& other,
  const AtomEnvironmentComponents componentBitmask
) const {
  /* Make sure that the components used to canonicalize each molecule instance
   * are enough to compare them canonically, too
   */
  if(_canonicalComponents < componentBitmask || other._canonicalComponents < componentBitmask) {
    throw std::logic_error("Fewer components were used in canonicalizing a Molecule than are being compared!");
  }

  if(graph().N() != other.graph().N() || graph().B() != other.graph().B()) {
    return false;
  }

  return (
    hashes::identityCompare(graph().inner(), stereopermutators(), other.graph().inner(), other.stereopermutators(), componentBitmask)
    && graph().inner().identicalGraph(other.graph().inner())
  );
}

bool Molecule::Impl::modularCompare(
  const Molecule::Impl& other,
  const AtomEnvironmentComponents componentBitmask
) const {
  const unsigned thisNumAtoms = graph().N();

  /* TODO use shortcut (same graph test) if
   * componentBitmask is components used to canonicalize both *this and other
   */

  if(thisNumAtoms != other.graph().N()) {
    return false;
  }

  // Compare number of bonds too
  if(graph().B() != other.graph().B()) {
    return false;
  }

  /* boost isomorphism will allocate a vector of size maxHash, this is dangerous
   * as the maximum hash can be immense, another post-processing step is needed
   * for the calculated hashes to decrease the memory space requirements
   *
   * This maps the hashes to an incremented number:
   */
  std::vector<hashes::HashType> thisHashes, otherHashes;
  hashes::HashType maxHash;

  std::tie(thisHashes, otherHashes, maxHash) = hashes::narrow(
    hashes::generate(graph().inner(), stereopermutators(), componentBitmask),
    hashes::generate(other.graph().inner(), other.stereopermutators(), componentBitmask)
  );

  // Where the corresponding index from the other graph is stored
  std::vector<AtomIndex> indexMap(thisNumAtoms);

  const auto& thisBGL = _adjacencies.inner().bgl();
  const auto& otherBGL = other._adjacencies.inner().bgl();

  bool isomorphic = boost::isomorphism(
    thisBGL,
    otherBGL,
    boost::make_safe_iterator_property_map(
      indexMap.begin(),
      thisNumAtoms,
      boost::get(boost::vertex_index, thisBGL)
    ),
    hashes::LookupFunctor(thisHashes),
    hashes::LookupFunctor(otherHashes),
    maxHash,
    boost::get(boost::vertex_index, thisBGL),
    boost::get(boost::vertex_index, otherBGL)
  );

  return isomorphic;
}

RankingInformation Molecule::Impl::rankPriority(
  const AtomIndex a,
  const std::vector<AtomIndex>& excludeAdjacent,
  const boost::optional<AngstromWrapper>& positionsOption
) const {
  if(!_isValidIndex(a)) {
    throw std::out_of_range("Supplied atom index is invalid!");
  }

  RankingInformation rankingResult;

  // Expects that bond types are set properly, complains otherwise
  rankingResult.sites = GraphAlgorithms::ligandSiteGroups(
    _adjacencies.inner(),
    a,
    excludeAdjacent
  );

  std::string molGraphviz;
#ifndef NDEBUG
  molGraphviz = dumpGraphviz();
#endif

  // Rank the substituents
  auto expandedTree = RankingTree(
    graph(),
    graph().cycles(),
    stereopermutators(),
    molGraphviz,
    a,
    excludeAdjacent,
    RankingTree::ExpansionOption::OnlyRequiredBranches,
    positionsOption
  );

  rankingResult.substituentRanking = expandedTree.getRanked();

  // Combine site information and substituent ranking into a site ranking
  rankingResult.siteRanking = RankingInformation::rankSites(
    rankingResult.sites,
    rankingResult.substituentRanking
  );

  // Find links between sites
  rankingResult.links = GraphAlgorithms::siteLinks(
    _adjacencies.inner(),
    a,
    rankingResult.sites,
    excludeAdjacent
  );

  return rankingResult;
}

bool Molecule::Impl::operator == (const Impl& other) const {
  if(
    _canonicalComponents == AtomEnvironmentComponents::All
    && other._canonicalComponents == AtomEnvironmentComponents::All
  ) {
    return canonicalCompare(other, AtomEnvironmentComponents::All);
  }

  return modularCompare(other, AtomEnvironmentComponents::All);
}

bool Molecule::Impl::operator != (const Impl& other) const {
  return !(*this == other);
}

} // namespace molassembler

} // namespace Scine
