/*!@file
 * @copyright This code is licensed under the 3-clause BSD license.
 *   Copyright ETH Zurich, Laboratory of Physical Chemistry, Reiher Group.
 *   See LICENSE.txt for details.
 *
 * The implementation of this consumer-facing type is a little complicated, so
 * it warrants an explanation at length. Since molassembler mostly pImpls its
 * public types, this class was another opportunity to hide dependencies from
 * library consumers. But this one is a little tricky, because the boost graph
 * dependency brings with it vertex and edge descriptors which need to be freely
 * distributable to work with the graph itself.
 *
 * In order to hide those types, alternate vertex and edge descriptors are
 * defined in Types.h (AtomIndex and BondIndex) and the conversion to the
 * underlying boost graph descriptors is implemented in Graph/Bridge.h.
 *
 * The iterators giving easy access to atoms, bonds, adjacents and incidents
 * have to be declared in the header, these are all template specializations of
 * the InnerBasedIterator facade, which are themselves pImpl-ed. Their
 * implementations are in GraphIterators.cpp.
 *
 * The PrivateGraph type is the Impl struct of Graph, except it's not a
 * Graph-local type, but a free type that many implementation details of
 * other molassembler components expect to be passed.
 */

#include "Molassembler/Graph.h"

#include "Utils/Bonds/BondOrderCollection.h"
#include "Utils/Typenames.h"

#include "Molassembler/Cycles.h"
#include "Molassembler/Graph/Bridge.h"
#include "Molassembler/Modeling/BondDistance.h"
#include "Molassembler/Molecule/MolGraphWriter.h"

#include "boost/graph/graphviz.hpp"

namespace Scine {
namespace Molassembler {

static_assert(
  std::is_same<AtomIndex, PrivateGraph::Vertex>::value,
  "Atomic index types are mismatched!"
);

Graph::Graph(Graph&& other) noexcept = default;
Graph& Graph::operator = (Graph&& other) noexcept = default;
Graph::Graph(const Graph& other) : innerPtr_(
  std::make_unique<PrivateGraph>(*other.innerPtr_)
) {}
Graph& Graph::operator = (const Graph& other) {
  *innerPtr_ = *other.innerPtr_;
  return *this;
}
Graph::~Graph() = default;

Graph::Graph() : innerPtr_(
  std::make_unique<PrivateGraph>()
) {}

Graph::Graph(PrivateGraph&& inner) : innerPtr_(
  std::make_unique<PrivateGraph>(std::move(inner))
) {}

Utils::ElementTypeCollection Graph::elementCollection() const {
  const AtomIndex size = N();

  Utils::ElementTypeCollection elements;
  elements.reserve(size);

  for(AtomIndex i = 0; i < size; ++i) {
    elements.push_back(elementType(i));
  }

  return elements;
}

Utils::ElementType Graph::elementType(const AtomIndex a) const {
  return inner().elementType(a);
}

AtomIndex Graph::N() const {
  return inner().N();
}

unsigned Graph::B() const {
  return inner().B();
}

boost::optional<std::vector<AtomIndex>> Graph::modularIsomorphism(
  const Graph& other,
  const AtomEnvironmentComponents components
) const {
  return inner().modularIsomorphism(
    other.inner(),
    components
  );
}

//! Determine which vertices belong to which side of a bridge edge
std::pair<
  std::vector<AtomIndex>,
  std::vector<AtomIndex>
> Graph::splitAlongBridge(BondIndex bridge) const {
  return inner().splitAlongBridge(
    toInner(bridge, inner())
  );
}

bool Graph::operator == (const Graph& other) const {
  // Better with newer boost: has_value()
  return static_cast<bool>(
    modularIsomorphism(other, AtomEnvironmentComponents::All)
  );
}

bool Graph::adjacent(const AtomIndex a, const AtomIndex b) const {
  return static_cast<bool>(
    inner().edgeOption(a, b)
  );
}

std::vector<AtomIndex> Graph::atomsOfElement(const Utils::ElementType e) const {
  std::vector<AtomIndex> matches;
  for(AtomIndex i : inner().vertices()) {
    if(inner().elementType(i) == e) {
      matches.push_back(i);
    }
  }
  return matches;
}

boost::optional<BondIndex> Graph::bond(const AtomIndex a, const AtomIndex b) const {
  if(auto edgeOption = inner().edgeOption(a, b)) {
    return toOuter(edgeOption.value(), inner());
  }

  return boost::none;
}

Utils::BondOrderCollection Graph::bondOrders() const {
  Utils::BondOrderCollection BOs(inner().N());

  for(const auto edge : inner().edges()) {
    BOs.setOrder(
      inner().source(edge),
      inner().target(edge),
      Bond::bondOrderMap.at(
        static_cast<std::underlying_type_t<BondType>>(
          inner().bondType(edge)
        )
      )
    );
  }

  return BOs;
}

BondType Graph::bondType(const BondIndex& edge) const {
  return inner().bondType(toInner(edge, inner()));
}

bool Graph::canRemove(const AtomIndex a) const {
  return inner().canRemove(a);
}

bool Graph::canRemove(const BondIndex& edge) const {
  return inner().canRemove(
    toInner(edge, inner())
  );
}

const Cycles& Graph::cycles() const {
  return inner().cycles();
}

PrivateGraph& Graph::inner() {
  return *innerPtr_;
}

const PrivateGraph& Graph::inner() const {
  return *innerPtr_;
}

unsigned Graph::degree(const AtomIndex a) const {
  return inner().degree(a);
}

std::string Graph::dumpGraphviz() const {
  MolGraphWriter propertyWriter(&inner(), nullptr);

  std::stringstream graphvizStream;

  boost::write_graphviz(
    graphvizStream,
    inner().bgl(),
    propertyWriter,
    propertyWriter,
    propertyWriter
  );

  return graphvizStream.str();
}

IteratorRange<Graph::AtomIterator> Graph::atoms() const {
  return {
    AtomIterator(inner(), true),
    AtomIterator(inner(), false)
  };
}

IteratorRange<Graph::BondIterator> Graph::bonds() const {
  return {
    BondIterator(inner(), true),
    BondIterator(inner(), false)
  };
}

IteratorRange<Graph::AdjacencyIterator> Graph::adjacents(const AtomIndex a) const {
  return {
    AdjacencyIterator(a, inner(), true),
    AdjacencyIterator(a, inner(), false)
  };
}

IteratorRange<Graph::IncidentEdgesIterator> Graph::bonds(const AtomIndex a) const {
  return {
    IncidentEdgesIterator(a, inner(), true),
    IncidentEdgesIterator(a, inner(), false)
  };
}

} // namespace Molassembler
} // namespace Scine
