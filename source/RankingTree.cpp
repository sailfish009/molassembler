#include "RankingTree.h"

#include "boost/graph/breadth_first_search.hpp"
#include "boost/algorithm/string/replace.hpp"
#include "template_magic/Boost.h"

#include "Delib/ElementInfo.h"
#include "MolGraphWriter.h"

#include <fstream>
#include <iostream>

namespace MoleculeManip {

class RankingTree::GraphvizWriter {
private:
  // Closures
  const RankingTree& _baseRef;
  const std::string _title;
  const std::set<TreeVertexIndex> _squareVertices;
  const std::set<TreeVertexIndex> _colorVertices;
  const std::set<TreeEdgeIndex> _colorEdges;

public:
  GraphvizWriter(
    const RankingTree& baseTree,
    const std::string& title = "",
    const std::set<TreeVertexIndex>& squareVertices = {},
    const std::set<TreeVertexIndex>& colorVertices = {},
    const std::set<TreeEdgeIndex>& colorEdges = {}
  ) : _baseRef(baseTree),
      _title(title),
      _squareVertices(squareVertices),
      _colorVertices(colorVertices),
      _colorEdges(colorEdges)
  {}

  void operator() (std::ostream& os) const {
    os << R"(  graph [fontname = "Arial"];)" << "\n"
      << R"(  node [fontname = "Arial", shape = "circle", style = "filled"];)" << "\n"
      << R"(  edge [fontname = "Arial"];)" << "\n"
      << R"(  labelloc="t"; label=")" << _title << R"(")" << ";\n";
  }

  void operator() (std::ostream& os, const TreeVertexIndex& vertexIndex) const {
    auto symbolString = Delib::ElementInfo::symbol(
      _baseRef._moleculeRef.getElementType(
        _baseRef._tree[vertexIndex].molIndex
      )
    );

    bool hasStereocenter = _baseRef._tree[vertexIndex].stereocenterOption.operator bool();

    os << "[" 
      << R"(label=")" << vertexIndex << "-" << symbolString 
      << _baseRef._tree[vertexIndex].molIndex << R"(")";

    // Node background coloring
    if(_colorVertices.count(vertexIndex) > 0) {
      os << R"(, fillcolor="tomato")";
    } else if(hasStereocenter) {
      os << R"(, fillcolor="steelblue")";
    } else if(MolGraphWriter::elementBGColorMap.count(symbolString) != 0u) {
      os << R"(, fillcolor=")" 
        << MolGraphWriter::elementBGColorMap.at(symbolString) << R"(")";
    } 

    // Font coloring
    if(_colorVertices.count(vertexIndex) > 0) {
      os << R"(, fontcolor="white")";
    } else if(MolGraphWriter::elementTextColorMap.count(symbolString) != 0u) {
      os << R"(, fontcolor=")" 
        << MolGraphWriter::elementTextColorMap.at(symbolString) << R"(")";
    } else if(hasStereocenter) {
      os << R"(, fontcolor="white")";
    }

    // Shape alterations
    if(_squareVertices.count(vertexIndex) > 0) {
      os << R"(, shape="square")";
    } else if(_baseRef._tree[vertexIndex].isDuplicate) {
      // Duplicate atoms get double-circle shape
      os << R"(, shape="doublecircle")";
    } else if(hasStereocenter) {
      os << R"(, shape="diamond")";
    }

    // Tooltip
    if(hasStereocenter) {
      os << R"(, tooltip=")" 
        << _baseRef._tree[vertexIndex].stereocenterOption.value().info()
        << R"(")";
    }

    // Make hydrogens smaller
    if(symbolString == "H") {
      os << ", fontsize=10, width=.6, fixedsize=true";
    }

    os << "]";
  }

  void operator() (std::ostream& os, const TreeEdgeIndex& edgeIndex) const {
    auto hasStereocenter = _baseRef._tree[edgeIndex].stereocenterOption.operator bool();

    os << "[";

    // Coloring
    if(_colorEdges.count(edgeIndex) > 0) {
      os << R"(color="tomato")";
    } else if(hasStereocenter) {
      os << R"(color="steelblue")";
    }
    
    // Edge width
    if(_colorEdges.count(edgeIndex) > 0 || hasStereocenter) {
      os << R"(, penwidth="2")";
    }

    // Tooltip
    if(hasStereocenter) {
      os << R"(, tooltip=")"
        << _baseRef._tree[edgeIndex].stereocenterOption.value().info()
        << R"(")";
    }

    os << "]";
  }
};

/* Sequence rule classes */
struct RankingTree::SequenceRuleOneVertexComparator {
private:
  const RankingTree& _base;

public:
  SequenceRuleOneVertexComparator(const RankingTree& base) : _base(base) {}

  /* NOTE: Since the multisets this is used in need a DESC ordering,
   * The arguments names have been swapped, effectively inverting the sorting
   */
  bool operator () (const TreeVertexIndex& b, const TreeVertexIndex& a) const {
    /* Cases:
     * - Neither is duplicate -> Compare Zs
     * - One is duplicate -> Non-duplicate is bigger
     * - Both are duplicate -> Compare distance to root
     */
    const bool& aDuplicate = _base._tree[a].isDuplicate;
    const bool& bDuplicate = _base._tree[b].isDuplicate;

    if(!aDuplicate && !bDuplicate) {
      // Casting elementType to unsigned basically gives Z
      return static_cast<unsigned>(
        _base._moleculeRef.getElementType(_base._tree[a].molIndex)
      ) < static_cast<unsigned>(
        _base._moleculeRef.getElementType(_base._tree[b].molIndex)
      );
    } 
    
    if(aDuplicate && bDuplicate) {
      // That vertex is smaller which is further from root (invert comparison)
      return _base._duplicateDepth(a) > _base._duplicateDepth(b);
    }

    if(aDuplicate && !bDuplicate) {
      return true;
    }

    // The case below is not needed, it falls together with equality
    /*if(!aDuplicate && bDuplicate) {
      return false;
    }*/

    return false;
  }
};

struct RankingTree::SequenceRuleThreeEdgeComparator {
private:
  const RankingTree& _base;

public:
  SequenceRuleThreeEdgeComparator(const RankingTree& base) : _base(base) {}

  bool operator () (const TreeEdgeIndex& a, const TreeEdgeIndex& b) const {
    /* Cases:
     * - Neither is instantiated -> neither can be smaller
     * - One is instantiated -> that one is bigger
     * - Both are instantiated -> Compare sequentially:
     *   - Number of possible assignments (non-stereogenic is smaller)
     *   - If assigned or not (unassigned is smaller)
     *   - Assignment value (Z > E)
     *
     * NOTE: Since the multisets this is used in need a DESC ordering,
     * all comparisons below are reversed.
     */

    auto EZOptionalA = _base._tree[a].stereocenterOption;
    auto EZOptionalB = _base._tree[b].stereocenterOption;

    if(!EZOptionalA && !EZOptionalB) {
      /* This does not invalidate the program, it just means that all
       * uninstantiated EZStereocenters are equal, and no relative ordering
       * is provided.
       */
      return false;
    }

    if(EZOptionalA && !EZOptionalB) {
      return true;
    }

    if(!EZOptionalA && EZOptionalB) {
      return false;
    }

    // Now we know both actually have a stereocenterPtr
    const auto& EZStereocenterA = EZOptionalA.value();
    const auto& EZStereocenterB = EZOptionalB.value();

    // Reverse everything below for descending sorting
    return TemplateMagic::componentSmaller(
      EZStereocenterB.numAssignments(),
      EZStereocenterA.numAssignments()
    ).value_or(
      /* Mixed optional comparison (includes comparison of assignment value if
       * assigned)
       */
      EZStereocenterB.assigned() < EZStereocenterA.assigned()
    );
  }
};

struct RankingTree::SequenceRuleFourVariantComparator {
public:
  using CompareType = boost::variant<TreeVertexIndex, TreeEdgeIndex>;

  class VariantComparisonVisitor : boost::static_visitor<bool> {
  private:
    const TreeGraphType& _treeRef;

  public:
    explicit VariantComparisonVisitor(
      const SequenceRuleFourVariantComparator& comparatorBase
    ) : _treeRef(comparatorBase._base._tree) {}

    /* Surprisingly, the code for edge and vertex indices is completely
     * identical, so we can abstract over the types.
     *
     * NOTE: Since we desire the inverted ordering sequence, this comparison
     * is implemented as normal, but swapping the parameters a and b.
     */
    template<typename T>
    bool operator() (const T& b, const T& a) const {
      const auto& aOption = _treeRef[a].stereocenterOption;
      const auto& bOption = _treeRef[b].stereocenterOption;

      // Uninstantiated stereocenters always compare false
      if(!aOption && !bOption) {
        return false;
      }

      // Instantiated is less than uninstantiated
      if(aOption && !bOption) {
        return false;
      }

      if(!aOption && bOption) {
        return true;
      }

      // Now we know both actually have an instantiated stereocenter
      const auto& StereocenterA = aOption.value();
      const auto& StereocenterB = bOption.value();

      /* We only want to know whether they differ in stereogenicity, i.e.
       * whether one is stereogenic while the other isn't. In effect, this
       * means comparing whether they have more than one assignment:
       *
       * Cases:
       * - Neither is stereogenic -> false
       * - Both are stereogenic -> false (neither bigger than other)
       * - A isn't stereogenic, B is
       *   false < true == 0 < 1 == true
       *   (So A < B is true, meaning stereogenic < non-stereogenic, leading
       *   to the desired DESC ordering)
       *
       * This is valid for both CN and EZ types of stereocenters
       */
      return (
        (StereocenterA.numAssignments() > 1) 
        < (StereocenterB.numAssignments() > 1)
      );
    }

    // For different types
    template<typename T, typename U>
    bool operator() (const T&, const U&) const {
      return false;
    }
  };

private:
  const RankingTree& _base;
  VariantComparisonVisitor _variantComparator;

public:
  SequenceRuleFourVariantComparator(const RankingTree& base) 
    : _base(base),
      _variantComparator {*this}
  {}

  bool operator () (const CompareType& a, const CompareType& b) const {
    return boost::apply_visitor(_variantComparator, a, b);
  }
};

struct RankingTree::SequenceRuleFiveVariantComparator {
public:
  using CompareType = boost::variant<TreeVertexIndex, TreeEdgeIndex>;

  class VariantComparisonVisitor : boost::static_visitor<bool> {
  private:
    const TreeGraphType& _treeRef;

  public:
    explicit VariantComparisonVisitor(
      const SequenceRuleFiveVariantComparator& base
    ) : _treeRef(base._base._tree) {}

