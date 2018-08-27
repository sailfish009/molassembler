#include "molassembler/Interpret.h"

#include "boost/range/iterator_range_core.hpp"

#include "Delib/AtomCollection.h"
#include "Delib/BondOrderCollection.h"

#include "temple/Containers.h"

#include "molassembler/BondOrders.h"
#include "molassembler/Molecule.h"
#include "molassembler/OuterGraph.h"
#include "molassembler/Graph/InnerGraph.h"

namespace molassembler {

InterpretResult interpret(
  const Delib::ElementTypeCollection& elements,
  const AngstromWrapper& angstromWrapper,
  const Delib::BondOrderCollection& bondOrders,
  const BondDiscretizationOption discretization
) {
  // Discretize bond orders (unfortunately signed type because of Delib signature)
  const int N = elements.size();

  InnerGraph atomCollectionGraph {
    static_cast<InnerGraph::Vertex>(N)
  };

  if(discretization == BondDiscretizationOption::Binary) {
    for(int i = 0; i < N; ++i) {
      for(int j = i + 1; j < N; ++j) {
        double bondOrder = bondOrders.getOrder(i, j);

        if(bondOrder > 0.5) {
          atomCollectionGraph.addEdge(i, j, BondType::Single);
        }
      }
    }
  } else if(discretization == BondDiscretizationOption::RoundToNearest) {
    for(int i = 0; i < N; ++i) {
      for(int j = i + 1; j < N; ++j) {
        double bondOrder = bondOrders.getOrder(i, j);

        if(bondOrder > 0.5) {
          auto bond = static_cast<BondType>(
            std::round(bondOrder) - 1
          );

          if(bondOrder > 6.5) {
            bond = BondType::Sextuple;
          }

          atomCollectionGraph.addEdge(i, j, bond);
        }
      }
    }
  }

  // Calculate the number of components and keep the component map
  std::vector<unsigned> componentMap;
  unsigned numComponents = atomCollectionGraph.connectedComponents(componentMap);

  struct MoleculeParts {
    InnerGraph graph;
    AngstromWrapper angstromWrapper;
  };

  std::vector<MoleculeParts> moleculePrecursors {numComponents};
  std::vector<InnerGraph::Vertex> indexInComponentMap (N);

  /* Maybe
   * - filtered_graph using predicate of componentMap number
   * - copy_graph to new OuterGraph keeping element types and bond orders
   *
   * - alternately, must keep a map of atomcollection index to precursor index
   *   and new precursor atom index in order to transfer edges too
   */

  unsigned nZeroLengthPositions = 0;

  for(int i = 0; i < N; ++i) {
    auto& precursor = moleculePrecursors.at(
      componentMap.at(i)
    );

    // Add a new vertex with element information
    InnerGraph::Vertex newIndex = precursor.graph.addVertex(elements.at(i));

    // Save new index in precursor graph
    indexInComponentMap.at(i) = newIndex;

    if(angstromWrapper.positions.at(i).asEigenVector().norm() <= 1e-14) {
      ++nZeroLengthPositions;
    }

    // Copy over position information adjusted by lengthScale
    precursor.angstromWrapper.positions.push_back(
      angstromWrapper.positions.at(i)
    );
  }

  // Copy over edges and bond orders
  for(
    const InnerGraph::Edge& edge :
    boost::make_iterator_range(
      atomCollectionGraph.edges()
    )
  ) {
    InnerGraph::Vertex source = atomCollectionGraph.source(edge),
                       target = atomCollectionGraph.target(edge);

    // Both source and target are part of the same component (since they are bonded)
    auto& precursor = moleculePrecursors.at(
      componentMap.at(source)
    );

    // Copy over the edge
    precursor.graph.addEdge(
      indexInComponentMap.at(source),
      indexInComponentMap.at(target),
      atomCollectionGraph.bondType(edge)
    );
  }

  // Collect results
  InterpretResult result;

  /* Transform precursors into Molecules. Positions may only be used if there
   * is at most one position very close to (0, 0, 0). Otherwise, we assume that
   * the given positions are faulty or no positional information is present,
   * and only the graph is used to create the Molecules.
   */
  if(nZeroLengthPositions < 2) {
    result.molecules.reserve(moleculePrecursors.size());
    for(auto& precursor : moleculePrecursors) {
      result.molecules.emplace_back(
        OuterGraph {std::move(precursor.graph)},
        precursor.angstromWrapper
      );
    }
  } else {
    result.molecules.reserve(moleculePrecursors.size());
    for(auto& precursor : moleculePrecursors) {
      result.molecules.emplace_back(
        OuterGraph {std::move(precursor.graph)}
      );
    }
  }

  result.componentMap = std::move(componentMap);

  return result;
}

InterpretResult interpret(
  const Delib::ElementTypeCollection& elements,
  const AngstromWrapper& angstromWrapper,
  const BondDiscretizationOption discretization
) {
  return interpret(
    elements,
    angstromWrapper,
    uffBondOrders(elements, angstromWrapper),
    discretization
  );
}

InterpretResult interpret(
  const Delib::AtomCollection& atomCollection,
  const Delib::BondOrderCollection& bondOrders,
  const BondDiscretizationOption discretization
) {
  return interpret(
    atomCollection.getElements(),
    AngstromWrapper {atomCollection.getPositions(), LengthUnit::Bohr},
    bondOrders,
    discretization
  );
}

InterpretResult interpret(
  const Delib::AtomCollection& atomCollection,
  const BondDiscretizationOption discretization
) {
  AngstromWrapper angstromWrapper {atomCollection.getPositions(), LengthUnit::Bohr};

  return interpret(
    atomCollection.getElements(),
    angstromWrapper,
    uffBondOrders(atomCollection.getElements(), angstromWrapper),
    discretization
  );
}

} // namespace molassembler
