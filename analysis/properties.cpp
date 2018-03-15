#include <algorithm>
#include <vector>
#include <iostream>
#include <cassert>
#include <functional>

#include "boost/program_options.hpp"
#include "temple/Stringify.h"

#include "stereopermutation/GenerateUniques.h"

std::ostream& nl(std::ostream& os) {
  os << '\n';
  return os;
}

using namespace stereopermutation;

int main(int argc, char* argv[]) {
  // Set up option parsing
  boost::program_options::options_description options_description("Recognized options");
  options_description.add_options()
    ("help", "Produce help message")
    (
      "s",
      boost::program_options::value<unsigned>(),
      "Set symmetry index"
    )
    (
      "c",
      boost::program_options::value<std::string>(),
      "Specify case characters"
    )
    (
      "l",
      boost::program_options::value<std::string>(),
      "Specify linking (remember to place quotation marks)"
    )
  ;

  // Parse
  boost::program_options::variables_map options_variables_map;
  boost::program_options::store(
    boost::program_options::parse_command_line(argc, argv, options_description),
    options_variables_map
  );
  boost::program_options::notify(options_variables_map);

  if(options_variables_map.count("help")) {
    std::cout << options_description << nl;
    return 0;
  }

  if(
    options_variables_map.count("s")
    && options_variables_map.count("c")
  ) {
    // Validate symmetry argument
    unsigned argSymmetry = options_variables_map["s"].as<unsigned>();
    if(argSymmetry >= Symmetry::allNames.size()) {
      std::cout << "Specified symmetry out of bounds. Valid symmetries are 0-"
        << (Symmetry::allNames.size() - 1) << ":\n\n";
      for(unsigned i = 0; i < Symmetry::allNames.size(); i++) {
        std::cout << "  " << i << " - " << Symmetry::name(Symmetry::allNames.at(i)) << "\n";
      }
      std::cout << std::endl;
      return 0;
    }

    Symmetry::Name symmetryName = Symmetry::allNames[argSymmetry];

    // Validate characters
    std::string chars = options_variables_map["c"].as<std::string>();

    if(chars.size() != Symmetry::size(symmetryName)) {
      std::cout << "Number of characters does not fit specified symmetry size: "
        << chars.size() << " characters specified, symmetry size is "
        << Symmetry::size(symmetryName) << nl;

      return 0;
    }

    std::vector<char> charVec;
    std::copy(
      chars.begin(),
      chars.end(),
      std::back_inserter(charVec)
    );

    // Validate links (if present)
    Stereopermutation::LinksSetType links;
    if(options_variables_map.count("l")) {
      /* Naive parse strategy: 
       * - remove all opening and closing brackets from the string
       * - split along commas, check size % 2 == 0
       * - parse non-overlapping pairwise as links
       */
      std::string linksString = options_variables_map["l"].as<std::string>();

      linksString.erase(
        std::remove_if(
          linksString.begin(),
          linksString.end(),
          [](const char& a) -> bool {
            return !(
              a == ',' || std::isdigit(a)
            );
          }
        ), 
        linksString.end()
      );

      std::vector<unsigned> linkIndices;

      std::stringstream ss;
      ss.str(linksString);
      std::string item;
      while(std::getline(ss, item, ',')) {
        linkIndices.push_back(std::stoul(item));
      }

      if(linkIndices.size() % 2 != 0) {
        std::cout << "The number of provided link indices is not even. You must "
          << "specify link pairs as pairwise indices for each link" << nl;
        return 0;
      }

      for(unsigned i = 0; i < linkIndices.size() / 2; ++i) {
        const auto& a = linkIndices.at(2 * i);
        const auto& b = linkIndices.at(2 * i + 1);

        if(a == b) {
          std::cout << "You have specified a link between identical indices!" << nl;
          return 0;
        }

        links.emplace(
          std::min(a, b),
          std::max(a, b)
        );
      }
    }

    // Generate the assignment
    Stereopermutation base {
      symmetryName,
      charVec,
      links
    };

    auto uniques = uniqueStereopermutationsWithWeights(
      base,
      symmetryName,
      false
    );

    std::cout << "Symmetry: " << Symmetry::name(symmetryName) << nl
      << "Characters: " << chars << nl
      << "Links: " << temple::stringify(links) << nl << nl;

    for(unsigned i = 0; i < uniques.assignments.size(); ++i) {
      std::cout << "Weight " << uniques.weights[i] << ": "
        << uniques.assignments[i]
        << ", link angles: ";

      for(const auto& linkPair : uniques.assignments[i].links) {
        std::cout << (
          180 * Symmetry::angleFunction(symmetryName)(linkPair.first, linkPair.second) / M_PI
        ) << " ";
      }
      std::cout << nl;
    }
  }

  return 0;
}