    /* The code for edge and vertex indices is completely identical, so we
     * can abstract over the types.
     *
     * NOTE: Since we desire the inverted ordering sequence, this comparison
     * is implemented as normal, but swapping the parameters a and b.
     */
    template<typename T>
    bool operator() (const T& b, const T& a) const {
      const auto& aOption = _treeRef[a].stereocenterOption;
      const auto& bOption = _treeRef[b].stereocenterOption;

      // Uninstantiated stereocenters always compare false
      if(!aOption && !bOption) {
        return false;
      }

      // Instantiated is less than uninstantiated
      if(aOption && !bOption) {
        return false;
      }

      if(!aOption && bOption) {
        return true;
      }

      // Now we know both actually have an instantiated stereocenter
      const auto& StereocenterA = aOption.value();
      const auto& StereocenterB = bOption.value();

      // Need to compare using the number of assignments first
      if(StereocenterA.numAssignments() < StereocenterB.numAssignments()) {
        return true;
      }

      if(StereocenterB.numAssignments() < StereocenterA.numAssignments()) {
        return false;
      }

      /* In our context, sequence rule 5 directly compares the assignment
       * of the assigned stereocenters.
       */
      return StereocenterA.assigned() < StereocenterB.assigned();
    }

    // For different types
    template<typename T, typename U>
    bool operator() (const T&, const U&) const {
      return false;
    }
  };

private:
  const RankingTree& _base;
  VariantComparisonVisitor _variantComparator;

public:
  SequenceRuleFiveVariantComparator(const RankingTree& base) 
    : _base(base),
      _variantComparator {*this}
  {}

  bool operator () (const CompareType& a, const CompareType& b) const {
    return boost::apply_visitor(_variantComparator, a, b);
  }
};

/* Variant visitor helper classes */
class RankingTree::VariantHasInstantiatedStereocenter : boost::static_visitor<bool> {
private:
  const RankingTree& _baseRef;

public:
  explicit VariantHasInstantiatedStereocenter(
    const RankingTree& base
  ) : _baseRef(base) {}

  //! Check if the variant is an instantiated stereocenter
  template<typename T>
  bool operator() (const T& a) const {
    const auto& stereocenterOption = _baseRef._tree[a].stereocenterOption;
    return stereocenterOption.operator bool();
  }
};

class RankingTree::VariantDepth : boost::static_visitor<unsigned> {
private:
  const RankingTree& _baseRef;

public:
  explicit VariantDepth(
    const RankingTree& base
  ) : _baseRef(base) {}

  //! Returns the depth of the vertex or edge
  template<typename T>
  unsigned operator() (const T& a) const {
    return _baseRef._mixedDepth(a);
  }
};

class RankingTree::VariantSourceNode : boost::static_visitor<TreeVertexIndex> {
private:
  const RankingTree& _baseRef;

public:
  explicit VariantSourceNode(
    const RankingTree& base
  ) : _baseRef(base) {}

  //! Returns the depth of the vertex or edge
  TreeVertexIndex operator() (const TreeVertexIndex& a) const {
    return a;
  }

  TreeVertexIndex operator() (const TreeEdgeIndex& a) const {
    return boost::source(a, _baseRef._tree);
  }
};

class RankingTree::VariantStereocenterStringRepresentation : boost::static_visitor<std::string> {
private:
  const RankingTree& _baseRef;

public:
  explicit VariantStereocenterStringRepresentation(
    const RankingTree& base
  ) : _baseRef(base) {}

  //! Returns a string representation of the *type* of the stereocenter
  template<typename T>
  std::string operator() (const T& a) const {
    const auto& aOption = _baseRef._tree[a].stereocenterOption;
    if(aOption) {
      return aOption.value().rankInfo();
    }

    return "";
  }
};

class RankingTree::VariantLikePair : boost::static_visitor<bool> {
private:
  const RankingTree& _baseRef;

public:
  explicit VariantLikePair(const RankingTree& base) : _baseRef(base) {}

  //! Returns a string representation of the *type* of the stereocenter
  template<typename T, typename U>
  bool operator() (const T& a, const U& b) const {
    const auto& aOption = _baseRef._tree[a].stereocenterOption;
    const auto& bOption = _baseRef._tree[b].stereocenterOption;

    return (
      aOption 
      && bOption
      && aOption.value().numAssignments() == bOption.value().numAssignments()
      && aOption.value().assigned() == bOption.value().assigned()
    );
  }
};

/*!
 * This function ranks the direct substituents of the atom it is instantiated
 * upon by the sequential application of the 2013 IUPAC Blue Book sequence
 * rules. They are somewhat adapted since the priority of asymmetric centers
 * of higher (and also lower) symmetry must also be considered because
 * transition metal chemistry is also included in this library.
 *
 * It returns a sorted vector of vectors, in which every sub-vector
 * represents a set of equal-priority substituents. The sorting is ascending,
 * meaning from lowest priority to highest priority.
 */
