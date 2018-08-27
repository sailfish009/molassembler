#ifndef INCLUDE_MOLASSEMBLER_DISTANCE_GEOMETRY_H
#define INCLUDE_MOLASSEMBLER_DISTANCE_GEOMETRY_H

#include "molassembler/DistanceGeometry/ValueBounds.h"
#include "molassembler/Types.h"

#include <tuple>
#include <vector>

/*! @file
 *
 * Contains some central data class declarations and type definitions for the
 * entire Distance Geometry scheme.
 */

namespace molassembler {

//! Distance geometry-related classes and functions
namespace DistanceGeometry {

struct ChiralityConstraint {
  using AtomListType = std::vector<AtomIndex>;
  using LigandSequence = std::array<AtomListType, 4>;

  LigandSequence sites;
  double lower, upper;

  ChiralityConstraint(
    const LigandSequence& sites,
    const double lower,
    const double upper
  ) : sites(sites),
      lower(lower),
      upper(upper)
  {
    // Must be <= because flat targets have lower = upper = 0
    assert(lower <= upper);
  }
};

enum class Partiality {
  FourAtom,
  TenPercent,
  All
};

} // namespace DistanceGeometry

} // namespace molassembler

#endif
