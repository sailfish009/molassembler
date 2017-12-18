#include "boost/graph/graphviz.hpp"
#include "cyclic_polygons/Minimal.h"
#include "Delib/ElementInfo.h"
#include "symmetry_information/Properties.h"

#include "DistanceGeometry/MoleculeSpatialModel.h"
#include "CommonTrig.h"
#include "Log.h"
#include "StdlibTypeAlgorithms.h"

#include <fstream>

/* TODO
 * - Use the static const MolGraphWriter coloring maps
 */

namespace MoleculeManip {

namespace DistanceGeometry {

// General availability of static constexpr members
constexpr double MoleculeSpatialModel::bondRelativeVariance;
constexpr double MoleculeSpatialModel::angleAbsoluteVariance;

MoleculeSpatialModel::MoleculeSpatialModel(
  const Molecule& molecule,
  const StereocenterList& stereocenterList,
  const DistanceMethod& distanceMethod
) : _molecule(molecule) {
  /* This is overall a pretty complicated constructor since it encompasses the
   * entire conversion from a molecular graph into some model of the relations
   * between atoms, determining which conformations are accessible.
   *
   * The rough sequence of operations is
   * - Make helper variables
   * - Set 1-2 bounds.
   * - Gather information on local geometries of all non-terminal atoms, using
   *   the existing stereocenter data and supplanting it with random-assignment
   *   inferred stereocenters on all other non-terminal atoms or double bonds.
   *
   *   - Copy original molecule's list of stereocenters. Set unassigned ones to
   *     a random assignment (consistent with occurrence statistics)
   *   - Instantiate EZStereocenters on all double bonds that aren't immediately
   *     involved in other stereocenters. This is to ensure that all atoms
   *     involved in non-stereogenic double bonds are also flat in the
   *     resulting 3D structure. They additionally allow extraction of
   *     angle and dihedral angle information just like CNStereocenters.
   *   - Instantiate CNStereocenters on all remaining atoms and default-assign
   *     them. This is so that we can get angle data between substituents easily.
   *     
   * - Set internal angles of all small flat cycles
   * - Set all remaining 1-3 bounds with additional tolerance if atoms involved
   *   in the angle are part of a small cycle
   * - Add EZStereocenter 1-4 bound information
   */

  // Helper variables
  CycleData cycleData {molecule.getGraph()};
  auto smallestCycleMap = makeSmallestCycleMap(
    cycleData,
    molecule
  );

  // Check constraints on static constants
  static_assert(
    0 < bondRelativeVariance && bondRelativeVariance < 1,
    "MoleculeSpatialModel static constant bond relative variance must fulfill"
    "0 < x << 1"
  );
  static_assert(
    0 < angleAbsoluteVariance && angleAbsoluteVariance < Symmetry::smallestAngle,
    "MoleculeSpatialModel static constant angle absolute variance must fulfill"
    "0 < x << (smallest angle any local symmetry returns)"
  );

  // Set 1-2 bounds 
  if(distanceMethod == DistanceMethod::UFFLike) {
    for(const auto& edge: molecule.iterateEdges()) {
      unsigned i = boost::source(edge, molecule.getGraph());
      unsigned j = boost::target(edge, molecule.getGraph());
      auto bondType = molecule.getGraph()[edge].bondType;

      auto bondDistance = Bond::calculateBondDistance(
        molecule.getElementType(i),
        molecule.getElementType(j),
        bondType
      );

      setBondBounds(
        {i, j},
        bondDistance,
        bondRelativeVariance
      );
    }
  } else { // Uniform
    for(const auto& edge: molecule.iterateEdges()) {
      unsigned i = boost::source(edge, molecule.getGraph());
      unsigned j = boost::target(edge, molecule.getGraph());

      setBondBounds(
        {i, j},
        1.5,
        bondRelativeVariance
      );
    }
  }

  // Populate the stereocenterMap with copies of the molecule's stereocenters
  // Start with the existing stereocenters with multiple assignments
  for(const auto& stereocenterPtr : stereocenterList) {
    for(const auto& involvedAtom : stereocenterPtr -> involvedAtoms()) {
      if(_stereocenterMap.count(involvedAtom) == 0) {
        if(stereocenterPtr -> type() == Stereocenters::Type::CNStereocenter) {
          // Downcast the shared ptr
          auto CNSPtr = std::dynamic_pointer_cast<
            Stereocenters::CNStereocenter
          >(stereocenterPtr);

          // Explicit new shared ptr using derived copy-constructor
          _stereocenterMap[involvedAtom] = std::make_shared<
            Stereocenters::CNStereocenter
          >(*CNSPtr);
        } else {
          auto EZSPtr = std::dynamic_pointer_cast<
            Stereocenters::EZStereocenter
          >(stereocenterPtr);

          _stereocenterMap[involvedAtom] = std::make_shared<
            Stereocenters::EZStereocenter
          >(*EZSPtr);
        }

        /* Now we have a stereocenter, but it might be unassigned, in which
         * case angle() will fail! Need to assign unassigned ones
         * at random consistent with the unique assignments' relative
         * occurrences (TODO)
         *
         * Make sure changes are only effected on stereocenters in our local
         * map of stereocenters, we are forbidden from changing the molecular
         * graph by altering assignments
         */
        if(!_stereocenterMap[involvedAtom] -> assigned()) {
          _stereocenterMap[involvedAtom] -> assign(0);
        }
      }
    }
  }

  /* For every non-implicated double bond, create an EZStereocenter 
   * Implicated means here that no stereocenters exist for either of the double
   * edge's atoms.
   *
   * TODO awkwardness here: double / aromatic bond type schism, aromatic bond
   * type not considered
   */
  for(const auto& edgeIndex: molecule.iterateEdges()) {
    if(molecule.getGraph()[edgeIndex].bondType == BondType::Double) {
      auto source = boost::source(edgeIndex, molecule.getGraph()),
           target = boost::target(edgeIndex, molecule.getGraph());

      if(
        _stereocenterMap.count(source) == 0 
        && _stereocenterMap.count(target) == 0
        && molecule.getNumAdjacencies(source) == 3
        && molecule.getNumAdjacencies(target) == 3
      ) {
        // Instantiate without regard for number of assignments
        auto newStereocenterPtr = std::make_shared<Stereocenters::EZStereocenter>(
          source,
          molecule.rankPriority(source, {target}), // exclude shared edge
          target,
          molecule.rankPriority(target, {source})
        );

        // Map source *and* target to the same stereocenterPtr
        _stereocenterMap[source] = newStereocenterPtr;
        _stereocenterMap[target] = newStereocenterPtr;
      }
    }
  }

  /* For every missing non-terminal atom, create a CNStereocenter in the
   * determined geometry
   */
  for(unsigned i = 0; i < molecule.numAtoms(); i++) {
    if(
      _stereocenterMap.count(i) == 0  // not already in the map
      && molecule.getNumAdjacencies(i) > 1 // non-terminal
    ) {
      _stereocenterMap[i] = std::make_shared<Stereocenters::CNStereocenter>(
        molecule.determineLocalGeometry(i),
        i,
        molecule.rankPriority(i)
      );

      // At this point, new stereocenters should have only one assignment
      assert(_stereocenterMap[i] -> numAssignments() == 1);

      // Default assign it to zero
      _stereocenterMap[i] -> assign(0);
    }
  }

  /* For all flat cycles for which the internal angles can be determined
   * exactly, add that information
   */
  for(
    auto cycleIter = cycleData.getCyclesIteratorSizeLE(5);
    !cycleIter.atEnd();
    cycleIter.advance()
  ) {
    const auto edgeDescriptors = cycleIter.getCurrentCycle();
    const unsigned cycleSize = edgeDescriptors.size();

    /* There are a variety of cases here which all need to be treated
     * differently:
     * - Cycle of size 3: Always flat, set angles from cyclic polygons library
     * - Cycle of size 4: If flat (maybe already with a single double bond), use
     *   library to get precise internal angles and set them. Otherwise not all
     *   vertices are coplanar and all angles and dihedrals get a general
     *   tolerance increase
     * - Cycle of size 5: If aromatic, flat and we get precise internal angles
     *   from library. Otherwise, slightly increased angle and dihedral
     *   tolerances
     *
     * Assumptions
     * - Need to find out whether all vertices in cycles of size four are
     *   coplanar if one double bond is present. That seems to be the case for
     *   the few strained molecules collected in tests/strained_... Some more
     *   of these size four cycles are coplanar, especially when heteroatoms
     *   are present, but I don't immediately see the distinguishing criterion.
     */
    if( // flat cases
      cycleSize == 3
      || (
        cycleSize == 4
        && countPlanarityEnforcingBonds(
          edgeDescriptors,
          molecule
        ) >= 1
      )
      /* TODO missing cases: 
       * - Need aromaticity checking routine for cycle size 5
       */
    ) {
      /* Gather sequence of atoms in cycle by progressively converting edge
       * descriptors into vertex indices
       */
      const auto indexSequence = makeRingIndexSequence(edgeDescriptors, molecule);

      /* First, we fetch the angles that maximize the cycle area using the
       * cyclic polygon library.
       */
      const auto cycleInternalAngles = CyclicPolygons::internalAngles(
        // Map sequential index pairs to their purported bond length
        TemplateMagic::mapSequentialPairs( 
          indexSequence,
          [&](const AtomIndexType& i, const AtomIndexType& j) -> double {
            auto bondTypeOption = molecule.getBondType(i, j);

            // These vertices really ought to be bonded
            assert(bondTypeOption);

            return Bond::calculateBondDistance(
              molecule.getElementType(i),
              molecule.getElementType(j),
              bondTypeOption.value()
            );
          }
        )
      );

      /* The first angle returned is the angle between edges one and two, which
       * are indices [0, 1] and [1, 2] respectively. The last angle is between
       * edges [n - 1, 0], [0, 1].
       *
       * So, e.g. C4, C7, N9, O3::
       *
       *   index sequence 4, 7, 9, 3, 4
       *   distances  C-C, C-N, N-O, O-C
       *   angles  C-C-N, C-N-O, N-O-C, O-C-C
       *                                ^- Last overlaps past index sequence
       *
       * Now we set all internal angles with low tolerance
       */
      // Make sure all assumptions we make about sequence lengths are true
      assert(indexSequence.size() == cycleInternalAngles.size() + 1);
      assert(indexSequence.size() == cycleSize + 1);
      // All non-overlapping triples
      for(
        unsigned angleCentralIndex = 1;
        angleCentralIndex < indexSequence.size() - 1;
        ++angleCentralIndex
      ) {
        setAngleBoundsIfEmpty(
          {
            indexSequence.at(angleCentralIndex - 1),
            indexSequence.at(angleCentralIndex),
            indexSequence.at(angleCentralIndex + 1)
          },
          cycleInternalAngles.at(angleCentralIndex - 1),
          angleAbsoluteVariance
        );
      }

      // There's a missing triple with the previous approach, which is always
      setAngleBoundsIfEmpty(
        {
          indexSequence.at(indexSequence.size() - 2),
          indexSequence.at(0),
          indexSequence.at(1)
        },
        cycleInternalAngles.back(),
        angleAbsoluteVariance
      );

      /* Internal-external and external-external angles where the central
       * atom is part of a cycle are taken care of in the general angles 
       * calculation below.
       */
    } 
    /* Non-flat cases are also taken care of below using a general tolerance 
     * increase on angles which directly effects the purported dihedral
     * distances.
     */
  }

  /* Returns a multiplier intended for the absolute angle variance for an atom
   * index. If that index is in a cycle of size < 6, the multiplier is > 1.
   */
  auto cycleMultiplierForIndex = [&](const AtomIndexType& i) -> double {
    if(smallestCycleMap.count(i) == 1) {
      if(smallestCycleMap.at(i) == 3) {
        return 2.5;
      } else if(smallestCycleMap.at(i) == 4) {
        return 1.7;
      } else if(smallestCycleMap.at(i) == 5) {
        return 1.3;
      }
    }

    return 1.0;
  };

  /* Model spiro centers */
  /* For an atom to be a spiro center, it needs to be contained in exactly two
   * URFs and have four substituents in a tetrahedral symmetry.
   *
   * We also only have to worry about modeling them if the two URFs are
   * particularly small, i.e. are sizes 3-5
   */
  for(AtomIndexType i = 0; i < _molecule.numAtoms(); ++i) {
    if(
      _molecule.getNumAdjacencies(i) == 4
      && _stereocenterMap.count(i) == 1
      && _stereocenterMap.at(i) -> type() == Stereocenters::Type::CNStereocenter
    ) {
      if(
        std::dynamic_pointer_cast<Stereocenters::CNStereocenter>(
          _stereocenterMap.at(i)
        ) -> getSymmetry() == Symmetry::Name::Tetrahedral
        && cycleData.numCycleFamilies(i) == 2
      ) {
        unsigned* URFIDs;
        auto nIDs = RDL_getURFsContainingNode(cycleData.getDataPtr(), i, &URFIDs);
        assert(nIDs == 2);

        bool allURFsSingularRC = true;
        for(unsigned idIdx = 0; idIdx < nIDs; ++idIdx) {
          if(RDL_getNofRCForURF(cycleData.getDataPtr(), URFIDs[idIdx]) > 1) {
            allURFsSingularRC = false;
            break;
          }
        }

        if(allURFsSingularRC) {
          // The two RCs still have to be disjoint save for i
          RDL_cycleIterator* cycleIteratorOne = RDL_getRCyclesForURFIterator(cycleData.getDataPtr(), URFIDs[0]);
          RDL_cycle* cycleOne = RDL_cycleIteratorGetCycle(cycleIteratorOne);
          RDL_cycleIterator* cycleIteratorTwo = RDL_getRCyclesForURFIterator(cycleData.getDataPtr(), URFIDs[1]);
          RDL_cycle* cycleTwo = RDL_cycleIteratorGetCycle(cycleIteratorTwo);

          // We model them only if both are small.
          if(cycleOne -> weight <= 5 && cycleTwo -> weight <= 5) {
            auto makeVerticesSet = [](const auto& cyclePtr) -> std::set<AtomIndexType> {
              std::set<AtomIndexType> vertices;

              for(unsigned i = 0; i < cyclePtr -> weight; ++i) {
                vertices.insert(cyclePtr->edges[i][0]);
                vertices.insert(cyclePtr->edges[i][1]);
              }

              return vertices;
            };

            auto cycleOneVertices = makeVerticesSet(cycleOne);
            auto cycleTwoVertices = makeVerticesSet(cycleTwo);

            auto intersection = TemplateMagic::setIntersection(
              cycleOneVertices,
              cycleTwoVertices
            );

            if(intersection.size() == 1 && *intersection.begin() == i) {
              auto iAdjacents = molecule.getAdjacencies(i);

              std::vector<AtomIndexType> firstAdjacents, secondAdjacents;
              for(const auto& iAdjacent : iAdjacents) {
                if(cycleOneVertices.count(iAdjacent)) {
                  firstAdjacents.push_back(iAdjacent);
                }

                if(cycleTwoVertices.count(iAdjacent)) {
                  secondAdjacents.push_back(iAdjacent);
                }
              }

              assert(firstAdjacents.size() == 2 && secondAdjacents.size() == 2);

              auto firstSequence = orderedIndexSequence<3>({
                firstAdjacents.front(),
                i,
                firstAdjacents.back()
              });

              auto secondSequence = orderedIndexSequence<3>({
                secondAdjacents.front(),
                i,
                secondAdjacents.back()
              });

              auto populateBounds = [&](const auto& sequence) {
                ValueBounds bounds;

                /* The cycle internal angle may have been generated already
                 * if that cycle is planar, so check the angle bounds 
                 */
                if(_angleBounds.count(sequence)) {
                  bounds = _angleBounds.at(sequence);
                } else {
                  // We have to generate some bounds on the cycle internal angle
                  double centralAngle = _stereocenterMap.at(i) -> angle(
                    sequence.front(),
                    i,
                    sequence.back()
                  );

                  double multiplier = cycleMultiplierForIndex(sequence.front())
                    * cycleMultiplierForIndex(sequence.back());

                  bounds.lower = StdlibTypeAlgorithms::clamp(
                    centralAngle - multiplier * angleAbsoluteVariance,
                    0.0,
                    M_PI
                  );

                  bounds.upper = StdlibTypeAlgorithms::clamp(
                    centralAngle + multiplier * angleAbsoluteVariance,
                    0.0,
                    M_PI
                  );
                }

                return bounds;
              };

              ValueBounds firstAngleBounds = populateBounds(firstSequence);
              ValueBounds secondAngleBounds = populateBounds(secondSequence);

              // Increases in cycle angles yield decrease in the cross angle
              double crossAngleLower = StdlibTypeAlgorithms::clamp(
                spiroCrossAngle(
                  firstAngleBounds.upper,
                  secondAngleBounds.upper
                ),
                0.0,
                M_PI
              );

              double crossAngleUpper = StdlibTypeAlgorithms::clamp(
                spiroCrossAngle(
                  firstAngleBounds.lower,
                  secondAngleBounds.lower
                ),
                0.0,
                M_PI
              );

              TemplateMagic::forAllPairs(
                firstAdjacents,
                secondAdjacents,
                [&](const auto& firstAdjacent, const auto& secondAdjacent) {
                  _angleBounds.emplace(
                    std::make_pair(
                      orderedIndexSequence<3>({firstAdjacent, i, secondAdjacent}),
                      ValueBounds {
                        crossAngleLower,
                        crossAngleUpper
                      }
                    )
                  );
                }
              );
            }
          }

          // Must manually free the cycles and iterators
          RDL_deleteCycle(cycleOne);
          RDL_deleteCycle(cycleTwo);

          RDL_deleteCycleIterator(cycleIteratorOne);
          RDL_deleteCycleIterator(cycleIteratorTwo);
        }

        // Must manually free the id array
        free(URFIDs);
      }
    }
  }

  // Set 1-3 bounds using all angles we can exploit from stereocenters
  for(const auto& mapIterPair : _stereocenterMap) {
    const auto& stereocenterPtr = mapIterPair.second;

    for(const auto& centralIndex : stereocenterPtr -> involvedAtoms()) {
      // All combinations between molecule of this index
      auto adjacentIndices = molecule.getAdjacencies(centralIndex);

      TemplateMagic::forAllPairs(
        adjacentIndices,
        [&](const AtomIndexType& i, const AtomIndexType& j) -> void {
          double multiplier = cycleMultiplierForIndex(i) * cycleMultiplierForIndex(j);

          setAngleBoundsIfEmpty(
            {
              i,
              centralIndex,
              j
            },
            stereocenterPtr -> angle(
              i,
              centralIndex,
              j
            ),
            multiplier * angleAbsoluteVariance
          );
        }
      );
    }
  }
  
  // Add EZStereocenter dihedral limit information
  // TODO these need tolerance adaptation in dependence of cycles too!
  for(const auto& mapIterPair : _stereocenterMap) {
    const auto& mappingIndex = mapIterPair.first;
    const auto& stereocenterPtr = mapIterPair.second;
    if(
      // Filter out CNStereocenters
      stereocenterPtr -> type() == Stereocenters::Type::EZStereocenter
      /* Since EZStereocenters are in the map twice (for each of their
       * involvedAtoms()), and we only want to process their dihedral
       * information once, check the mapping index against the first index in
       * the involvedAtoms set
       */
      && mappingIndex == *(
        stereocenterPtr -> involvedAtoms().begin()
      )
    ) {
      for(const auto& dihedralLimit : stereocenterPtr -> dihedralLimits()) {
        setDihedralBoundsIfEmpty(
          dihedralLimit.indices,
          dihedralLimit.lower,
          dihedralLimit.upper
        );
      }
    }
  }
}

void MoleculeSpatialModel::setBondBounds(
  const std::array<AtomIndexType, 2>& bondIndices,
  const double& centralValue,
  const double& relativeVariance
) {
  /* Ensure correct use, variance for bounds should at MOST smaller than half
   * of the central value
   */
  assert(relativeVariance < 0.5 * centralValue);

  // C++17: try_emplace
  auto indexSequence = orderedIndexSequence<2>(bondIndices);
  if(_bondBounds.count(indexSequence) == 0) {
    _bondBounds.emplace(
      std::make_pair(
        indexSequence,
        ValueBounds {
          (1 - relativeVariance) * centralValue,
          (1 + relativeVariance) * centralValue
        }
      )
    );
  }
}

/*!
 * Adds the angle bounds to the model, but only if the information for that
 * set of indices does not exist yet.
 */
void MoleculeSpatialModel::setAngleBoundsIfEmpty(
  const std::array<AtomIndexType, 3>& angleIndices,
  const double& centralValue,
  const double& absoluteVariance
) {
  auto orderedIndices = orderedIndexSequence<3>(angleIndices);

  // C++17: try_emplace
  if(_angleBounds.count(orderedIndices) == 0) {
    _angleBounds.emplace(
      std::make_pair(
        orderedIndices,
        ValueBounds {
          StdlibTypeAlgorithms::clamp(
            centralValue - absoluteVariance,
            0.0,
            M_PI
          ),
          StdlibTypeAlgorithms::clamp(
            centralValue + absoluteVariance,
            0.0,
            M_PI
          )
        }
      )
    );
  }
}

/*!
 * Adds the dihedral bounds to the model, but only if the information for that
 * set of indices does not exist yet.
 */
void MoleculeSpatialModel::setDihedralBoundsIfEmpty(
  const std::array<AtomIndexType, 4>& dihedralIndices,
  const double& lower,
  const double& upper
) {
  assert(lower < upper);

  auto orderedIndices = orderedIndexSequence<4>(dihedralIndices);

  // C++17: try_emplace
  if(_dihedralBounds.count(orderedIndices) == 0) {
    _dihedralBounds.emplace(
      std::make_pair(
        orderedIndices,
        ValueBounds {
          StdlibTypeAlgorithms::clamp(
            lower,
            -M_PI,
            M_PI
          ),
          StdlibTypeAlgorithms::clamp(
            upper,
            -M_PI,
            M_PI
          )
        }
      )
    );
  }
}

void MoleculeSpatialModel::addDefaultDihedrals() {
  for(const auto& edgeDescriptor : _molecule.iterateEdges()) {
    const auto& sourceIndex = boost::source(edgeDescriptor, _molecule.getGraph());
    const auto& targetIndex = boost::target(edgeDescriptor, _molecule.getGraph());

    auto sourceAdjacencies = _molecule.getAdjacencies(sourceIndex);
    auto targetAdjacencies = _molecule.getAdjacencies(targetIndex);

    TemplateMagic::inplaceRemove(sourceAdjacencies, targetIndex);
    TemplateMagic::inplaceRemove(targetAdjacencies, sourceIndex);

    // Now, every combination
    for(const auto& sourceAdjacentIndex : sourceAdjacencies) {
      for(const auto& targetAdjacentIndex : targetAdjacencies) {
        setDihedralBoundsIfEmpty(
          {
            sourceAdjacentIndex,
            sourceIndex,
            targetIndex,
            targetAdjacentIndex,
          },
          0,
          M_PI
        );
      }
    }
  }
}

DistanceBoundsMatrix MoleculeSpatialModel::makeDistanceBounds() const {
  /* TODO
   * - This merely adds information that exists explicitly in form of 
   *   constrained internal variables. Much more dihedral information could
   *   be placed into the distance bounds matrix if all angle bounds are
   *   present.
   * - Could use the Molecule to ensure all information is gathered
   *   or alternatively reduce parameter requirements to number of atoms only
   * - Dihedral setting angle lookup may fail, no checks!
   * - Smooth at the end and check for inconsistencies
   */
  DistanceBoundsMatrix distanceBounds {_molecule.numAtoms()};

  // Start with bonds
  for(const auto& bondPair : _bondBounds) {
    const auto& indexSequence = bondPair.first;
    const auto& bondBounds = bondPair.second;

    distanceBounds.setLowerBound(
      indexSequence.front(),
      indexSequence.back(),
      bondBounds.lower
    );

    distanceBounds.setUpperBound(
      indexSequence.front(),
      indexSequence.back(),
      bondBounds.upper
    );
  }

  assert(distanceBounds.boundInconsistencies() == 0);

  // Next are angles
  for(const auto& anglePair : _angleBounds) {
    const auto& indexSequence = anglePair.first;
    const auto& angleBounds = anglePair.second;

    distanceBounds.setLowerBound(
      indexSequence.front(),
      indexSequence.back(),
      CommonTrig::lawOfCosines(
        distanceBounds.lowerBound(
          indexSequence.front(),
          indexSequence.at(1)
        ),
        distanceBounds.lowerBound(
          indexSequence.at(1),
          indexSequence.back()
        ),
        angleBounds.lower
      )
    );

    distanceBounds.setUpperBound(
      indexSequence.front(),
      indexSequence.back(),
      CommonTrig::lawOfCosines(
        distanceBounds.upperBound(
          indexSequence.front(),
          indexSequence.at(1)
        ),
        distanceBounds.upperBound(
          indexSequence.at(1),
          indexSequence.back()
        ),
        angleBounds.upper
      )
    );
  }

  assert(distanceBounds.boundInconsistencies() == 0);

  // Then dihedrals
  for(const auto& dihedralPair : _dihedralBounds) {
    const auto& indexSequence = dihedralPair.first;
    const auto& dihedralBounds = dihedralPair.second;

    const auto& abAngleBounds = _angleBounds.at(
      orderedIndexSequence<3>({
        indexSequence.at(0),
        indexSequence.at(1),
        indexSequence.at(2)
      })
    );

    const auto& bcAngleBounds = _angleBounds.at(
      orderedIndexSequence<3>({
        indexSequence.at(1),
        indexSequence.at(2),
        indexSequence.at(3)
      })
    );

    distanceBounds.setLowerBound(
      indexSequence.front(),
      indexSequence.back(),
      CommonTrig::dihedralLength(
        distanceBounds.lowerBound(
          indexSequence.front(),
          indexSequence.at(1)
        ),
        distanceBounds.lowerBound(
          indexSequence.at(1),
          indexSequence.at(2)
        ),
        distanceBounds.lowerBound(
          indexSequence.at(2),
          indexSequence.back()
        ),
        abAngleBounds.lower,
        bcAngleBounds.lower,
        dihedralBounds.lower // cis dihedral
      )
    );

    distanceBounds.setUpperBound(
      indexSequence.front(),
      indexSequence.back(),
      CommonTrig::dihedralLength(
        distanceBounds.upperBound(
          indexSequence.front(),
          indexSequence.at(1)
        ),
        distanceBounds.upperBound(
          indexSequence.at(1),
          indexSequence.at(2)
        ),
        distanceBounds.upperBound(
          indexSequence.at(2),
          indexSequence.back()
        ),
        abAngleBounds.upper,
        bcAngleBounds.upper,
        dihedralBounds.upper // trans dihedral
      )
    );
  }

  assert(distanceBounds.boundInconsistencies() == 0);

  // Finally, raise non-bonded items with no lower bound to sum of vdw radii
  for(unsigned i = 0; i < _molecule.numAtoms() - 1; ++i) {
    for(unsigned j = i + 1; j < _molecule.numAtoms(); ++j) {
      /* setting the bounds will fail for bonded pairs as those have strict
       * bounds already and the fairly high sum of vdw would lead to
       * inconsistencies
       */
      if(distanceBounds.lowerBound(i, j) == 0) {
        distanceBounds.setLowerBound(
          i,
          j,
          AtomInfo::vdwRadius(
            _molecule.getElementType(i)
          ) + AtomInfo::vdwRadius(
            _molecule.getElementType(j)
          ) 
        );
      }
    }
  }

  assert(distanceBounds.boundInconsistencies() == 0);

  distanceBounds.smooth();

  assert(distanceBounds.boundInconsistencies() == 0);

  return distanceBounds;
}

std::vector<
  Stereocenters::ChiralityConstraintPrototype
> MoleculeSpatialModel::getChiralityPrototypes() const {
  std::vector<Stereocenters::ChiralityConstraintPrototype> prototypes;

  for(const auto& iterPair : _stereocenterMap) {
    const auto& stereocenterPtr = iterPair.second;

    auto chiralityPrototypes = stereocenterPtr -> chiralityConstraints();
    std::copy(
      chiralityPrototypes.begin(),
      chiralityPrototypes.end(),
      std::back_inserter(prototypes)
    );
  }

  return prototypes;
}

void MoleculeSpatialModel::dumpDebugInfo() const {
  auto& logRef = Log::log(Log::Level::Debug);
  logRef << "MoleculeSpatialModel debug info" << std::endl;

  // Bonds
  for(const auto& bondIterPair : _bondBounds) {
    const auto& indexArray = bondIterPair.first;
    const auto& bounds = bondIterPair.second;
    logRef << "Bond " << TemplateMagic::condenseIterable(indexArray)
      << ": [" << bounds.lower << ", " << bounds.upper << "]" << std::endl;
  }

  // Angles
  for(const auto& angleIterPair : _angleBounds) {
    const auto& indexArray = angleIterPair.first;
    const auto& bounds = angleIterPair.second;
    logRef << "Angle " << TemplateMagic::condenseIterable(indexArray)
      << ": [" << bounds.lower << ", " << bounds.upper << "]" << std::endl;
  }

  // Dihedrals
  for(const auto& dihedralIterPair : _dihedralBounds) {
    const auto& indexArray = dihedralIterPair.first;
    const auto& bounds = dihedralIterPair.second;
    logRef << "Dihedral " << TemplateMagic::condenseIterable(indexArray)
      << ": [" << bounds.lower << ", " << bounds.upper << "]" << std::endl;
  }
}

struct MoleculeSpatialModel::ModelGraphWriter {
  /* Settings to determine appearance */
  // Color maps
  const std::map<
    std::string,
    std::string
  > elementBGColorMap {
    {"H", "white"},
    {"C", "gray"},
    {"N", "blue"},
    {"O", "red"}
  };