void RankingTree::_applySequenceRules(
  const boost::optional<Delib::PositionCollection>& positionsOption
) {
  /* Sequence rule 2
   * - A node with higher atomic mass precedes ones with lower atomic mass
   *
   * NOTE: We skip this sequence rule for two reasons
   * - There is currently no way to represent isotopes in the library
   * - This rule may come to bear when disconnecting heteroaromatic cycles,
   *   where all aromatic atoms are given a duplicate atom with a mass equal
   *   to the average of what it would have if the double bonds were located
   *   at each of the possible positions. Although differentiating these
   *   cases would be good, there is currently no code to detect aromaticity
   *   nor permute all possible arrangements of double bonds in their
   *   subgraphs.
   */


  /* Sequence rule 3: double bond configurations
   * - Z > E > unassigned > non-stereogenic 
   *   (seqCis > seqTrans > non-stereogenic)
   */
  /* Before we can compare using sequence rule 3, we have to instantiate all
   * auxiliary descriptors, meaning we have to instantiate EZStereocenters in
   * all double bond positions and CNStereocenters in all candidate
   * positions. 
   *
   * In both cases, we have to rank non-duplicate adjacent vertices according
   * to the all of the same sequence rules, with the difference that graph
   * traversal isn't straightforward top-down, but proceeds in all unexplored
   * directions. 
   *
   * We want to proceed from the outer parts of the tree inwards, so that
   * branches that are forced to consider later sequence rules have their
   * required stereocenters instantiated already.
   */
  // Cleanly divide the tree into depths by its edges
  std::vector<
    std::set<TreeEdgeIndex>
  > byDepth;

  /* A much-needed variable which was often re-declared within the local
   * scopes of every sequence rule. This allows reuse.
   */
  auto undecidedSets = _branchOrderingHelper.getUndecidedSets();

  { // Populate byDepth from active branches only
    std::set<TreeEdgeIndex> rootEdges;

    for(const auto& undecidedSet : undecidedSets) {
      for(const auto& undecidedBranch : undecidedSet) {
        rootEdges.insert(
          boost::edge(0, undecidedBranch, _tree).first
        );
      }
    }

    byDepth.emplace_back(
      std::move(rootEdges)
    );
  }

  std::set<TreeEdgeIndex> nextChildren;

  for(const auto& edge : byDepth.back()) {
    auto outIterPair = boost::out_edges(
      boost::target(edge, _tree), _tree
    );

    while(outIterPair.first != outIterPair.second) {
      nextChildren.insert(*outIterPair.first);

      ++outIterPair.first;
    }
  }

  while(nextChildren.size() != 0) {
    byDepth.emplace_back(
      std::move(nextChildren)
    );

    nextChildren.clear();

    for(const auto& edge : byDepth.back()) {
      auto outIterPair = boost::out_edges(
        boost::target(edge, _tree), _tree
      );

      while(outIterPair.first != outIterPair.second) {
        nextChildren.insert(*outIterPair.first);

        ++outIterPair.first;
      }
    }
  }

#ifndef NDEBUG
  auto collectHandledEdges = [&](
    const typename decltype(byDepth)::reverse_iterator& rIter
  ) -> std::set<TreeEdgeIndex> {
    std::set<TreeEdgeIndex> edgeIndices;

    for(auto it = byDepth.rbegin(); it != rIter; ++it) {
      const auto& currentEdges = *it;

      for(const auto& edge : currentEdges) {
        edgeIndices.insert(edge);
      }
    }

    return edgeIndices;
  };
#endif

  // Remember if you found something to instantiate or not
  bool foundStereocenters = false;

  // Process the tree, from the bottom up
  for(auto it = byDepth.rbegin(); it != byDepth.rend(); ++it) {
    const auto& currentEdges = *it;

    for(const auto& edge: currentEdges) {
      auto sourceIndex = boost::source(edge, _tree);

      if(_doubleBondEdges.count(edge)) {
        // Instantiate an EZStereocenter here!
        
        /* TODO ensure that there is at least one non-duplicate substituent
         * on each end of the edge
         */
        auto targetIndex = boost::target(edge, _tree);

        auto sourceIndicesToRank = _auxiliaryAdjacentsToRank(
          sourceIndex, 
          {targetIndex}
        );

        auto targetIndicesToRank = _auxiliaryAdjacentsToRank(
          targetIndex,
          {sourceIndex}
        );

        if(
          1 <= sourceIndicesToRank.size() && sourceIndicesToRank.size() <= 2
          && 1 <= targetIndicesToRank.size() && targetIndicesToRank.size() <= 2
        ) {
          RankingInformation sourceRanking, targetRanking;

          // Get ranking for the edge substituents
          sourceRanking.sortedSubstituents = _mapToAtomIndices(
            _auxiliaryApplySequenceRules(
              sourceIndex,
              sourceIndicesToRank
            )
          );

          targetRanking.sortedSubstituents = _mapToAtomIndices(
            _auxiliaryApplySequenceRules(
              targetIndex,
              targetIndicesToRank
            )
          );

          /* NOTE: There is no need to collect linking information since we
           * are in an acyclic digraph, and any cycles in the original molecule
           * are no longer present.
           */

          const AtomIndexType molSourceIndex = _tree[sourceIndex].molIndex;
          const AtomIndexType molTargetIndex = _tree[targetIndex].molIndex;

          auto newStereocenter = Stereocenters::EZStereocenter {
            molSourceIndex,
            sourceRanking,
            molTargetIndex,
            targetRanking
          };

          // Try to assign the new stereocenter
          if(newStereocenter.numAssignments() > 1) {
            if(positionsOption) {
              // Fit from positions
              newStereocenter.fit(
                positionsOption.value()
              );
            } else {
              /* Need to get chiral information from the molecule (if present).
               * Have to be careful, any stereocenters on the same atom may have
               * different symmetries and different ranking for the same
               * substituents
               */
              if(_moleculeRef.getStereocenterList().involving(molSourceIndex)) {
                const auto& stereocenterPtr = _moleculeRef.getStereocenterList().at(
                  molSourceIndex
                );

                if(
                  stereocenterPtr->type() == Stereocenters::Type::EZStereocenter
                  && stereocenterPtr->involvedAtoms() == std::set<AtomIndexType> {
                    molSourceIndex,
                    molTargetIndex
                  } && stereocenterPtr->numAssignments() == 2
                ) {
                  // Take the assignment from that stereocenter
                  newStereocenter.assign(
                    stereocenterPtr->assigned()
                  );
                }
              }
            }
          }

          _tree[edge].stereocenterOption = newStereocenter;

          // Mark that we instantiated something
          foundStereocenters = true;

#ifndef NDEBUG
          if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
            _writeGraphvizFiles({
              _adaptMolGraph(_moleculeRef.dumpGraphviz()),
              dumpGraphviz("Sequence rule 3 preparation", {0}, {}, collectHandledEdges(it))
            });
          }
#endif
        }
      }

      if(
        !_tree[edge].stereocenterOption // No EZStereocenter on this edge
        && !_tree[sourceIndex].stereocenterOption // No CNStereocenter
        && _nonDuplicateDegree(sourceIndex) >= 3 // Min. degree for chirality
        && sourceIndex != 0 // not root, no CNStereocenters needed there!
      ) {
        /* TODO reduce the effort here by counting the number of adjacent
         * (terminal!) hydrogens and comparing with a table of symmetries vs
         * #hydrogens 
         * -> max # assignments
         *
         * In case only one assignment is possible, there is no reason to rank
         * the substituents
         */
        // Instantiate a CNStereocenter here!
        RankingInformation centerRanking;

        centerRanking.sortedSubstituents = _mapToAtomIndices(
          _auxiliaryApplySequenceRules(
            sourceIndex,
            _auxiliaryAdjacentsToRank(sourceIndex, {})
          )
        );

        const AtomIndexType molSourceIndex = _tree[sourceIndex].molIndex;

        auto newStereocenter = Stereocenters::CNStereocenter {
          _moleculeRef.determineLocalGeometry(molSourceIndex),
          molSourceIndex,
          centerRanking
        };

        if(newStereocenter.numAssignments() > 1) {
          if(positionsOption) {
            newStereocenter.fit(
              positionsOption.value()
            );
          } else { // Try to get an assignment from the molecule
            if(_moleculeRef.getStereocenterList().involving(molSourceIndex)) {
              const auto& stereocenterPtr = _moleculeRef.getStereocenterList().at(
                molSourceIndex
              );

              /* TODO
               * consider what to do in cases in which molecule number of
               * assignments is fewer finding the equivalent case by means of
               * rotations should be possible
               *
               * widening cases are not assignable
               */
              if(
                stereocenterPtr->type() == Stereocenters::Type::CNStereocenter
                && stereocenterPtr->involvedAtoms() == std::set<AtomIndexType> {
                  molSourceIndex
                } && stereocenterPtr->numAssignments() == newStereocenter.numAssignments()
              ) {
                newStereocenter.assign(
                  stereocenterPtr->assigned()
                );
              }
            }
          }
        }

        _tree[sourceIndex].stereocenterOption = newStereocenter;

        // Mark that we instantiated something
        foundStereocenters = true;
        
#ifndef NDEBUG
        if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
          _writeGraphvizFiles({
            _adaptMolGraph(_moleculeRef.dumpGraphviz()),
            dumpGraphviz("Sequence rule 3 preparation", {0}, {}, collectHandledEdges(it))
          });
        }
#endif
      }

    }
  }

  // Was anything instantiated? If not, we can skip rules 3 through 5.
  if(!foundStereocenters) {
    return;
  }

  // Apply sequence rule 3
  _runBFS<
    3, // Sequence rule 3
    true, // BFS downwards only
    true, // Insert edges
    false, // Insert vertices
    TreeEdgeIndex, // Multiset value type
    SequenceRuleThreeEdgeComparator // Multiset comparator type
  >(
    0, // Source index is root
    _branchOrderingHelper
  );

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "Sets post sequence rule 3: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        _branchOrderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

  // Was sequence rule 3 enough?
  if(_branchOrderingHelper.isTotallyOrdered()) {
    return;
  }

  /* Sequence rule 4: Other configurations (RS, MP, EZ)
   * - stereogenic > pseudostereogenic > non-stereogenic
   * - like pairs of descriptors over unlike pairs of descriptors
   *   set 1: {R, M, Z}, set 2: {S, P, E}, like within a set, unlike crossover
   *
   *   Procedure:
   *
   *   - Create a set of all stereogenic groups within the branches to rank
   *   - Establish relative rank by application of sequence rules. 
   *
   *     Can probably use _auxiliaryApplySequenceRules recursively at the first
   *     junction between competing stereocenters on the respective branch
   *     indices to find out which precedes which, provided both are at the
   *     same depth from root.
   *
   *     (WARNING: This may be tricky!)
   *
   *   - Choose a representative stereodescriptor for each branch according to
   *     the following rules:
   *
   *     - The solitarily highest ranked stereogenic group
   *     - The stereodescriptor occurring more often than all others in 
   *       the set of stereodescriptors
   *     - All most-often-occurring stereodescriptors
   *
   *   - Precedence:
   *     - The branch with fewer representative stereodescriptors has priority
   *     - Go through both lists of ranked stereocenters simultaneously,
   *       proceeding from highest rank to lowest, pairing each occurring
   *       descriptor with the reference descriptor.
   *
   *       like pairs precede unlike pairs of stereodescriptors
   *
   *
   * - (Pseudoasymmetries) r precedes s and m precedes p
   */

  { // Sequence rule 4 local scope
    /* First part of rule: A regular omni-directional BFS, inserting both
     * edges and node indices into the multiset! Perhaps avoiding the
     * insertion of uninstantiated edges and vertices entirely?
     */

    using VariantType = boost::variant<TreeVertexIndex, TreeEdgeIndex>;

    std::map<
      TreeVertexIndex,
      std::multiset<VariantType, SequenceRuleFourVariantComparator>
    > comparisonSets;

    std::map<
      TreeVertexIndex,
      std::vector<TreeVertexIndex>
    > seeds;

    undecidedSets = _branchOrderingHelper.getUndecidedSets();

    // Initialize the BFS state
    for(const auto& undecidedSet : undecidedSets) {
      for(const auto& undecidedBranch : undecidedSet) {
        comparisonSets.emplace(
          undecidedBranch,
          *this
        );

        comparisonSets.at(undecidedBranch).emplace(undecidedBranch);
        comparisonSets.at(undecidedBranch).emplace(
          boost::edge(0, undecidedBranch, _tree).first
        );

        seeds[undecidedBranch] = {undecidedBranch};
      }
    }

    /* First comparison (undecided sets are up-to-date from previous sequence
     * rule)
     */
    _compareBFSSets(comparisonSets, undecidedSets, _branchOrderingHelper);

    // Recalculate undecided sets
    undecidedSets = _branchOrderingHelper.getUndecidedSets();

#ifndef NDEBUG
    if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
      _writeGraphvizFiles({
        _adaptMolGraph(_moleculeRef.dumpGraphviz()),
        dumpGraphviz("Sequence rule 4A", {0}, _collectSeeds(seeds, undecidedSets)),
        _makeGraph("Sequence rule 4A multisets", 0, comparisonSets, undecidedSets),
        _branchOrderingHelper.dumpGraphviz()
      });
    }
#endif

    while(undecidedSets.size() > 0 && _relevantSeeds(seeds, undecidedSets)) {
      // Perform a full BFS Step on all undecided set seeds
      for(const auto& undecidedSet : undecidedSets) {
        for(const auto& undecidedTreeIndex : undecidedSet) {
          auto& branchSeeds = seeds[undecidedTreeIndex];

          std::vector<TreeVertexIndex> newSeeds;

          for(const auto& seed: branchSeeds) {
            for( // Out-edges
              auto outIterPair = boost::out_edges(seed, _tree);
              outIterPair.first != outIterPair.second;
              ++outIterPair.first
            ) {
              const auto& outEdge = *outIterPair.first;

              auto edgeTarget = boost::target(outEdge, _tree);

              if(
                comparisonSets.at(undecidedTreeIndex).count(edgeTarget) == 0
                && !_tree[edgeTarget].isDuplicate
              ) {
                comparisonSets.at(undecidedTreeIndex).emplace(edgeTarget);
                comparisonSets.at(undecidedTreeIndex).emplace(outEdge);

                newSeeds.push_back(edgeTarget);
              }
            }
          }

          // Overwrite the seeds
          branchSeeds = std::move(newSeeds);
        }
      }

      // Make comparisons in all undecided sets
      _compareBFSSets(comparisonSets, undecidedSets, _branchOrderingHelper);

      // Recalculate the undecided sets
      undecidedSets = _branchOrderingHelper.getUndecidedSets();

#ifndef NDEBUG
      if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
        _writeGraphvizFiles({
          _adaptMolGraph(_moleculeRef.dumpGraphviz()),
          dumpGraphviz("Sequence rule 4A", {0}, _collectSeeds(seeds, undecidedSets)),
          _makeGraph("Sequence rule 4A multisets", 0, comparisonSets, undecidedSets),
          _branchOrderingHelper.dumpGraphviz()
        });
      }
