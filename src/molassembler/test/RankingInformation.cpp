#define BOOST_TEST_MODULE RankingInformationTestModule
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include "RankingInformation.h"
#include "temple/Stringify.h"

template<typename T>
using RaggedVector = std::vector<
  std::vector<T>
>;

BOOST_AUTO_TEST_CASE(rankingCombinationTests) {
  using namespace molassembler;

  using RaggedAtoms = RaggedVector<AtomIndexType>;
  using RaggedLigands = RaggedVector<unsigned>;

  auto symmetricHapticPincerRanking = RaggedAtoms {
    {1, 6},
    {2, 5},
    {3, 4}
  };

  auto symmetricHapticPincerLigands = RaggedAtoms {
    {1, 2},
    {3, 4},
    {5, 6}
  };

  std::vector<GraphAlgorithms::LinkInformation> symmetricHapticPincerLinks;
  GraphAlgorithms::LinkInformation a, b;
  a.indexPair = {0, 1};
  b.indexPair = {1, 2};

  symmetricHapticPincerLinks.push_back(std::move(a));
  symmetricHapticPincerLinks.push_back(std::move(b));

  auto symmetricHapticPincerRankedLigands = RankingInformation::rankLigands(
    symmetricHapticPincerLigands,
    symmetricHapticPincerRanking
  );

  BOOST_CHECK_MESSAGE(
    (symmetricHapticPincerRankedLigands == RaggedLigands {{0, 2}, {1}}),
    "Expected {{0, 2}, 1}, got " << temple::stringify(symmetricHapticPincerRankedLigands)
  );

  auto asymmetricHapticPincerRanking = RaggedAtoms {
    {1}, {6}, {2}, {5}, {3}, {4}
  };

  auto asymmetricHapticPincerLigands = RaggedAtoms {
    {1, 2},
    {3, 4},
    {5, 6}
  };

  std::vector<GraphAlgorithms::LinkInformation> asymmetricHapticPincerLinks;
  a.indexPair = {0, 1};
  b.indexPair = {1, 2};

  asymmetricHapticPincerLinks.push_back(std::move(a));
  asymmetricHapticPincerLinks.push_back(std::move(b));

  auto asymmetricHapticPincerRankedLigands = RankingInformation::rankLigands(
    asymmetricHapticPincerLigands,
    asymmetricHapticPincerRanking
  );

  BOOST_CHECK((asymmetricHapticPincerRankedLigands == RaggedLigands {{0}, {2}, {1}}));
}