  const std::map<
    std::string,
    std::string
  > elementTextColorMap {
    {"H", "black"},
    {"C", "white"},
    {"N", "white"},
    {"O", "white"}
  };

  const std::map<
    BondType,
    std::string
  > bondTypeDisplayString {
    {BondType::Single, R"(color = "black")"},
    {BondType::Double, R"(color = "black:invis:black")"},
    {BondType::Triple, R"(color = "black:invis:black:invis:black")"},
    {BondType::Quadruple, R"(label = "4")"},
    {BondType::Quintuple, R"(label = "5")"},
    {BondType::Sextuple, R"(label = "6")"},
    {BondType::Aromatic, R"(style = "dashed")"},
    {BondType::Eta, R"(style = "dotted")"}
  };

  /* State */
  // We promise to be good and not change anything
  const GraphType* const graphPtr;
  const MoleculeSpatialModel& spatialModel;

/* Constructor */
  ModelGraphWriter(
    const GraphType* passGraphPtr,
    const MoleculeSpatialModel& spatialModel
  ) : graphPtr(passGraphPtr),
    spatialModel(spatialModel)
  {}

/* Helper functions */
  Delib::ElementType getElementType(const AtomIndexType& vertexIndex) const {
    return (*graphPtr)[vertexIndex].elementType;
  }
  
/* Accessors for boost::write_graph */
  // Global options
  void operator() (std::ostream& os) const {
    os << "graph [fontname = \"Arial\", layout = neato];\n"
      << "node [fontname = \"Arial\", shape = circle, style = filled];\n"
      << "edge [fontname = \"Arial\"];\n";

    /* Additional nodes */
    for(const auto& stereocenterIterPair : spatialModel._stereocenterMap) {
      const auto& mappingIndex = stereocenterIterPair.first;
      const auto& stereocenterPtr = stereocenterIterPair.second;

      if( 
        stereocenterPtr->type() == Stereocenters::Type::EZStereocenter
      ) { 
        // Avoid writing EZStereocenters twice
        if(*stereocenterPtr->involvedAtoms().rbegin() == mappingIndex) {
          continue;
        }

        char ezState = 'u';
        if(stereocenterPtr -> assigned()) {
          if(stereocenterPtr -> assigned().value() == 1) {
            ezState = 'E';
          } else {
            ezState = 'Z';
          }
        }

        os << "EZ" << TemplateMagic::condenseIterable(
            stereocenterPtr -> involvedAtoms(),
            ""
          ) << R"( [label=")" << ezState 
          << R"(", fillcolor="tomato", shape="square", fontcolor="white", )" 
          << R"(tooltip=")" 
          << stereocenterPtr -> info()
          << R"("];)" << "\n";
      } else {
        const auto cnPtr = std::dynamic_pointer_cast<Stereocenters::CNStereocenter>(
          stereocenterPtr
        );

        os << "CN" << TemplateMagic::condenseIterable(
          stereocenterPtr -> involvedAtoms(),
          ""
        ) << R"( [label=")" << Symmetry::name(cnPtr -> getSymmetry()) 
          << R"(", fillcolor="steelblue", shape="square", fontcolor="white", )"
          << R"(tooltip=")" << cnPtr -> info()
          << R"("];)" << "\n";
      }
    }
  }

  // Vertex options
  void operator() (std::ostream& os, const AtomIndexType& vertexIndex) const {
    const std::string symbolString = Delib::ElementInfo::symbol(
      getElementType(vertexIndex)
    );

    os << "[";
    
    // Add element name and index label
    os << R"(label = ")" << symbolString << vertexIndex << R"(")";

    // Coloring
    if(elementBGColorMap.count(symbolString) != 0u) {
      os << R"(, fillcolor=")" << elementBGColorMap.at(symbolString) << R"(")";
    } else { // default
      os << R"(, fillcolor="white")";
    }
    if(elementTextColorMap.count(symbolString) != 0u) {
      os << R"(, fontcolor=")" << elementTextColorMap.at(symbolString) << R"(")";
    } else { // default
      os << R"(, fontcolor="orange")";
    }

    // Font sizing
    if(symbolString == "H") {
      os << ", fontsize=10, width=.3, fixedsize=true";
    }

    // Any angles this atom is the central atom in
    std::vector<std::string> angleStrings;
    for(const auto& angleIterPair : spatialModel._angleBounds) {
      const auto& indexSequence = angleIterPair.first;
      const auto& angleBounds = angleIterPair.second;

      if(indexSequence.at(1) == vertexIndex) {
        angleStrings.emplace_back(
          "["s + std::to_string(indexSequence.at(0)) + ","s 
          + std::to_string(indexSequence.at(2)) +"] -> ["s 
          + std::to_string(
            ConstexprMagic::Math::round(
              ConstexprMagic::Math::toDegrees(angleBounds.lower)
            )
          ) + ", "s + std::to_string(
            ConstexprMagic::Math::round(
              ConstexprMagic::Math::toDegrees(angleBounds.upper)
            )
          ) + "]"s
        );
      }
    }

    if(angleStrings.empty()) {
      os << R"(, tooltip="no angles here")";
    } else {
      os << R"(, tooltip="angles: )" << TemplateMagic::condenseIterable(
        angleStrings,
        "&#10;"s
      ) << R"(")";
    }
    
    os << "]\n";

    // Are there any additional vertices we ought to connect to?
    if(spatialModel._stereocenterMap.count(vertexIndex) == 1) {
      const auto& stereocenterPtr = spatialModel._stereocenterMap.at(vertexIndex);
      if(stereocenterPtr -> type() == Stereocenters::Type::CNStereocenter) {
        os << ";CN";
      } else {
        os << ";EZ";
      }

      os << TemplateMagic::condenseIterable(stereocenterPtr -> involvedAtoms(), "")
        << " -- " << vertexIndex << R"([color="gray", dir="forward", len="2"])";
    }
  }

  // Edge options
  void operator() (std::ostream& os, const EdgeIndexType& edgeIndex) const {
    const AtomIndexType source = boost::source(edgeIndex, *graphPtr);
    const AtomIndexType target = boost::target(edgeIndex, *graphPtr);

    os << "[";

    // Bond Type display options
    auto bondType = (*graphPtr)[edgeIndex].bondType;
    if(bondTypeDisplayString.count(bondType) != 0u) {
      os << bondTypeDisplayString.at(bondType);
    }

    // If one of the bonded atoms is a hydrogen, shorten the bond
    /*if(
      getElementType(target) == Delib::ElementType::H
      || getElementType(source) == Delib::ElementType::H
    ) {
      os << ", len=0.5";
    }*/

    os << ", penwidth=3";

    const auto indexSequence = orderedIndexSequence<2>({source, target});
    if(spatialModel._bondBounds.count(indexSequence) == 1) {
      const auto& bondBounds = spatialModel._bondBounds.at(indexSequence);
      os << R"(, edgetooltip="[)" << bondBounds.lower << ", " << bondBounds.upper <<  R"(]")";
    }

    os << "]";
  }
};

void MoleculeSpatialModel::writeGraphviz(const std::string& filename) const {
  ModelGraphWriter graphWriter(
    &_molecule.getGraph(),
    *this
  );

  std::ofstream outStream(filename);

  boost::write_graphviz(
    outStream,
    _molecule.getGraph(),
    graphWriter,
    graphWriter,
    graphWriter
  );

  outStream.close();
}

double MoleculeSpatialModel::spiroCrossAngle(const double& alpha, const double& beta) {
  // The source of this equation is explained in documents/
  return std::acos(
    -1 * std::cos(alpha / 2) * std::cos(beta / 2)
  );
}

} // namespace DistanceGeometry

} // namespace MoleculeManip