#endif
    }

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "Sets post sequence rule 4A: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        _branchOrderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

    // Is Sequence Rule 4, part A enough?
    if(_branchOrderingHelper.isTotallyOrdered()) {
      return;
    }

    /* Second part: Choosing representational stereodescriptors for all
     * branches and pairing in sequence of priority for branches.
     */
    std::map<
      TreeVertexIndex,
      std::set<VariantType>
    > stereocenterMap;

    VariantHasInstantiatedStereocenter isInstantiatedChecker {*this};

    // Copy all those variants from part A that are actually instantiated
    for(const auto& undecidedSet : _branchOrderingHelper.getUndecidedSets()) {
      for(const auto& undecidedBranch : undecidedSet) {
        stereocenterMap[undecidedBranch] = {};

        for(const auto& variantValue : comparisonSets.at(undecidedBranch)) {
          if(boost::apply_visitor(isInstantiatedChecker, variantValue)) {
            stereocenterMap.at(undecidedBranch).insert(variantValue);
          }
        }
      }
    }

    std::map<
      TreeVertexIndex,
      OrderDiscoveryHelper<VariantType>
    > relativeOrders;

    VariantDepth depthFetcher {*this};
    VariantSourceNode sourceNodeFetcher {*this};
    VariantStereocenterStringRepresentation stringRepFetcher {*this};

    std::map<
      TreeVertexIndex,
      std::set<VariantType>
    > representativeStereodescriptors;

    for(const auto& mapIterPair : stereocenterMap) {
      const auto& branchIndex = mapIterPair.first;
      const auto& variantSet = mapIterPair.second;

      // Instantiate the order discovery helpers
      relativeOrders.emplace(
        branchIndex,
        variantSet
      );

      // Compare variants in a branch based on mixed depth
      TemplateMagic::forAllPairs(
        variantSet,
        [&](const auto& a, const auto& b) {
          auto aDepth = boost::apply_visitor(depthFetcher, a);
          auto bDepth = boost::apply_visitor(depthFetcher, b);

          if(aDepth < bDepth) {
            relativeOrders.at(branchIndex).addLessThanRelationship(a, b);
          } else if(bDepth < aDepth) {
            relativeOrders.at(branchIndex).addLessThanRelationship(b, a);
          }
        }
      );

      /* Try to resolve undecided sets via ranking downwards at junction of
       * pairs
       */
      for(
        const auto& undecidedSet : 
        relativeOrders.at(branchIndex).getUndecidedSets()
      ) {
        TemplateMagic::forAllPairs(
          undecidedSet,
          [&](const auto& a, const auto& b) {
            auto junctionInfo = _junction(
              boost::apply_visitor(sourceNodeFetcher, a),
              boost::apply_visitor(sourceNodeFetcher, b)
            );

            auto aJunctionChild = junctionInfo.firstPath.back();
            auto bJunctionChild = junctionInfo.secondPath.back();

            /* Do not use _auxiliaryApplySequenceRules for root-level ranking,
             * it should only establish differences within branches
             */
            if(junctionInfo.junction != 0) {
              auto relativeRank = _auxiliaryApplySequenceRules(
                junctionInfo.junction,
                {aJunctionChild, bJunctionChild}
              );

              /* relativeRank can only have sizes 1 or 2, where size 1 means
               * that no difference was found
               *
               * TODO this may be an accidental inversion of priority
               */
              if(relativeRank.size() == 2) {
                if(relativeRank.front().front() == aJunctionChild) {
                  relativeOrders.at(branchIndex).addLessThanRelationship(a, b);
                } else {
                  relativeOrders.at(branchIndex).addLessThanRelationship(b, a);
                }
              }
            }
          }
        );
      }

      // Pick the representative stereodescriptor for each branch!
      auto undecidedStereocenterSets = relativeOrders.at(branchIndex).getUndecidedSets();
      if(undecidedStereocenterSets.size() == 0) {
        // 0 - No stereocenters in the branch, so none are representative
        representativeStereodescriptors[branchIndex] = {};
      } else if(undecidedStereocenterSets.back().size() == 1) {
        // 1 - The solitary highest ranked stereogenic group
        representativeStereodescriptors[branchIndex] = {
          undecidedStereocenterSets.back().front()
        };
      } else {
        // 2 - The stereodescriptor(s) occurring more often than all others
        auto groupedByStringRep = TemplateMagic::groupByMapping(
          stereocenterMap.at(branchIndex),
          [&](const auto& variantType) -> std::string {
            return boost::apply_visitor(stringRepFetcher, variantType);
          }
        );

        auto maxSize = TemplateMagic::accumulate(
          groupedByStringRep,
          0u,
          [](const unsigned& maxSize, const auto& stringGroup) -> unsigned {
            if(stringGroup.size() > maxSize) {
              return stringGroup.size();
            }

            return maxSize;
          }
        );

        representativeStereodescriptors[branchIndex] = {};

        // All stereocenter string groups with maximum size are representative
        for(const auto& stringGroup : groupedByStringRep) {
          if(stringGroup.size() == maxSize) {
            representativeStereodescriptors.at(branchIndex).insert(
              stringGroup.begin(),
              stringGroup.end()
            );
          }
        }
      }
    }

    auto undecidedBranchSets = _branchOrderingHelper.getUndecidedSets();

    VariantLikePair variantLikeComparator {*this};

    for(const auto& undecidedSet : undecidedBranchSets) {
      TemplateMagic::forAllPairs(
        undecidedSet,
        [&](const auto& branchA, const auto& branchB) {
          // Precedence via amount of representative stereodescriptors
          if(
            representativeStereodescriptors.at(branchA).size() 
            < representativeStereodescriptors.at(branchB).size() 
          ) {
            _branchOrderingHelper.addLessThanRelationship(branchA, branchB);
          } else if(
            representativeStereodescriptors.at(branchB).size() 
            < representativeStereodescriptors.at(branchA).size() 
          ) {
            _branchOrderingHelper.addLessThanRelationship(branchB, branchA);
          } else {
            // Compare by sequential pairing
            /* What is considered a like pair?
             *
             * IUPAC says any pairs within the sets {R, M, Z} or {S, P, E}
             * are considered like-pairs, such as RR, RZ, SP, or ES. This
             * is a little more difficult for us, since any more expansive
             * stereodescriptors where choice is non-binary are not reducible
             * to two sets. Retaining the correct behavior for tetrahedral
             * stereodescriptors is the guiding principle here.
             *
             * So, we require that CN-tetrahedral-2-0 and EZ-2-0 are
             * considered like and so are CN-tetrahedral-2-1 and EZ-2-1. 
             *
             * We go about this by deciding that two stereodescriptors compare
             * like if they have the same number of assignments and have the
             * same assignment index.
             *
             * This is convenient, but means we have to ensure assignments
             * are canonical, i.e. a correspondence with R/S makes sense.
             * TODO
             */

            auto branchAOrders = relativeOrders.at(branchA).getSets();
            auto branchBOrders = relativeOrders.at(branchB).getSets();

            // Go through the ranked stereocenter groups in DESC order!
            auto branchAStereocenterGroupIter = branchAOrders.rbegin();
            auto branchBStereocenterGroupIter = branchBOrders.rbegin();

            while(
              branchAStereocenterGroupIter != branchAOrders.rend()
              && branchBStereocenterGroupIter != branchBOrders.rend()
            ) {
              unsigned ABranchLikePairs = 0, BBranchLikePairs = 0;

              // Count A-branch like pairs
              TemplateMagic::forAllPairs(
                *branchAStereocenterGroupIter,
                representativeStereodescriptors.at(branchA),
                [&](const auto& variantA, const auto& variantB) {
                  if(
                    boost::apply_visitor(
                      variantLikeComparator,
                      variantA,
                      variantB
                    )
                  ) {
                    ABranchLikePairs += 1;
                  }
                }
              );

              // Count B-branch like pairs
              TemplateMagic::forAllPairs(
                *branchBStereocenterGroupIter,
                representativeStereodescriptors.at(branchB),
                [&](const auto& variantA, const auto& variantB) {
                  if(
                    boost::apply_visitor(
                      variantLikeComparator,
                      variantA,
                      variantB
                    )
                  ) {
                    BBranchLikePairs += 1;
                  }
                }
              );

              if(ABranchLikePairs < BBranchLikePairs) {
                _branchOrderingHelper.addLessThanRelationship(branchA, branchB);
                break;
              } else if(BBranchLikePairs < ABranchLikePairs) {
                _branchOrderingHelper.addLessThanRelationship(branchB, branchA);
                break;
              }

              ++branchAStereocenterGroupIter;
              ++branchBStereocenterGroupIter;
            }
          }
        }
      );
    }

#ifndef NDEBUG
    Log::log(Log::Particulars::RankingTreeDebugInfo)
      << "Sets post sequence rule 4B: {" 
      << TemplateMagic::condenseIterable(
        TemplateMagic::map(
          _branchOrderingHelper.getSets(),
          [](const auto& indexSet) -> std::string {
            return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
          }
        )
      ) << "}\n";
#endif

    // Is Sequence Rule 4, part B enough?
    if(_branchOrderingHelper.isTotallyOrdered()) {
      return;
    }

    // TODO 4C Pseudo-asymmetries, possibly via already-gathered information?

  } // End sequence rule 4 local scope

  // Was sequence rule 4 enough?
  if(_branchOrderingHelper.isTotallyOrdered()) {
    return;
  }

  /* Sequence rule 5: 
   * - Atom or group with {R, M, Z} precedes {S, P, E}
   */
  _runBFS<
    5, // Sequence rule 5
    true, // BFS downwards only
    true, // Insert edges
    true, // Insert vertices
    boost::variant<TreeVertexIndex, TreeEdgeIndex>, // MultisetValueType
    SequenceRuleFiveVariantComparator // Multiset comparator type
  >(
    0, // Source index is root
    _branchOrderingHelper
  );

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "Sets post sequence rule 5: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        _branchOrderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

  // Exhausted sequence rules, anything undecided is now equal
}

std::vector<
  std::vector<RankingTree::TreeVertexIndex>
