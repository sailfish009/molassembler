#include "DistanceGeometry/ImplicitGraphBoost.h"
#include "boost/graph/two_bit_color_map.hpp"


#define USE_SPECIALIZED_GOR1_ALGORITHM
#ifdef USE_SPECIALIZED_GOR1_ALGORITHM
#include "DistanceGeometry/Gor1.h"
#else
#include "Graph/Gor1.h"
#endif

#include "Molecule.h"
#include "DistanceGeometry/DistanceGeometry.h"
#include "AtomInfo.h"
#include "template_magic/Random.h"

#include "boost/graph/bellman_ford_shortest_paths.hpp"

namespace MoleculeManip {

namespace DistanceGeometry {

/* Class Implementation */

ImplicitGraph::ImplicitGraph(
  const Molecule& molecule,
  const BoundList& bounds
) : _moleculePtr(&molecule)
{
  unsigned N = molecule.numAtoms();
  _distances.resize(N, N);
  _distances.setZero();

  // Populate _distances with bounds from list
  VertexDescriptor a, b;
  ValueBounds bound;
  for(const auto& boundTuple : bounds) {
    std::tie(a, b, bound) = boundTuple;

    upperBound(a, b) = bound.upper;
    lowerBound(a, b) = bound.lower;
  }

  // Determine the two heaviest element types in the molecule, O(N)
  _heaviestAtoms = {Delib::ElementType::H, Delib::ElementType::H};
  for(AtomIndexType i = 0; i < N; ++i) {
    auto elementType = molecule.getElementType(i);
    if(
      static_cast<unsigned>(elementType) 
      > static_cast<unsigned>(_heaviestAtoms.back())
    ) {
      _heaviestAtoms.back() = elementType;

      if(
        static_cast<unsigned>(_heaviestAtoms.back()) 
        > static_cast<unsigned>(_heaviestAtoms.front())
      ) {
        std::swap(_heaviestAtoms.front(), _heaviestAtoms.back());
      }
    }
  }
}

void ImplicitGraph::addBound(
  const VertexDescriptor& a,
  const VertexDescriptor& b,
  const ValueBounds& bound
) {
  if(a < b) {
    _distances(a, b) = bound.upper;
    _distances(b, a) = bound.lower;
  } else {
    _distances(a, b) = bound.lower;
    _distances(b, a) = bound.upper;
  }
}

ImplicitGraph::VertexDescriptor ImplicitGraph::num_vertices() const {
  return 2 * _distances.outerSize();
}

ImplicitGraph::VertexDescriptor ImplicitGraph::num_edges() const {
  /* Every entry in the upper triangle indicates a bound
   * For every upper bound, there are 4 edges
   * For every lower bound, there are 2 edges
   * If there is no entry in the upper triangle, there is still an implicit
   * lower bound.
   */
  unsigned N = _distances.outerSize();
  unsigned count = 0;

  for(unsigned i = 0; i < N - 1; ++i) {
    for(unsigned j = i + 1; j < N; ++j) {
      if(_distances(i, j) > 0) {
        ++count;
      }
    }
  }

  return 4 * count + N * (N - 1);
}

std::pair<ImplicitGraph::EdgeDescriptor, bool> ImplicitGraph::edge(const VertexDescriptor& i, const VertexDescriptor& j) const {
  const auto a = internal(i);
  const auto b = internal(j);

  // Right-to-left edges never exist
  if(!isLeft(i) && isLeft(j)) {
    return {
      EdgeDescriptor {},
      false
    };
  }

  // Left-to-right edges always exist, whether explicit or implicit does not matter
  if(isLeft(i) && !isLeft(j) && a != b) {
    return {
      EdgeDescriptor {i, j},
      true
    };
  }

  unsigned N = _distances.outerSize();

  if(a < N && b < N && _distances(a, b) != 0) {
    return {
      EdgeDescriptor {i, j}, 
      true
    };
  }

  return {
    EdgeDescriptor {},
    false
  };
}

bool ImplicitGraph::hasExplicit(const EdgeDescriptor& edge) const {
  assert(isLeft(edge.first) && !isLeft(edge.second));

  return _distances(internal(edge.first), internal(edge.second)) != 0;
}

Eigen::MatrixXd ImplicitGraph::makeDistanceBounds() const {
  Eigen::MatrixXd bounds;

  unsigned N = _distances.outerSize();
  bounds.resize(N, N);
  bounds.setZero();

  unsigned M = num_vertices();
  std::vector<double> distances (M);
  std::vector<VertexDescriptor> predecessors (M);
  using ColorMapType = boost::two_bit_color_map<>;
  ColorMapType color_map {M};

  for(VertexDescriptor a = 0; a < N; ++a) {
    // Perform a single shortest paths calculation for a

    auto predecessor_map = boost::make_iterator_property_map(
      predecessors.begin(),
      VertexIndexMap()
    );

    auto distance_map = boost::make_iterator_property_map(
      distances.begin(),
      VertexIndexMap()
    );

    // re-fill color map with white
    std::fill(
      color_map.data.get(),
      color_map.data.get() + (color_map.n + color_map.elements_per_char - 1) 
        / color_map.elements_per_char,
      0
    );

#ifdef USE_SPECIALIZED_GOR1_ALGORITHM
    boost::gor1_ig_shortest_paths(
      *this,
      VertexDescriptor {left(a)},
      predecessor_map,
      color_map,
      distance_map
    );
#else
    boost::gor1_simplified_shortest_paths(
      *this,
      VertexDescriptor {left(a)},
      predecessor_map,
      color_map,
      distance_map
    );
#endif

    for(VertexDescriptor b = a + 1; b < N; ++b) {
      // a is always smaller than b, hence (a, b) is the upper bound
      bounds(a, b) = distances.at(left(b));
      bounds(b, a) = -distances.at(right(b));

      assert(bounds(a, b) > bounds(b, a));
    }
  }

  return bounds;
}

Eigen::MatrixXd& ImplicitGraph::makeDistanceMatrix() {
  unsigned N = _moleculePtr->numAtoms();

  std::vector<AtomIndexType> indices(N);
  std::iota(
    indices.begin(),
    indices.end(),
    0
  );

  std::shuffle(
    indices.begin(),
    indices.end(),
    TemplateMagic::random.randomEngine
  );

  unsigned M = num_vertices();
  std::vector<double> distances (M);
  std::vector<VertexDescriptor> predecessors (M);

  using ColorMapType = boost::two_bit_color_map<>;
  // Determine triangle inequality limits for pair
  ColorMapType color_map {M};

  for(const AtomIndexType& a : indices) {
    std::vector<AtomIndexType> otherIndices (N - 1);
    std::iota(
      otherIndices.begin(),
      otherIndices.begin() + a,
      0
    );

    std::iota(
      otherIndices.begin() + a,
      otherIndices.end(),
      a
    );

    std::shuffle(
      otherIndices.begin(),
      otherIndices.end(),
      TemplateMagic::random.randomEngine
    );

    for(const AtomIndexType& b : otherIndices) {
      if(
        a == b
        || (
          _distances(a, b) == _distances(b, a)
          && _distances(a, b) != 0
        )
      ) {
        // Skip on-diagonal and already-chosen entries
        continue;
      }

      auto predecessor_map = boost::make_iterator_property_map(
        predecessors.begin(),
        VertexIndexMap()
      );

      auto distance_map = boost::make_iterator_property_map(
        distances.begin(),
        VertexIndexMap()
      );

      // re-fill color map with white
      std::fill(
        color_map.data.get(),
        color_map.data.get() + (color_map.n + color_map.elements_per_char - 1) 
          / color_map.elements_per_char,
        0
      );

#ifdef USE_SPECIALIZED_GOR1_ALGORITHM
      boost::gor1_ig_shortest_paths(
        *this,
        VertexDescriptor {left(a)},
        predecessor_map,
        color_map,
        distance_map
      );
#else
      boost::gor1_simplified_shortest_paths(
        *this,
        VertexDescriptor {left(a)},
        predecessor_map,
        color_map,
        distance_map
      );
#endif

      assert(-distances.at(right(b)) < distances.at(left(b)));

      // Pick fixed distance
      double fixedDistance = TemplateMagic::random.getSingle<double>(
        -distances.at(right(b)),
        distances.at(left(b))
      );

      // Record in distances matrix
      _distances(a, b) = fixedDistance;
      _distances(b, a) = fixedDistance;
    }
  }

  return _distances;
}

ImplicitGraph::VertexDescriptor ImplicitGraph::out_degree(VertexDescriptor i) const {
  unsigned count = 0;
  unsigned N = _distances.outerSize();
  VertexDescriptor a = internal(i);
  for(VertexDescriptor b = 0; b < N; ++b) {
    if(_distances(a, b) != 0) {
      ++count;
    }
  }

  if(isLeft(i)) {
    return count + N - 1;
  }

  return count;
}

double& ImplicitGraph::lowerBound(const VertexDescriptor& a, const VertexDescriptor& b) {
  return _distances(
    std::max(a, b),
    std::min(a, b)
  );
}

double& ImplicitGraph::upperBound(const VertexDescriptor& a, const VertexDescriptor& b) {
  return _distances(
    std::min(a, b),
    std::max(a, b)
  );
}

double ImplicitGraph::lowerBound(const VertexDescriptor& a, const VertexDescriptor& b) const {
  return _distances(
    std::max(a, b),
    std::min(a, b)
  );
}

double ImplicitGraph::upperBound(const VertexDescriptor& a, const VertexDescriptor& b) const {
  return _distances(
    std::min(a, b),
    std::max(a, b)
  );
}

double ImplicitGraph::maximalImplicitLowerBound(const VertexDescriptor& i) const {
  assert(isLeft(i));
  auto a = internal(i);
  auto elementType = _moleculePtr->getElementType(a);

  if(elementType == _heaviestAtoms.front()) {
    return AtomInfo::vdwRadius(
      _heaviestAtoms.back()
    ) + AtomInfo::vdwRadius(elementType);
  } 

  return AtomInfo::vdwRadius(
    _heaviestAtoms.front()
  ) + AtomInfo::vdwRadius(elementType);
}

/* Nested classes */
/* Edge weight property map */
ImplicitGraph::EdgeWeightMap::EdgeWeightMap(const ImplicitGraph& base)
  : _basePtr(&base)
{}

double ImplicitGraph::EdgeWeightMap::operator [] (const EdgeDescriptor& e) const {
  auto a = internal(e.first);
  auto b = internal(e.second);

  if(isLeft(e.first) && !isLeft(e.second)) {
    double explicitValue = _basePtr->lowerBound(a, b);

    if(explicitValue) {
      return -explicitValue;
    }

    return -(
      AtomInfo::vdwRadius(
        _basePtr->_moleculePtr->getElementType(a)
      ) + AtomInfo::vdwRadius(
        _basePtr->_moleculePtr->getElementType(b)
      )
    );
  }

  return _basePtr->upperBound(a, b);
}

double ImplicitGraph::EdgeWeightMap::operator () (const EdgeDescriptor& e) const {
  return this->operator[](e);
}

ImplicitGraph::EdgeWeightMap ImplicitGraph::getEdgeWeightPropertyMap() const {
  return {*this};
}

/* Vertex iterator */
ImplicitGraph::vertex_iterator::vertex_iterator() = default;
ImplicitGraph::vertex_iterator::vertex_iterator(ImplicitGraph::VertexDescriptor i) : index(i) {}
ImplicitGraph::vertex_iterator::vertex_iterator(const ImplicitGraph::vertex_iterator& other) = default;
ImplicitGraph::vertex_iterator::vertex_iterator(ImplicitGraph::vertex_iterator&& other) = default;
ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator = (const ImplicitGraph::vertex_iterator& other) = default;
ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator = (ImplicitGraph::vertex_iterator&& other) = default;

bool ImplicitGraph::vertex_iterator::operator == (const ImplicitGraph::vertex_iterator& other) const {
  return index == other.index;
}

bool ImplicitGraph::vertex_iterator::operator != (const ImplicitGraph::vertex_iterator& other) const {
  return index != other.index;
}

ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator ++ () {
  ++index;
  return *this;
}

ImplicitGraph::vertex_iterator ImplicitGraph::vertex_iterator::operator ++ (int) {
  vertex_iterator copy = *this;
  ++index;
  return copy;
}

ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator -- () {
  --index;
  return *this;
}

ImplicitGraph::vertex_iterator ImplicitGraph::vertex_iterator::operator -- (int) {
  vertex_iterator copy = *this;
  --index;
  return copy;
}

ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator + (unsigned i) {
  index += i;
  return *this;
}

ImplicitGraph::vertex_iterator& ImplicitGraph::vertex_iterator::operator - (unsigned i) {
  index -= i;
  return *this;
}

ImplicitGraph::VertexDescriptor ImplicitGraph::vertex_iterator::operator * () const {
  return index;
}

int ImplicitGraph::vertex_iterator::operator - (const vertex_iterator& other) const {
  return static_cast<int>(index) - static_cast<int>(other.index);
}

ImplicitGraph::vertex_iterator ImplicitGraph::vbegin() const {
  return {};
}

ImplicitGraph::vertex_iterator ImplicitGraph::vend() const {
  return {num_vertices()};
}

/* Edge iterator */
ImplicitGraph::edge_iterator::edge_iterator() = default;
ImplicitGraph::edge_iterator::edge_iterator(
  const ImplicitGraph& base,
  VertexDescriptor i
) : _basePtr {&base},
    _i {i}
{
  auto a = internal(_i);

  _b = 0;
  if(a == 0) {
    ++_b;
  }

  _crossGroup = false;
  unsigned N = _basePtr->_distances.outerSize();

  if(a < N) {
    while(_b < N && !_basePtr->_distances(a, _b)) {
      ++_b;
    }
  }
}

ImplicitGraph::edge_iterator::edge_iterator(const ImplicitGraph::edge_iterator& other) = default;
ImplicitGraph::edge_iterator::edge_iterator(ImplicitGraph::edge_iterator&& other) = default;
ImplicitGraph::edge_iterator& ImplicitGraph::edge_iterator::operator = (const ImplicitGraph::edge_iterator& other) = default;
ImplicitGraph::edge_iterator& ImplicitGraph::edge_iterator::operator = (ImplicitGraph::edge_iterator&& other) = default;

ImplicitGraph::edge_iterator& ImplicitGraph::edge_iterator::operator ++ () {
  _increment();
  return *this;
}

ImplicitGraph::edge_iterator ImplicitGraph::edge_iterator::operator ++ (int) {
  edge_iterator copy = *this;
  ++(*this);
  return copy;
}

// unordered_map::iterator is a ForwardIterator, decrement impossible

bool ImplicitGraph::edge_iterator::operator == (const edge_iterator& other) const {
  return (
    _crossGroup == other._crossGroup
    && _b == other._b
    && _i == other._i
    && _basePtr == other._basePtr
  );
}

bool ImplicitGraph::edge_iterator::operator != (const edge_iterator& other) const {
  return !(*this == other);
}

double ImplicitGraph::edge_iterator::weight() const {
  auto a = internal(_i);
  if(_crossGroup) {
    double data = _basePtr->lowerBound(a, _b);

    if(data) {
      return -data;
    }

    return -(
      AtomInfo::vdwRadius(
        _basePtr->_moleculePtr->getElementType(a)
      ) + AtomInfo::vdwRadius(
        _basePtr->_moleculePtr->getElementType(_b)
      )
    );
  }

  return _basePtr->upperBound(a, _b);
}

ImplicitGraph::VertexDescriptor ImplicitGraph::edge_iterator::target() const {
  if(_crossGroup) {
    return right(_b);
  } 

  if(isLeft(_i)) {
    return left(_b);
  }

  return right(_b);
}

ImplicitGraph::EdgeDescriptor ImplicitGraph::edge_iterator::operator * () const {
  return {
    _i,
    target()
  };
}

void ImplicitGraph::edge_iterator::_increment() {
  unsigned N = _basePtr->_distances.outerSize();
  auto a = internal(_i);

  if(!_crossGroup) {
    ++_b;
    // Find the next explicit information
    while(_b < N && !_basePtr->_distances(a, _b)) {
      ++_b;
    }

    if(_b == N) {
      if(isLeft(_i)) {
        // Roll over to implicits only if _is is left
        _crossGroup = true;
        _b = 0;
        if(_b == a) {
          ++_b;
        }
      } else {
        // Right vertices have no implicits
        ++_i;
        _b = 0;

        a = internal(_i);

        if(_b == a) {
          ++_b;
        }

        if(a < N) {
          // Search for next explicit
          while(_b < N && !_basePtr->_distances(a, _b)) {
            ++_b;
          }
        }
      }
    }
  } else {
    ++_b;

    if(_b == a) {
      ++_b;
    }

    if(_b == N) {
      // Rollover to next explicits
      _crossGroup = false;
      _b = 0;
      ++_i;

      a = internal(_i);

      if(_b == a) {
        ++_b;
      }

      while(_b < N && !_basePtr->_distances(a, _b)) {
        ++_b;
      }
    }
  }
}

std::string ImplicitGraph::edge_iterator::state() const {
  using namespace std::string_literals;

  return std::to_string(_i) + " "s
    + std::to_string(_b) + " "s
    + std::to_string(_crossGroup);
}

ImplicitGraph::edge_iterator ImplicitGraph::ebegin() const {
  return {
    *this, 
    0
  };
}

ImplicitGraph::edge_iterator ImplicitGraph::eend() const {
  return {
    *this, 
    num_vertices()
  };
}


/* Out edge iterator */
ImplicitGraph::edge_iterator ImplicitGraph::obegin(VertexDescriptor i) const {
  return {
    *this,
    i
  };
}

ImplicitGraph::edge_iterator ImplicitGraph::oend(VertexDescriptor i) const {
  return {
    *this,
    i + 1
  };
}

ImplicitGraph::in_group_edge_iterator::in_group_edge_iterator() = default;
ImplicitGraph::in_group_edge_iterator::in_group_edge_iterator(
  const ImplicitGraph::in_group_edge_iterator& other
) = default;
ImplicitGraph::in_group_edge_iterator::in_group_edge_iterator(
  ImplicitGraph::in_group_edge_iterator&& other
) = default;
ImplicitGraph::in_group_edge_iterator& ImplicitGraph::in_group_edge_iterator::operator = (
  ImplicitGraph::in_group_edge_iterator&& other
) = default;
ImplicitGraph::in_group_edge_iterator& ImplicitGraph::in_group_edge_iterator::operator = (
  const ImplicitGraph::in_group_edge_iterator& other
) = default;

ImplicitGraph::in_group_edge_iterator::in_group_edge_iterator(
  const ImplicitGraph& base,
  const VertexDescriptor& i
) : _basePtr{&base},
    _i {i},
    _b {0},
    _isLeft {isLeft(i)}
{
  auto a = internal(_i);

  if(a == 0) {
    ++_b;
  }

  unsigned N = _basePtr->_distances.outerSize();
  while(_b < N && !_basePtr->_distances(a, _b)) {
    ++_b;
  }
}

ImplicitGraph::in_group_edge_iterator::in_group_edge_iterator(
  const ImplicitGraph& base,
  const VertexDescriptor& i,
  bool
) : _basePtr{&base},
    _i {i},
    _b {static_cast<VertexDescriptor>(base._distances.outerSize())},
    _isLeft {isLeft(i)}
{
  if(internal(_i) == _b) {
    ++_b;
  }
}

} // namespace DistanceGeometry

} // namespace MoleculeManip
