#ifndef INCLUDE_LOG_H
#define INCLUDE_LOG_H

#include <set>
#include <iostream>

/*! @file
 *
 * Basic Logging functionality for debugging
 */

namespace Log {

namespace detail {
  class NullBuffer : public std::streambuf {
  public:
    int overflow(int c);
  };

  // Some objects we need
  extern NullBuffer nullBuffer;
  extern std::ostream nullStream;
}

//! Level of logging
enum class Level : unsigned {
  Trace,
  Debug,
  Info,
  Warning,
  Error,
  Fatal,
  None
};

//! Particular cases of special logging items that may or may not be desired
enum class Particulars {
  /*! In Molecule.cpp, where a fit of CNStereocenters against positions is
   * performed when a Molecule is read in, you can have numerical details of
   * the fit logged. Corresponding analysis scripts also exist.
   */
  CNStereocenterFit,
  /*! CNStereocenter's addSubstituent, removeSubstituent and propagate* functions
   */
  CNStereocenterStatePropagation,
  /*! In generateConformation.cpp, when chirality constraint prototypes are
   * fully determined into chirality constraints, emit some debug information
   */
  PrototypePropagatorDebugInfo,
  //! In DGRefinementProblem, chirality constraint numerical debug information
  DGRefinementChiralityNumericalDebugInfo,
  /*! In DGRefinementProblem, the callback function can reveal some information
   * on the current status of the optimization
   */
  DGRefinementProgress,
  //! Explain why a structure was not accepted
  DGStructureAcceptanceFailures,
  //! In generateConformation, show the Trees generated from the molecules
  gatherDGInformationTrees,
  //! in debugDistanceGeometry, progress information
  DGDebugInfo,
  //! Ranking debug information
  RankingTreeDebugInfo
};


// Log variables
extern Level level;
extern std::set<Particulars> particulars;

// Logging calls
std::ostream& log(const Level& decisionLevel);
std::ostream& log(const Particulars& particular);
bool isSet(Particulars particular);

} // namespace Log

#endif