> RankingTree::_auxiliaryApplySequenceRules(
  const RankingTree::TreeVertexIndex& sourceIndex,
  const std::set<RankingTree::TreeVertexIndex>& adjacentsToRank
) const {
  /* Sequence rule 1 */
  OrderDiscoveryHelper<TreeVertexIndex> orderingHelper {adjacentsToRank};

#ifndef NEDEBUG
Log::log(Log::Particulars::RankingTreeDebugInfo)
  << "  Preparing sequence rule 3, ranking substituents of tree index " 
  << sourceIndex 
  <<  " {" 
  << TemplateMagic::condenseIterable(
    TemplateMagic::map(
      orderingHelper.getSets(),
      [](const auto& indexSet) -> std::string {
        return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
      }
    )
  ) << "}\n";
#endif


  auto undecidedSets = orderingHelper.getUndecidedSets();

  _runBFS<
    1, // Sequence rule 1
    false, // BFS downwards only
    false, // Insert edges
    true, // Insert vertices
    TreeVertexIndex, // Multiset value type
    SequenceRuleOneVertexComparator // Multiset comparator type
  >(
    sourceIndex,
    orderingHelper
  );

#ifndef NDEBUG
Log::log(Log::Particulars::RankingTreeDebugInfo)
  << "  Sets post sequence rule 1: {" 
  << TemplateMagic::condenseIterable(
    TemplateMagic::map(
      orderingHelper.getSets(),
      [](const auto& indexSet) -> std::string {
        return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
      }
    )
  ) << "}\n";
#endif

  // Is Sequence Rule 1 enough?
  if(orderingHelper.isTotallyOrdered()) {
    // No conversion of indices in _auxiliaryApplySequenceRules()!
    return orderingHelper.getSets();
  }

  /* Sequence rule 2
   * - A node with higher atomic mass precedes ones with lower atomic mass
   *
   * NOTE: We skip this sequence rule for two reasons
   * - There is currently no way to represent isotopes in the library
   * - This rule may come to bear when disconnecting heteroaromatic cycles,
   *   where all aromatic atoms are given a duplicate atom with a mass equal
   *   to the average of what it would have if the double bonds were located
   *   at each of the possible positions. Although differentiating these
   *   cases would be good, there is currently no code to detect aromaticity
   *   nor permute all possible arrangements of double bonds in their
   *   subgraphs.
   */

  /* Sequence rule 3: double bond configurations
   * - Z > E > unassigned > non-stereogenic 
   *   (seqCis > seqTrans > non-stereogenic)
   */
  _runBFS<
    3, // Sequence rule 3
    false, // BFS downwards only
    true, // Insert edges
    false, // Insert vertices
    TreeEdgeIndex, // Multiset value type
    SequenceRuleThreeEdgeComparator // Multiset comparator type
  >(
    sourceIndex,
    orderingHelper
  );

#ifndef NDEBUG
Log::log(Log::Particulars::RankingTreeDebugInfo)
  << "  Sets post sequence rule 3: {" 
  << TemplateMagic::condenseIterable(
    TemplateMagic::map(
      orderingHelper.getSets(),
      [](const auto& indexSet) -> std::string {
        return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
      }
    )
  ) << "}\n";
#endif

  // Is sequence rule 3 enough?
  if(orderingHelper.isTotallyOrdered()) {
    // No conversion of indices in _auxiliaryApplySequenceRules()!
    return orderingHelper.getSets();
  }

  /* Sequence rule 4: Other configurations (RS, MP, EZ)
   * - stereogenic > pseudostereogenic > non-stereogenic
   * - like pairs of descriptors over unlike pairs of descriptors
   *   set 1: {R, M, Z}, set 2: {S, P, E}, like within a set, unlike crossover
   *
   *   Procedure:
   *
   *   - Create a set of all stereogenic groups within the branches to rank
   *   - Establish relative rank by application of sequence rules. 
   *
   *     Can probably use _auxiliaryApplySequenceRules recursively at the first
   *     junction between competing stereocenters on the respective branch
   *     indices to find out which precedes which, provided both are at the
   *     same depth from root.
   *
   *     (WARNING: This may be tricky!)
   *
   *   - Choose a representative stereodescriptor for each branch according to
   *     the following rules:
   *
   *     - The solitarily highest ranked stereogenic group
   *     - The stereodescriptor occurring more often than all others in 
   *       the set of stereodescriptors
   *     - All most-often-occurring stereodescriptors
   *
   *   - Precedence:
   *     - The branch with fewer representative stereodescriptors has priority
   *     - Go through both lists of ranked stereocenters simultaneously,
   *       proceeding from highest rank to lowest, pairing each occurring
   *       descriptor with the reference descriptor.
   *
   *       like pairs precede unlike pairs of stereodescriptors
   *
   *
   * - (Pseudoasymmetries) r precedes s and m precedes p
   */
  { // Sequence rule 4 local scope
    /* First part of rule: A regular omni-directional BFS, inserting both
     * edges and node indices into the multiset! Perhaps avoiding the
     * insertion of uninstantiated edges and vertices entirely?
     */

    using VariantType = boost::variant<TreeVertexIndex, TreeEdgeIndex>;

    std::set<TreeVertexIndex> visitedVertices {sourceIndex};

    std::map<
      TreeVertexIndex,
      std::multiset<VariantType, SequenceRuleFourVariantComparator>
    > comparisonSets;

    std::map<
      TreeVertexIndex,
      std::vector<TreeVertexIndex>
    > seeds;

    undecidedSets = orderingHelper.getUndecidedSets();

    // Initialize the BFS state
    for(const auto& undecidedSet : undecidedSets) {
      for(const auto& undecidedBranch : undecidedSet) {
        comparisonSets.emplace(
          undecidedBranch,
          *this
        );

        auto forwardEdge = boost::edge(sourceIndex, undecidedBranch, _tree);
        auto backwardEdge = boost::edge(undecidedBranch, sourceIndex, _tree);

        comparisonSets.at(undecidedBranch).emplace(undecidedBranch);
        comparisonSets.at(undecidedBranch).emplace(
          forwardEdge.second
          ? forwardEdge.first
          : backwardEdge.first
        );

        seeds[undecidedBranch] = {undecidedBranch};

        visitedVertices.insert(undecidedBranch);
      }
    }
    
    _compareBFSSets(comparisonSets, undecidedSets, orderingHelper);

    undecidedSets = orderingHelper.getUndecidedSets();

#ifndef NDEBUG
    if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
      _writeGraphvizFiles({
        _adaptMolGraph(_moleculeRef.dumpGraphviz()),
        dumpGraphviz("Sequence rule 3 prep", {0}),
        dumpGraphviz("_aux Sequence rule 4A", {sourceIndex}, visitedVertices),
        _makeGraph("_aux Sequence rule 4A multisets", sourceIndex, comparisonSets, undecidedSets),
        orderingHelper.dumpGraphviz()
      });
    }
#endif

    while(undecidedSets.size() > 0 && _relevantSeeds(seeds, undecidedSets)) {
      // Perform a full BFS Step on all undecided set seeds
      for(const auto& undecidedSet : undecidedSets) {
        for(const auto& undecidedTreeIndex : undecidedSet) {
          auto& branchSeeds = seeds[undecidedTreeIndex];

          std::vector<TreeVertexIndex> newSeeds;

          for(const auto& seed: branchSeeds) {

            for( // In-edges
              auto inIterPair = boost::in_edges(seed, _tree);
              inIterPair.first != inIterPair.second;
              ++inIterPair.first
            ) {
              const auto& inEdge = *inIterPair.first;

              auto edgeSource = boost::source(inEdge, _tree);

              // Check if already placed
              if(visitedVertices.count(edgeSource) == 0) {
                comparisonSets.at(undecidedTreeIndex).emplace(edgeSource);
                comparisonSets.at(undecidedTreeIndex).emplace(inEdge);

                newSeeds.push_back(edgeSource);
                visitedVertices.insert(edgeSource);
              }
            }

            for( // Out-edges
              auto outIterPair = boost::out_edges(seed, _tree);
              outIterPair.first != outIterPair.second;
              ++outIterPair.first
            ) {
              const auto& outEdge = *outIterPair.first;

              auto edgeTarget = boost::target(outEdge, _tree);

              // Check if already placed and non-duplicate
              if(
                visitedVertices.count(edgeTarget) == 0
                && !_tree[edgeTarget].isDuplicate
              ) {
                comparisonSets.at(undecidedTreeIndex).emplace(edgeTarget);
                comparisonSets.at(undecidedTreeIndex).emplace(outEdge);

                newSeeds.push_back(edgeTarget);
                visitedVertices.insert(edgeTarget);
              }
            }
          }

          // Overwrite the seeds
          branchSeeds = std::move(newSeeds);
        }
      }

      // Make comparisons in all undecided sets
      _compareBFSSets(comparisonSets, undecidedSets, orderingHelper);

      // Recalculate the undecided sets
      undecidedSets = orderingHelper.getUndecidedSets();

#ifndef NDEBUG
      if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
        _writeGraphvizFiles({
          _adaptMolGraph(_moleculeRef.dumpGraphviz()),
          dumpGraphviz("Sequence rule 3 prep", {0}),
          dumpGraphviz("_aux Sequence rule 4A", {sourceIndex}, visitedVertices),
          _makeGraph("_aux Sequence rule 4A multisets", sourceIndex, comparisonSets, undecidedSets),
          orderingHelper.dumpGraphviz()
        });
      }
#endif
    }

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "  Sets post sequence rule 4A: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        orderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

    // Is Sequence Rule 4, part A enough?
    if(orderingHelper.isTotallyOrdered()) {
      // No conversion of indices in _auxiliaryApplySequenceRules()!
      return orderingHelper.getSets();
    }

    /* Second part: Choosing representational stereodescriptors for all
     * branches and pairing in sequence of priority for branches.
     */
    std::map<
      TreeVertexIndex,
      std::set<VariantType>
    > stereocenterMap;

    VariantHasInstantiatedStereocenter isInstantiatedChecker {*this};

    // Copy all those variants from part A that are actually instantiated
    for(const auto& undecidedSet : orderingHelper.getUndecidedSets()) {
      for(const auto& undecidedBranch : undecidedSet) {
        stereocenterMap[undecidedBranch] = {};

        for(const auto& variantValue : comparisonSets.at(undecidedBranch)) {
          if(boost::apply_visitor(isInstantiatedChecker, variantValue)) {
            stereocenterMap.at(undecidedBranch).insert(variantValue);
          }
        }
      }
    }

    std::map<
      TreeVertexIndex,
      OrderDiscoveryHelper<VariantType>
    > relativeOrders;

    VariantDepth depthFetcher {*this};
    VariantSourceNode sourceNodeFetcher {*this};
    VariantStereocenterStringRepresentation stringRepFetcher {*this};

    std::map<
      TreeVertexIndex,
      std::set<VariantType>
    > representativeStereodescriptors;

    for(const auto& mapIterPair : stereocenterMap) {
      const auto& branchIndex = mapIterPair.first;
      const auto& variantSet = mapIterPair.second;

      // Instantiate the order discovery helpers
      relativeOrders.emplace(
        branchIndex,
        variantSet
      );

      // Compare based on depth
      TemplateMagic::forAllPairs(
        variantSet,
        [&](const auto& a, const auto& b) {
          auto aDepth = boost::apply_visitor(depthFetcher, a);
          auto bDepth = boost::apply_visitor(depthFetcher, b);

          if(aDepth < bDepth) {
            relativeOrders.at(branchIndex).addLessThanRelationship(a, b);
          } else if(bDepth < aDepth) {
            relativeOrders.at(branchIndex).addLessThanRelationship(b, a);
          }
        }
      );

      /* Try to resolve undecided sets via ranking downwards at junction of
       * pairs
       */
      for(
        const auto& undecidedSet 
        : relativeOrders.at(branchIndex).getUndecidedSets()
      ) {
        TemplateMagic::forAllPairs(
          undecidedSet,
          [&](const auto& a, const auto& b) {
            auto junctionInfo = _junction(
              boost::apply_visitor(sourceNodeFetcher, a),
              boost::apply_visitor(sourceNodeFetcher, b)
            );

            auto aJunctionChild = junctionInfo.firstPath.back();
            auto bJunctionChild = junctionInfo.secondPath.back();

            /* Do not use _auxiliaryApplySequenceRules for root-level ranking,
             * it should only establish differences within branches here
             */
            if(junctionInfo.junction != 0) {
              auto relativeRank = _auxiliaryApplySequenceRules(
                junctionInfo.junction,
                {aJunctionChild, bJunctionChild}
              );

              /* relativeRank can only have sizes 1 or 2, 1 meaning that no
               * difference was found
               */
              if(relativeRank.size() == 2) {
                if(relativeRank.front().front() == aJunctionChild) {
                  relativeOrders.at(branchIndex).addLessThanRelationship(a, b);
                } else {
                  relativeOrders.at(branchIndex).addLessThanRelationship(b, a);
                }
              }
            }
          }
        );
      }

      // Pick the representative stereodescriptor for each branch!
      auto undecidedStereocenterSets = relativeOrders.at(branchIndex).getUndecidedSets();

      if(undecidedStereocenterSets.size() == 0) {
        // 0 - No stereocenters in the branch, so none are representative
        representativeStereodescriptors[branchIndex] = {};
      } else if(undecidedStereocenterSets.back().size() == 1) {
        // 1 - The solitary highest ranked stereogenic group
        representativeStereodescriptors[branchIndex] = {
          undecidedStereocenterSets.back().front()
        };
      } else {
        // 2 - The stereodescriptor occurring more often than all others
        auto groupedByStringRep = TemplateMagic::groupByMapping(
          stereocenterMap.at(branchIndex),
          [&](const auto& variantType) -> std::string {
            return boost::apply_visitor(stringRepFetcher, variantType);
          }
        );

        auto maxSize = TemplateMagic::accumulate(
          groupedByStringRep,
          0u,
          [](const unsigned& maxSize, const auto& stringGroup) -> unsigned {
            if(stringGroup.size() > maxSize) {
              return stringGroup.size();
            }

            return maxSize;
          }
        );

        representativeStereodescriptors[branchIndex] = {};

        // All stereocenter string groups with maximum size are representative
        for(const auto& stringGroup : groupedByStringRep) {
          if(stringGroup.size() == maxSize) {
            representativeStereodescriptors.at(branchIndex).insert(
              stringGroup.begin(),
              stringGroup.end()
            );
          }
        }
      }
    }

    auto undecidedBranchSets = orderingHelper.getUndecidedSets();

    VariantLikePair variantLikeComparator {*this};

    for(const auto& undecidedSet : undecidedBranchSets) {
      TemplateMagic::forAllPairs(
        undecidedSet,
        [&](const auto& branchA, const auto& branchB) {
          // Precedence via amount of representative stereodescriptors
          if(
            representativeStereodescriptors.at(branchA).size() 
            < representativeStereodescriptors.at(branchB).size() 
          ) {
            orderingHelper.addLessThanRelationship(branchA, branchB);
          } else if(
            representativeStereodescriptors.at(branchB).size() 
            < representativeStereodescriptors.at(branchA).size() 
          ) {
            orderingHelper.addLessThanRelationship(branchB, branchA);
          } else {
            // Compare by sequential pairing
            /* What is considered a like pair?
             *
             * IUPAC says any pairs within the sets {R, M, Z} or {S, P, E}
             * are considered like-pairs, such as RR, RZ, SP, or ES. This
             * is a little more difficult for us, since any more expansive
             * stereodescriptors where choice is non-binary are not reducible
             * to two sets. Retaining the correct behavior for tetrahedral
             * stereodescriptors is the guiding principle here.
             *
             * So, we require that CN-tetrahedral-2-0 and EZ-2-0 are
             * considered like and so are CN-tetrahedral-2-1 and EZ-2-1. 
             *
             * We go about this by deciding that two stereodescriptors compare
             * like if they have the same number of assignments and have the
             * same assignment index.
             *
             * This is convenient, but means we have to ensure assignments
             * are canonical, i.e. a correspondence with R/S makes sense.
             * TODO
             */

            auto branchAOrders = relativeOrders.at(branchA).getSets();
            auto branchBOrders = relativeOrders.at(branchB).getSets();

            // Go through the ranked stereocenter groups in DESC order!
            auto branchAStereocenterGroupIter = branchAOrders.rbegin();
            auto branchBStereocenterGroupIter = branchBOrders.rbegin();

            while(
              branchAStereocenterGroupIter != branchAOrders.rend()
              && branchBStereocenterGroupIter != branchBOrders.rend()
            ) {
              unsigned ABranchLikePairs = 0, BBranchLikePairs = 0;

              // Count A-branch like pairs
              TemplateMagic::forAllPairs(
                *branchAStereocenterGroupIter,
                representativeStereodescriptors.at(branchA),
                [&](const auto& variantA, const auto& variantB) {
                  if(
                    boost::apply_visitor(
                      variantLikeComparator,
                      variantA,
                      variantB
                    )
                  ) {
                    ABranchLikePairs += 1;
                  }
                }
              );

              // Count B-branch like pairs
              TemplateMagic::forAllPairs(
                *branchBStereocenterGroupIter,
                representativeStereodescriptors.at(branchB),
                [&](const auto& variantA, const auto& variantB) {
                  if(
                    boost::apply_visitor(
                      variantLikeComparator,
                      variantA,
                      variantB
                    )
                  ) {
                    BBranchLikePairs += 1;
                  }
                }
              );

              if(ABranchLikePairs < BBranchLikePairs) {
                orderingHelper.addLessThanRelationship(branchA, branchB);
                break;
              } else if(BBranchLikePairs < ABranchLikePairs) {
                orderingHelper.addLessThanRelationship(branchB, branchA);
                break;
              }

              ++branchAStereocenterGroupIter;
              ++branchBStereocenterGroupIter;
            }
          }
        }
      );
    }

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "  Sets post sequence rule 4B: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        orderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

    // Is Sequence Rule 4, part B enough?
    if(orderingHelper.isTotallyOrdered()) {
      // No conversion of indices in _auxiliaryApplySequenceRules()!
      return orderingHelper.getSets();
    }

    // TODO 4C Pseudo-asymmetries, possibly via already-gathered information?

  } // End sequence rule 4 local scope

  /* Sequence rule 5: 
   * - Atom or group with {R, M, Z} precedes {S, P, E}
   */
  _runBFS<
    5, // Sequence rule 5
    false, // BFS downwards only
    true, // Insert edges
    true, // Insert vertices
    boost::variant<TreeVertexIndex, TreeEdgeIndex>, // MultisetValueType
    SequenceRuleFiveVariantComparator // Multiset comparator type
  >(
    sourceIndex,
    orderingHelper
  );

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "  Sets post sequence rule 5: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        orderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

  // Exhausted sequence rules, return the sets
  return orderingHelper.getSets();
}

