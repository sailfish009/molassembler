/*!@file
 * @copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 * @brief Contains functions overarching different refinement implementations
 */

#ifndef INCLUDE_MOLASSEMBLER_REFINEMENT_META_H
#define INCLUDE_MOLASSEMBLER_REFINEMENT_META_H

#include "temple/Stringify.h"

namespace Scine {
namespace molassembler {
namespace DistanceGeometry {

/*template<typename FloatType, typename Derived>
auto flatten(const Eigen::MatrixBase<Derived>& positions) {
  using MapVectorType = Eigen::Matrix<typename Derived::Scalar, 1, Eigen::Dynamic>;
  return Eigen::Map<MapVectorType>(
    positions.derived().data(),
    positions.derived().cols() * positions.derived().rows()
  ).template cast<FloatType>().eval();
}*/


/**
 * @brief Decides whether the final structure from a refinement is acceptable
 *
 * A final structure is acceptable if
 * - All distance bounds are within 0.5 of either the lower or upper boundary
 * - All chiral constraints are within 0.5 of either the lower or upper boundary
 *
 * @param bounds The distance bounds
 * @param chiralConstraints All chiral constraints
 * @param dihedralConstraints All dihedral constraints
 * @param positions The final positions from a refinement
 *
 * @return Whether the final structure is acceptable
 */
template<class RefinementType, typename PositionType>
bool finalStructureAcceptable(
  const RefinementType& refinement,
  const DistanceBoundsMatrix& bounds,
  const PositionType& positions
) {
  struct FinalStructureAcceptableVisitor {
    const double deviationThreshold = 0.5;
    bool earlyExit = false;
    bool value = true;

    void distanceOverThreshold(AtomIndex /* i */, AtomIndex /* j */, double /* distance */) {
      earlyExit = true;
      value = false;
    }

    void chiralOverThreshold(const ChiralConstraint& /* chiral */, double /* volume */) {
      earlyExit = true;
      value = false;
    }

    void dihedralOverThreshold(const DihedralConstraint& /* dihedral */, double /* term */) {
      earlyExit = true;
      value = false;
    }
  };

  return refinement.visitUnfulfilledConstraints(
    bounds,
    positions,
    FinalStructureAcceptableVisitor {}
  );
}

/**
 * @brief Writes messages to the log explaining why a structure was rejected
 *
 * @see finalStructureAcceptable for criteria on acceptability
 *
 * A final structure is acceptable if
 * - All distance bounds are within 0.5 of either the lower or upper boundary
 * - All chiral constraints are within 0.5 of either the lower or upper boundary
 *
 * @param bounds The distance bounds
 * @param chiralConstraints All chiral constraints
 * @param dihedralConstraints All dihedral constraints
 * @param positions The final positions from a refinement
 */
template<class RefinementType, typename PositionType>
void explainAcceptanceFailure(
  const RefinementType& refinement,
  const DistanceBoundsMatrix& bounds,
  const PositionType& positions
) {
  struct AcceptanceFailureExplainer {
    const double deviationThreshold = 0.5;
    bool earlyExit = false;
    bool value = true;
    std::ostream& log = Log::log(Log::Particulars::DGStructureAcceptanceFailures);
    const DistanceBoundsMatrix& distanceBounds;

    AcceptanceFailureExplainer(const DistanceBoundsMatrix& passBounds)
      : distanceBounds(passBounds) {}

    void distanceOverThreshold(AtomIndex i, AtomIndex j, double distance) {
      log << "Distance constraint " << i << " - " << j << " : ["
        << distanceBounds.lowerBound(i, j) << ", " << distanceBounds.upperBound(i, j)
        << "] deviation over threshold, is : " << distance << "\n";
    }

    void chiralOverThreshold(const ChiralConstraint& chiral, double volume) {
      log << "Chiral constraint " << temple::stringifyContainer(chiral.sites) << " : ["
        << chiral.lower << ", " << chiral.upper
        << "] deviation over threshold, is : " << volume << "\n";
    }

    void dihedralOverThreshold(const DihedralConstraint& dihedral, double term) {
      log << "Dihedral constraint " << temple::stringifyContainer(dihedral.sites)
        << " : [" << dihedral.lower << ", " << dihedral.upper
        << "], term is : " << term << "\n";
    }
  };

  refinement.visitUnfulfilledConstraints(
    bounds,
    positions,
    AcceptanceFailureExplainer {bounds}
  );
}

/**
 * @brief Writes messages to the log explaining the final contributions to
 *   the error function
 *
 * @param bounds The distance bounds
 * @param chiralConstraints All chiral constraints
 * @param dihedralConstraints All dihedral constraints
 * @param positions The final positions from a refinement
 */
template<class RefinementType, typename PositionType>
void explainFinalContributions(
  const RefinementType& refinement,
  const DistanceBoundsMatrix& bounds,
  const PositionType& positions
) {
  struct FinalContributionsExplainer {
    const double deviationThreshold = 0;
    bool earlyExit = false;
    bool value = true;
    std::ostream& log = Log::log(Log::Particulars::DGFinalErrorContributions);
    const DistanceBoundsMatrix& distanceBounds;

    FinalContributionsExplainer(const DistanceBoundsMatrix& passBounds)
      : distanceBounds(passBounds) {}

    void distanceOverThreshold(AtomIndex i, AtomIndex j, double distance) {
      log << "Distance constraint " << i << " - " << j << " : ["
        << distanceBounds.lowerBound(i, j) << ", " << distanceBounds.upperBound(i, j)
        << "] is unsatisfied: " << distance << "\n";
    }

    void chiralOverThreshold(const ChiralConstraint& chiral, double volume) {
      log << "Chiral constraint " << temple::stringify(chiral.sites) << " : ["
        << chiral.lower << ", " << chiral.upper
        << "] is unsatisfied: " << volume << "\n";
    }

    void dihedralOverThreshold(const DihedralConstraint& dihedral, double term) {
      log << "Dihedral constraint " << temple::stringify(dihedral.sites)
        << " : [" << dihedral.lower << ", " << dihedral.upper
        << "] is unsatisfied, term is: " << term << "\n";
    }
  };

  refinement.visitUnfulfilledConstraints(
    bounds,
    positions,
    FinalContributionsExplainer {bounds}
  );
}

} // namespace DistanceGeometry
} // namespace molassembler
} // namespace Scine

#endif