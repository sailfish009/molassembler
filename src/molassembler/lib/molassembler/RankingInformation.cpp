/*!@file
 * @copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 */
#include "molassembler/RankingInformation.h"

#include "temple/Adaptors/Zip.h"
#include "temple/Functional.h"
#include "temple/TinySet.h"

#include "molassembler/Containers/OrderDiscoveryHelper.h"
#include "molassembler/Cycles.h"

namespace Scine {

namespace molassembler {

LinkInformation::LinkInformation() = default;

LinkInformation::LinkInformation(
  std::pair<unsigned, unsigned> siteIndices,
  std::vector<AtomIndex> sequence,
  const AtomIndex source
) {
  /* Fix degrees of freedom of the underlying information so we can
   * efficiently implement operator <. indexPair can be an ordered pair:
   */
  indexPair = std::move(siteIndices);
  if(indexPair.first > indexPair.second) {
    std::swap(indexPair.first, indexPair.second);
  }

  // The cycle sequence should be centralized on the source vertex
  cycleSequence = centralizeRingIndexSequence(std::move(sequence), source);

  /* After centralization, the source vertex is first. We need to fix the
   * remaining degree of freedom, which is if that the cycle sequence in
   * between can be reversed. We choose to fix it by making it ascending if
   * there are at least four vertices in the sequence between the second and
   * second-to-last vertices
   */

  if(
    cycleSequence.size() >= 3
    && cycleSequence.at(1) > cycleSequence.back()
  ) {
    // Reverse is [first, last), and cycleSequence is a vector, so:
    std::reverse(
      std::begin(cycleSequence) + 1,
      std::end(cycleSequence)
    );
  }
}

void LinkInformation::applyPermutation(const std::vector<AtomIndex>& permutation) {
  // A link's site indices do not change, but the sequence does
  for(AtomIndex& atomIndex : cycleSequence) {
    atomIndex = permutation.at(atomIndex);
  }

  // Conditional reverse of the sequence now depends on the new vertex indices
  if(
    cycleSequence.size() >= 3
    && cycleSequence.at(1) > cycleSequence.back()
  ) {
    // Reverse is [first, last), and cycleSequence is a vector, so:
    std::reverse(
      std::begin(cycleSequence) + 1,
      std::end(cycleSequence)
    );
  }
}

bool LinkInformation::operator == (const LinkInformation& other) const {
  return (
    indexPair == other.indexPair
    && cycleSequence == other.cycleSequence
  );
}

bool LinkInformation::operator != (const LinkInformation& other) const {
  return !(*this == other);
}

bool LinkInformation::operator < (const LinkInformation& other) const {
  return std::tie(indexPair, cycleSequence) < std::tie(other.indexPair, other.cycleSequence);
}


std::vector<unsigned> RankingInformation::siteConstitutingAtomsRankedPositions(
  const std::vector<AtomIndex>& siteAtomList,
  const RankingInformation::RankedSubstituentsType& substituentRanking
) {
  auto positionIndices = temple::map(
    siteAtomList,
    [&substituentRanking](AtomIndex constitutingIndex) -> unsigned {
      return temple::find_if(
        substituentRanking,
        [&constitutingIndex](const auto& equalPrioritySet) -> bool {
          return temple::find(equalPrioritySet, constitutingIndex) != equalPrioritySet.end();
        }
      ) - substituentRanking.begin();
    }
  );

  std::sort(
    positionIndices.begin(),
    positionIndices.end(),
    std::greater<> {}
  );

  return positionIndices;
}

RankingInformation::RankedSitesType RankingInformation::rankSites(
  const SiteListType& sites,
  const RankedSubstituentsType& substituentRanking
) {
  // Use partial ordering helper
  OrderDiscoveryHelper<unsigned> siteOrdering;
  siteOrdering.setUnorderedValues(
    temple::iota<unsigned>(
      sites.size()
    )
  );

  const unsigned sitesSize = sites.size();

  // Pairwise compare all sites, adding relational discoveries to the ordering helper
  for(unsigned i = 0; i < sitesSize; ++i) {
    for(unsigned j = i + 1; j < sitesSize; ++j) {
      const auto& a = sites.at(i);
      const auto& b = sites.at(j);

      if(a.size() < b.size()) {
        siteOrdering.addLessThanRelationship(i, j);
      } else if(a.size() > b.size()) {
        siteOrdering.addLessThanRelationship(j, i);
      } else {
        auto aPositions = siteConstitutingAtomsRankedPositions(a, substituentRanking);
        auto bPositions = siteConstitutingAtomsRankedPositions(b, substituentRanking);

        // lexicographical comparison
        if(aPositions < bPositions) {
          siteOrdering.addLessThanRelationship(i, j);
        } else if(aPositions > bPositions) {
          siteOrdering.addLessThanRelationship(j, i);
        }

        // No further differentiation.
      }
    }
  }

  // OrderingDiscoveryHelper's getSets() returns DESC order, so we reverse it to ASC
  auto finalSets = siteOrdering.getSets();

  std::reverse(
    std::begin(finalSets),
    std::end(finalSets)
  );

  return finalSets;
}

void RankingInformation::applyPermutation(const std::vector<AtomIndex>& permutation) {
  // .substituentRanking is mapped by applying the vertex permutation
  for(auto& group : substituentRanking) {
    for(AtomIndex& atomIndex : group) {
      atomIndex = permutation.at(atomIndex);
    }
  }
  // .sites too
  for(auto& group : sites) {
    for(AtomIndex& atomIndex : group) {
      atomIndex = permutation.at(atomIndex);
    }
  }
  // .siteRanking is unchanged as it is index based into .sites
  // .links do have to be mapped, though
  for(LinkInformation& link : links) {
    link.applyPermutation(permutation);
  }

  // Sort links to re-establish ordering
  std::sort(
    std::begin(links),
    std::end(links)
  );
}

unsigned RankingInformation::getSiteIndexOf(const AtomIndex i) const {
  // Find the atom index i within the set of ligand definitions
  auto findIter = temple::find_if(
    sites,
    [&](const auto& siteAtomList) -> bool {
      return temple::any_of(
        siteAtomList,
        [&](const AtomIndex siteConstitutingIndex) -> bool {
          return siteConstitutingIndex == i;
        }
      );
    }
  );

  if(findIter == std::end(sites)) {
    throw std::out_of_range("Specified atom index is not part of any ligand");
  }

  return findIter - std::begin(sites);
}

unsigned RankingInformation::getRankedIndexOfSite(const unsigned i) const {
  auto findIter = temple::find_if(
    siteRanking,
    [&](const auto& equallyRankedSiteIndices) -> bool {
      return temple::any_of(
        equallyRankedSiteIndices,
        [&](const unsigned siteIndex) -> bool {
          return siteIndex == i;
        }
      );
    }
  );

  if(findIter == std::end(siteRanking)) {
    throw std::out_of_range("Specified ligand index is not ranked.");
  }

  return findIter - std::begin(siteRanking);
}

bool RankingInformation::hasHapticSites() const {
  return temple::any_of(
    sites,
    [](const auto& siteAtomList) -> bool {
      return siteAtomList.size() > 1;
    }
  );
}

bool RankingInformation::operator == (const RankingInformation& other) const {
  /* This is a nontrivial operator since there is some degree of freedom in how
   * sites are chosen
   */

  // Check all sizes
  if(
    substituentRanking.size() != other.substituentRanking.size()
    || sites.size() != other.sites.size()
    || siteRanking.size() != other.siteRanking.size()
    || links.size() != other.links.size()
  ) {
    return false;
  }

  // substituentRanking can be compared lexicographically
  if(substituentRanking != other.substituentRanking) {
    return false;
  }

  // Combined comparison of siteRanking with sites
  if(
    !temple::all_of(
      temple::adaptors::zip(
        siteRanking,
        other.siteRanking
      ),
      [&](
        const auto& thisEqualSiteGroup,
        const auto& otherEqualSiteGroup
      ) -> bool {
        temple::TinyUnorderedSet<AtomIndex> thisSiteGroupVertices;

        for(const auto siteIndex : thisEqualSiteGroup) {
          for(const auto siteConstitutingIndex : sites.at(siteIndex)) {
            thisSiteGroupVertices.insert(siteConstitutingIndex);
          }
        }

        for(const auto siteIndex : otherEqualSiteGroup) {
          for(const auto siteConstitutingIndex : other.sites.at(siteIndex)) {
            if(thisSiteGroupVertices.count(siteConstitutingIndex) == 0) {
              return false;
            }
          }
        }

        return true;
      }
    )
  ) {
    return false;
  }

  // Compare the links (these should be kept sorted throughout execution)
  assert(std::is_sorted(std::begin(links), std::end(links)));
  assert(std::is_sorted(std::begin(other.links), std::end(other.links)));
  return links == other.links;
}

bool RankingInformation::operator != (const RankingInformation& other) const {
  return !(*this == other);
}

} // namespace molassembler

} // namespace Scine