std::vector<RankingTree::TreeVertexIndex> RankingTree::_expand(
  const RankingTree::TreeVertexIndex& index,
  const std::set<AtomIndexType>& molIndicesInBranch
) {
  std::vector<TreeVertexIndex> newIndices;

  std::set<AtomIndexType> treeOutAdjacencies;
  TreeGraphType::out_edge_iterator iter, end;
  std::tie(iter, end) = boost::out_edges(index, _tree);
  while(iter != end) {
    auto targetVertex = boost::target(*iter, _tree);

    treeOutAdjacencies.insert(
      _tree[targetVertex].molIndex
    );

    newIndices.push_back(targetVertex);

    ++iter;
  }

  for(
    const auto& molAdjacentIndex : 
    _moleculeRef.iterateAdjacencies(_tree[index].molIndex)
  ) {
    if(treeOutAdjacencies.count(molAdjacentIndex) != 0) {
      continue;
    }

    if(molIndicesInBranch.count(molAdjacentIndex) == 0) {
      auto newIndex = boost::add_vertex(_tree);
      _tree[newIndex].molIndex = molAdjacentIndex;
      _tree[newIndex].isDuplicate = false;

      newIndices.push_back(newIndex);

      boost::add_edge(index, newIndex, _tree);
      
      // Need to add duplicates!
      auto duplicateVertices = _addBondOrderDuplicates(index, newIndex);
      std::copy(
        duplicateVertices.begin(),
        duplicateVertices.end(),
        std::back_inserter(newIndices)
      );

      // Copy the indices and insert the newest addition
      auto molIndicesInBranchCopy = molIndicesInBranch;
      molIndicesInBranchCopy.insert(molAdjacentIndex);
    } else if(molAdjacentIndex != _tree[_parent(index)].molIndex)  {
      auto newIndex = boost::add_vertex(_tree);
      _tree[newIndex].molIndex = molAdjacentIndex;
      _tree[newIndex].isDuplicate = true;

      newIndices.push_back(newIndex);

      boost::add_edge(index, newIndex, _tree);
    }
  }

  // Does not contain any new duplicate indices
  return newIndices;
}

template<>
std::string RankingTree::toString(const TreeVertexIndex& vertexIndex) const {
  return std::to_string(vertexIndex);
}

template<>
std::string RankingTree::toString(const TreeEdgeIndex& edgeIndex) const {
  return (
    std::to_string(
      boost::source(edgeIndex, _tree)
    ) + "->"s + std::to_string(
      boost::target(edgeIndex, _tree)
    )
  );
}

template<>
std::string RankingTree::toString(const boost::variant<TreeVertexIndex, TreeEdgeIndex>& variant) const {
  if(variant.which() == 0) {
    return toString<TreeVertexIndex>(boost::get<TreeVertexIndex>(variant));
  } 
  
  return toString<TreeEdgeIndex>(boost::get<TreeEdgeIndex>(variant));
}

//! Returns the parent of a node. Fails if called on the root!
RankingTree::TreeVertexIndex RankingTree::_parent(const RankingTree::TreeVertexIndex& index) const {
  assert(index != 0);

  // All nodes in the graph must have in_degree of 1
  assert(boost::in_degree(index, _tree) == 1);

  // Follow singular in_node to source, overwrite index
  auto iterPair = boost::in_edges(index, _tree);
  return boost::source(*iterPair.first, _tree);
}

//! Returns the direct descendants of a tree node
std::set<RankingTree::TreeVertexIndex> RankingTree::_children(const RankingTree::TreeVertexIndex& index) const {
  std::set<TreeVertexIndex> children;

  TreeGraphType::out_edge_iterator iter, end;
  std::tie(iter, end) = boost::out_edges(index, _tree);

  while(iter != end) {
    children.insert(
      boost::target(*iter, _tree)
    );

    ++iter;
  }

  return children;
}

std::set<RankingTree::TreeVertexIndex> RankingTree::_adjacents(const RankingTree::TreeVertexIndex& index) const {
  std::set<TreeVertexIndex> adjacents;

  auto inIterPair = boost::in_edges(index, _tree);
  while(inIterPair.first != inIterPair.second) {
    adjacents.emplace(
      boost::source(*inIterPair.first, _tree)
    );

    ++inIterPair.first;
  }

  auto outIterPair = boost::out_edges(index, _tree);
  while(outIterPair.first != outIterPair.second) {
    adjacents.emplace(
      boost::target(*outIterPair.first, _tree)
    );

    ++outIterPair.first;
  }

  return adjacents;
}

std::set<RankingTree::TreeEdgeIndex> RankingTree::_adjacentEdges(const RankingTree::TreeVertexIndex& index) const {
  std::set<TreeEdgeIndex> edges;

  auto inIterPair = boost::in_edges(index, _tree);
  while(inIterPair.first != inIterPair.second) {
    edges.emplace(*inIterPair.first);

    ++inIterPair.first;
  }

  auto outIterPair = boost::out_edges(index, _tree);
  while(outIterPair.first != outIterPair.second) {
    edges.emplace(*outIterPair.first);

    ++outIterPair.first;
  }

  return edges;
}

bool RankingTree::_isBondSplitDuplicateVertex(const TreeVertexIndex& index) const {
  // If the vertex isn't duplicate, it can't be a closure
  if(!_tree[index].isDuplicate) {
    return false;
  }

  /* Duplicate atoms have two sources - they can be either from multiple bond
   * breakage or from cycle closure. If we can find the typical pattern from a
   * multiple bond breakage, then it can't be a cycle closure
   */
  auto parentIndex = _parent(index);

  auto inEdgesIterPair = boost::in_edges(parentIndex, _tree);
  while(inEdgesIterPair.first != inEdgesIterPair.second) {
    auto adjacentIndex = boost::source(*inEdgesIterPair.first, _tree);
    if(
      _tree[adjacentIndex].molIndex == _tree[index].molIndex
      && !_tree[adjacentIndex].isDuplicate
    ) {
      return true;
    }

    ++inEdgesIterPair.first;
  }

  auto outEdgesIterPair = boost::out_edges(parentIndex, _tree);
  while(outEdgesIterPair.first != outEdgesIterPair.second) {
    auto adjacentIndex = boost::target(*outEdgesIterPair.first, _tree);

    if(
      _tree[adjacentIndex].molIndex == _tree[index].molIndex
      && !_tree[adjacentIndex].isDuplicate
    ) {
      return true;
    }

    ++outEdgesIterPair.first;
  }

  // Remaining case is a cycle closure
  return false;
}

bool RankingTree::_isCycleClosureDuplicateVertex(const TreeVertexIndex& index) const {
  // If the vertex isn't duplicate, it can't be a closure
  if(!_tree[index].isDuplicate) {
    return false;
  }

  /* Duplicate atoms have two sources - they can be either from multiple bond
   * breakage or from cycle closure. If we can find the typical pattern from a
   * multiple bond breakage, then it isn't a cycle closure.
   */
  auto parentIndex = _parent(index);

  auto inEdgesIterPair = boost::in_edges(parentIndex, _tree);
  while(inEdgesIterPair.first != inEdgesIterPair.second) {
    auto adjacentIndex = boost::source(*inEdgesIterPair.first, _tree);
    if(
      _tree[adjacentIndex].molIndex == _tree[index].molIndex
      && !_tree[adjacentIndex].isDuplicate
    ) {
      return false;
    }

    ++inEdgesIterPair.first;
  }

  auto outEdgesIterPair = boost::out_edges(parentIndex, _tree);
  while(outEdgesIterPair.first != outEdgesIterPair.second) {
    auto adjacentIndex = boost::target(*outEdgesIterPair.first, _tree);

    if(
      _tree[adjacentIndex].molIndex == _tree[index].molIndex
      && !_tree[adjacentIndex].isDuplicate
    ) {
      return false;
    }

    ++outEdgesIterPair.first;
  }

  /* When you have eliminated the impossible, whatever remains, however
   * improbable, must be the truth.
   */
  return true;
}

std::set<RankingTree::TreeVertexIndex> RankingTree::_auxiliaryAdjacentsToRank(
  const TreeVertexIndex& sourceIndex,
  const std::set<TreeVertexIndex>& excludeIndices
) const {
  return TemplateMagic::moveIf(
    _adjacents(sourceIndex),
    [&](const auto& nodeIndex) -> bool {
      // In case we explicitly exclude them, immediately discard
      if(excludeIndices.count(nodeIndex) > 0) {
        return false;
      }

      // If they are duplicate, we have some work to do
      if(_tree[nodeIndex].isDuplicate) {
        /* We need to distinguish between duplicate atoms that are the result
         * of multiple bond splits and cycle closures. Cycle closures need
         * to be kept, while multiple bond splits need to be removed.
         */
        return _isCycleClosureDuplicateVertex(nodeIndex);
      }

      // Keep the vertex in all other cases
      return true;
    }
  );
}

unsigned RankingTree::_nonDuplicateDegree(const RankingTree::TreeVertexIndex& index) const {
  auto adjacents = _adjacents(index);

  auto numDuplicate = TemplateMagic::accumulate(
    adjacents,
    0u,
    [&](const unsigned& count, const auto& treeIndex) -> unsigned {
      return count + static_cast<unsigned>(
        _tree[treeIndex].isDuplicate
      );
    }
  );

  return adjacents.size() - numDuplicate;
}

bool RankingTree::_relevantSeeds(
  const std::map<
    RankingTree::TreeVertexIndex, 
    std::vector<RankingTree::TreeVertexIndex>
  >& seeds,
  const std::vector<
    std::vector<RankingTree::TreeVertexIndex>
  >& undecidedSets
) {
  for(const auto& undecidedSet : undecidedSets) {
    for(const auto& undecidedTreeIndex : undecidedSet) {
      if(seeds.at(undecidedTreeIndex).size() > 0) {
        return true;
      }
    }
  }

  return false;
}

std::vector<RankingTree::TreeVertexIndex> RankingTree::_addBondOrderDuplicates(
  const TreeVertexIndex& treeSource,
  const TreeVertexIndex& treeTarget
) {
  std::vector<TreeVertexIndex> newIndices;

  const auto& molGraph = _moleculeRef.getGraph();

  auto molGraphEdge = boost::edge(
    _tree[treeSource].molIndex,
    _tree[treeTarget].molIndex,
    molGraph
  );

  /* In case the bond order is non-fractional (aromatic / eta)
   * and > 1, add duplicate atoms
   */
  assert(molGraphEdge.second);

  auto bondType = molGraph[molGraphEdge.first].bondType;

  /* If the bond is double, we must remember it for sequence rule 2.
   * Remembering these edges is way more convenient than looking for subgraphs
   * which have the trace pattern of a split multiple-bond of BO 2.
   */
  if(bondType == BondType::Double) {
    auto edgePair = boost::edge(treeSource, treeTarget, _tree);
    _doubleBondEdges.emplace(edgePair.first);
  }

  double bondOrder = Bond::bondOrderMap.at(
    static_cast<unsigned>(bondType)
  );
  unsigned integralBondOrder = static_cast<unsigned>(bondOrder);

  // Check if bond order is integral
  if(static_cast<double>(integralBondOrder) == bondOrder) {
    // Add duplicate atom to source and target integralBondOrder - 1 times
    for(unsigned N = 1; N < integralBondOrder; ++N) {
      // Add a node with molIndex molSource to treeTarget
      auto a = boost::add_vertex(_tree);
      _tree[a].molIndex = _tree[treeSource].molIndex;
      _tree[a].isDuplicate = true;
      boost::add_edge(treeTarget, a, _tree);

      // Add a node with molIndex molTarget to treeSource
      auto b = boost::add_vertex(_tree);
      _tree[b].molIndex = _tree[treeTarget].molIndex;
      _tree[b].isDuplicate = true;

      newIndices.push_back(b);

      boost::add_edge(treeSource, b, _tree);
    }
  }

  return newIndices;
}

std::set<AtomIndexType> RankingTree::_molIndicesInBranch(TreeVertexIndex index) const {
  std::set<AtomIndexType> indices {_tree[index].molIndex};

  while(index != 0) {
    // All nodes in the graph must have in_degree of 1
    assert(boost::in_degree(index, _tree) == 1);

    // Follow singular in_node to source, overwrite index
    auto iterPair = boost::in_edges(index, _tree);
    index = boost::source(*iterPair.first, _tree);

    indices.insert(_tree[index].molIndex);
  }

  return indices;
}

unsigned RankingTree::_duplicateDepth(TreeVertexIndex index) const {
  // Call this only on duplicate vertices!
  assert(_tree[index].isDuplicate);

  auto duplicateMolIndex = _tree[index].molIndex;

  /* Summary:
   * - If the original atom is not found, return actual depth
   * - If the original atom is found, return that vertex's depth
   */

  unsigned depth = 0;

  while(index != 0) {
    // All nodes in the graph must have in_degree of 1
    assert(boost::in_degree(index, _tree) == 1);

    // Follow singular in_node to source, overwrite index
    auto iterPair = boost::in_edges(index, _tree);
    index = boost::source(*iterPair.first, _tree);

    ++depth;

    if(_tree[index].molIndex == duplicateMolIndex) {
      /* Reset depth. Since the loop continues until root, this returns the
       * depth of the non-duplicate node
       */
      depth = 0;
    }
  }

  return depth;
}

//! Returns the depth of a node in the tree
unsigned RankingTree::_depthOfNode(TreeVertexIndex index) const {
  /* As long as VertexListS is vecS, the first node (root) will always have
   * index zero
   */
  unsigned depth = 0;

  while(index != 0) {
    // All nodes in the graph must have in_degree of 1
    assert(boost::in_degree(index, _tree) == 1);

    // Follow singular in_node to source, overwrite index
    auto iterPair = boost::in_edges(index, _tree);
    index = boost::source(*iterPair.first, _tree);

    ++depth;
  }

  return depth;
}

//!  Returns a mixed depth measure for ranking both vertices and edges
unsigned RankingTree::_mixedDepth(const TreeVertexIndex& vertexIndex) const {
  return 2 * _depthOfNode(vertexIndex);
}

//!  Returns a mixed depth measure for ranking both vertices and edges
unsigned RankingTree::_mixedDepth(const TreeEdgeIndex& edgeIndex) const {
  return 2 * _depthOfNode(
    boost::source(edgeIndex, _tree)
  ) + 1;
}

struct RankingTree::JunctionInfo {
  TreeVertexIndex junction;

  std::vector<TreeVertexIndex> firstPath, secondPath;
};

/*!
 * Returns the deepest vertex in the tree in whose child branches both a and
 * b are located
 */
typename RankingTree::JunctionInfo RankingTree::_junction(
  const TreeVertexIndex& a,
  const TreeVertexIndex& b
) const {
  JunctionInfo data;


  /* Determine the junction vertex */
  std::set<TreeVertexIndex> aBranchIndices = _molIndicesInBranch(a);

  // By default, the junction is root
  data.junction = 0;

  // In case b is included in the path, that is the junction
  if(aBranchIndices.count(b)) {
    data.junction = b;
  } else {
    // Backtrack with b, checking at each iteration
    auto bCurrent = b;
    while(bCurrent != 0) {
      bCurrent = _parent(bCurrent);

      if(aBranchIndices.count(bCurrent)) {
        data.junction = bCurrent;
      }
    }
  }

  /* Reconstruct the paths to the junction */

  for(
    TreeVertexIndex aCurrent = a;
    aCurrent != data.junction;
    aCurrent = _parent(aCurrent)
  ) {
    data.firstPath.push_back(aCurrent);
  }

  for(
    TreeVertexIndex bCurrent = b;
    bCurrent != data.junction;
    bCurrent = _parent(bCurrent)
  ) {
    data.secondPath.push_back(bCurrent);
  }

  return data;
}

//! Returns whether a molecular graph index exists in a specific branch
bool RankingTree::_molIndexExistsInBranch(
  const AtomIndexType& molIndex,
  TreeVertexIndex treeIndex
) const {
  // Check whether this treeIndex already has the molIndex
  if(_tree[treeIndex].molIndex == molIndex) {
    return true;
  }

  while(treeIndex != 0) {
    // All nodes in the graph must have in_degree of 1
    assert(boost::in_degree(treeIndex, _tree) == 1);

    /* Move up one node in the graph. Follow singular in_node to source,
     * overwrite index
     */
    auto iterPair = boost::in_edges(treeIndex, _tree);
    treeIndex = boost::source(*iterPair.first, _tree);

    // Test this position
    if(_tree[treeIndex].molIndex == molIndex) {
      return true;
    }
  }

  return false;
}


std::set<RankingTree::TreeVertexIndex> RankingTree::_collectSeeds(
  const std::map<
    TreeVertexIndex,
    std::vector<TreeVertexIndex>
  >& seeds,
  const std::vector<
    std::vector<TreeVertexIndex>
  >& undecidedSets
) {
  std::set<TreeVertexIndex> visited;

  for(const auto& undecidedSet : undecidedSets) {
    for(const auto& undecidedBranch : undecidedSet) {
      for(const auto& seed : seeds.at(undecidedBranch)) {
        visited.insert(seed);
      }
    }
  }

  return visited;
}

RankingTree::RankingTree(
  const Molecule& molecule,
  const AtomIndexType& atomToRank,
  const std::set<AtomIndexType>& excludeIndices,
  const ExpansionOption& expansionMethod,
  const boost::optional<Delib::PositionCollection>& positionsOption
) : _moleculeRef(molecule) {
  // Set the root vertex
  auto rootIndex = boost::add_vertex(_tree);
  _tree[rootIndex].molIndex = atomToRank;
  _tree[rootIndex].isDuplicate = false;

  /* Add the direct descendants of the root atom, if they are not explicitly
   * excluded by parameters
   */
  std::set<TreeVertexIndex> branchIndices;
  for(
    const auto& rootAdjacentIndex : 
    _moleculeRef.iterateAdjacencies(atomToRank)
  ) {
    if(excludeIndices.count(rootAdjacentIndex) == 0) {
      auto branchIndex = boost::add_vertex(_tree);
      _tree[branchIndex].molIndex = rootAdjacentIndex;
      _tree[branchIndex].isDuplicate = false;

      boost::add_edge(rootIndex, branchIndex, _tree);

      _addBondOrderDuplicates(rootIndex, branchIndex);

      branchIndices.insert(branchIndex);
    }
  }

  // Set the ordering helper's list of unordered values
  _branchOrderingHelper.setUnorderedValues(branchIndices);

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "Ranking substituents of atom index " 
    << _tree[0].molIndex
    << ": {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        _branchOrderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

// The class should work regardless of which tree expansion method is chosen
if(expansionMethod == ExpansionOption::Optimized) {
  /* Expand only those branches which are yet undecided, immediately compare
   * using sequence rule 1.
   */

  std::map<
    TreeVertexIndex,
    std::multiset<TreeVertexIndex, SequenceRuleOneVertexComparator>
  > comparisonSets;

  std::map<
    TreeVertexIndex,
    std::vector<TreeVertexIndex>
  > seeds;

  for(const auto& branchIndex : branchIndices) {
    comparisonSets.emplace(
      branchIndex,
      *this
    );

    comparisonSets.at(branchIndex).insert(branchIndex);

    seeds[branchIndex] = {branchIndex};
  }

  auto undecidedSets = _branchOrderingHelper.getUndecidedSets();

  // Make the first comparison
  _compareBFSSets(comparisonSets, undecidedSets, _branchOrderingHelper);

  // Update the undecided sets
  undecidedSets = _branchOrderingHelper.getUndecidedSets();

#ifndef NDEBUG
  // Write debug graph files if the corresponding log particular is set
  if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
    std::string header = "Sequence rule 1";

    _writeGraphvizFiles({
      _adaptMolGraph(_moleculeRef.dumpGraphviz()),
      dumpGraphviz(
        header,
        {0},
        _collectSeeds(seeds, undecidedSets)
      ),
      _makeGraph(
        header + " multisets"s,
        0,
        comparisonSets,
        undecidedSets
      ),
      _branchOrderingHelper.dumpGraphviz()
    });
  }
#endif

  // TODO try to avoid repeated computation with _molIndicesInBranch somehow
  // Main BFS loop
  while(undecidedSets.size() > 0 && _relevantSeeds(seeds, undecidedSets)) {

    // Perform a step
    for(const auto& undecidedSet : undecidedSets) {
      for(const auto& undecidedBranch : undecidedSet) {
        auto& branchSeeds = seeds.at(undecidedBranch);
        auto& branchComparisonSet = comparisonSets.at(undecidedBranch);
        
        branchComparisonSet.clear();

        std::vector<TreeVertexIndex> newSeeds;
        
        for(const auto& seedVertex : branchSeeds) {
          auto newVertices = _expand(
            seedVertex,
            _molIndicesInBranch(seedVertex)
          );

          // Add ALL new vertices to the comparison set
          std::copy(
            newVertices.begin(),
            newVertices.end(),
            std::inserter(branchComparisonSet, branchComparisonSet.begin())
          );

          // Add non-duplicate vertices to the new seeds
          std::copy_if(
            newVertices.begin(),
            newVertices.end(),
            std::back_inserter(newSeeds),
            [&](const auto& newVertex) -> bool {
              return !_tree[newVertex].isDuplicate;
            }
          );
        }

        branchSeeds = std::move(newSeeds);
      }
    }

    // Compare and update the undecided sets
    _compareBFSSets(comparisonSets, undecidedSets, _branchOrderingHelper);
    undecidedSets = _branchOrderingHelper.getUndecidedSets();

#ifndef NDEBUG
    // Write debug graph files if the corresponding log particular is set
    if(Log::particulars.count(Log::Particulars::RankingTreeDebugInfo) > 0) {
      std::string header = "Sequence rule 1";

      _writeGraphvizFiles({
        _adaptMolGraph(_moleculeRef.dumpGraphviz()),
        dumpGraphviz(
          header,
          {0},
          _collectSeeds(seeds, undecidedSets)
        ),
        _makeGraph(
          header + " multisets"s,
          0,
          comparisonSets,
          undecidedSets
        ),
        _branchOrderingHelper.dumpGraphviz()
      });
    }
#endif
  }

} else { // Full tree expansion requested

  std::vector<TreeVertexIndex> seeds;
  std::copy(
    branchIndices.begin(),
    branchIndices.end(),
    std::back_inserter(seeds)
  );
  
  // TODO try to avoid repeated computation with _molIndicesInBranch somehow
  do {
    std::vector<TreeVertexIndex> newSeeds;
    
    for(const auto& seedVertex : seeds) {
      auto newVertices = _expand(
        seedVertex,
        _molIndicesInBranch(seedVertex)
      );

      // Add non-duplicate vertices to the new seeds
      std::copy_if(
        newVertices.begin(),
        newVertices.end(),
        std::back_inserter(newSeeds),
        [&](const auto& newVertex) -> bool {
          return !_tree[newVertex].isDuplicate;
        }
      );
    }

    seeds = std::move(newSeeds);
  } while(seeds.size() > 0);

  /* Apply sequence rule 1 here, not in _applySequenceRules, since the
   * optimized version IS the application of sequence rule 1
   */
  /* Sequence rule 1
   * - Higher atomic number precedes lower atomic number
   * - A duplicate atom node whose corresponding non-duplicate atom node is
   *   root or is closer to the root preceds a duplicate node whose
   *   corresponding atom node is further from the root
   */
  _runBFS<
    1, // Sequence rule 1
    true, // BFS downwards only
    false, // Insert edges
    true, // Insert vertices
    TreeVertexIndex, // Multiset value type
    SequenceRuleOneVertexComparator // Multiset comparator type
  >(
    0, // Source index is root
    _branchOrderingHelper
  );
}

#ifndef NDEBUG
  Log::log(Log::Particulars::RankingTreeDebugInfo)
    << "Sets post sequence rule 1: {" 
    << TemplateMagic::condenseIterable(
      TemplateMagic::map(
        _branchOrderingHelper.getSets(),
        [](const auto& indexSet) -> std::string {
          return "{"s + TemplateMagic::condenseIterable(indexSet) + "}"s;
        }
      )
    ) << "}\n";
#endif

  // Was sequence rule 1 enough?
  if(_branchOrderingHelper.isTotallyOrdered()) {
    return;
  }

  // Perform ranking
  _applySequenceRules(positionsOption);
}


std::string RankingTree::dumpGraphviz(
  const std::string& title,
  const std::set<TreeVertexIndex>& squareVertices,
  const std::set<TreeVertexIndex>& colorVertices,
  const std::set<TreeEdgeIndex>& colorEdges
) const {
  GraphvizWriter propertyWriter {
    *this,
    title,
    squareVertices,
    colorVertices,
    colorEdges
  };

  std::stringstream ss;

  boost::write_graphviz(
    ss,
    _tree,
    propertyWriter,
    propertyWriter,
    propertyWriter
  );

  return ss.str();
}

const typename RankingTree::TreeGraphType& RankingTree::getGraph() const {
  return _tree;
}

std::vector<
  std::vector<AtomIndexType>
> RankingTree::_mapToAtomIndices(
  const std::vector<
    std::vector<RankingTree::TreeVertexIndex>
  >& treeRankingSets
) const {
  return TemplateMagic::map(
    treeRankingSets,
    [&](const auto& set) -> std::vector<AtomIndexType> {
      return TemplateMagic::map(
        set,
        [&](const auto& treeVertex) -> AtomIndexType {
          return _tree[treeVertex].molIndex;
        }
      );
    }
  );
}

std::vector<
  std::vector<AtomIndexType>
> RankingTree::getRanked() const {
  // We must transform the ranked tree vertex indices back to molecule indices.
  return _mapToAtomIndices(
    _branchOrderingHelper.getSets()
  );
}


#ifndef NDEBUG
// Initialize the debug counter
unsigned RankingTree::_debugMessageCounter = 0;

void RankingTree::_writeGraphvizFiles(
  const std::vector<std::string>& graphvizStrings
) {
  using namespace std::string_literals;

  for(unsigned i = 0; i < graphvizStrings.size(); ++i) {
    std::string filename = (
      "ranking-tree-"s
      + std::to_string(_debugMessageCounter)
      + "-"s
      + std::to_string(i)
      + ".dot"s
    );


    std::ofstream outFile(filename);
    outFile << graphvizStrings.at(i);
    outFile.close();
  }

  ++_debugMessageCounter;
}

std::string RankingTree::_adaptMolGraph(std::string molGraph) {
  /* We have to make it a digraph for compatibility with all other graphs, so:
   * 1: "graph G {" -> "digraph G {"
   */
  molGraph.insert(0, "di");

  // 2: all "--" edges must become -> edges
  auto firstEdgePos = molGraph.find_first_of("--");
  boost::replace_all(molGraph, "--", "->");

  // 3: all edges need a dir="none" specification to avoid directed edges
  auto bracketPos = molGraph.find_first_of(']', firstEdgePos);
  while(bracketPos != std::string::npos) {
    molGraph.insert(bracketPos, R"(, dir="none")");
    bracketPos += 13;
    bracketPos = molGraph.find_first_of(']', bracketPos);
  }

  return molGraph;
}
#endif


} // namespace MoleculeManip
