/*!@file
 * @copyright This code is licensed under the 3-clause BSD license.
 *   Copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 */

#define BOOST_SYSTEM_NO_DEPRECATED
#define BOOST_FILESYSTEM_NO_DEPRECATED
#include "boost/filesystem.hpp"
#include "boost/program_options.hpp"

#include "molassembler/Temple/Functional.h"
#include "molassembler/Temple/Stringify.h"

#include "molassembler/Shapes/Data.h"
#include "molassembler/Shapes/PropertyCaching.h"

#include <fstream>
#include <iomanip>
#include <iostream>

const std::array<unsigned, 4> distortionColumns {{8, 8, 8, 30}};
const std::array<unsigned, 3> shapeColumns {{5, 5, 25}};
const std::array<unsigned, 4> ambiguityColumns {{10, 25, 25, 4}};

using namespace Scine;

std::ostream& nl(std::ostream& out) {
  out << '\n';
  return out;
}

std::string condense(const std::vector<unsigned>& indexVector) {
  using namespace std::string_literals;
  return "{"s + temple::condense(indexVector) + "}"s;
}

std::ostream& operator << (
  std::ostream& out,
  const shapes::properties::DistortionInfo& distortion
) {
  out << std::setw(distortionColumns[0]) << distortion.angularDistortion
    << std::setw(distortionColumns[1]) << distortion.chiralDistortion
    << std::setw(distortionColumns[2]) << (distortion.angularDistortion + distortion.chiralDistortion)
    << std::setw(distortionColumns[3]) << condense(distortion.indexMapping);

  return out;
}

void printMappingsHeader() {
  std::cout << std::setw(distortionColumns[0]) << "Angular"
    << std::setw(distortionColumns[1]) << "Chiral"
    << std::setw(distortionColumns[2]) << "Total"
    << std::setw(distortionColumns[3]) << "Mapping" << nl;
}

void printPermissibleSymmetries() {
  std::cout << std::setw(shapeColumns[0]) << "Idx"
    << std::setw(shapeColumns[1]) << "Size"
    << std::setw(shapeColumns[2]) << "Name"
    << nl;

  for(unsigned i = 0; i < shapes::allShapes.size(); i++) {
    std::cout << std::setw(shapeColumns[0]) << i
      << std::setw(shapeColumns[1]) << shapes::size(shapes::allShapes.at(i))
      << std::setw(shapeColumns[2]) << shapes::name(shapes::allShapes.at(i))
      << nl;
  }

  std::cout << std::endl;
}

void writeDistortions(
  const std::vector<shapes::properties::DistortionInfo>& distortions
) {
  std::cout << std::fixed << std::setprecision(2);

  for(const auto& distortion : distortions) {
    std::cout << distortion << nl;
  }
}

double calculateAmbiguity(
  const std::vector<shapes::properties::DistortionInfo>& distortions
) {
  /* Some measure between 0 and 1 that indicates how ambiguous choosing the
   * lowest mapping is.
   */

  auto sortByTotalView = temple::sort(
    distortions,
    [](const auto& a, const auto& b) -> bool {
      return (
        (a.angularDistortion + a.chiralDistortion)
        < (b.angularDistortion + b.chiralDistortion)
      );
    }
  );

  if(distortions.size() <= 1) {
    return 0;
  }

  auto firstValue = (
    sortByTotalView.at(0).angularDistortion
    + sortByTotalView.at(0).chiralDistortion
  );
  auto secondValue = (
    sortByTotalView.at(1).angularDistortion
    + sortByTotalView.at(1).chiralDistortion
  );

  if(std::fabs(secondValue - firstValue) < 1e-10) {
    return 1;
  }

  return firstValue / secondValue;
}

struct AmbiguityEntry {
  double ambiguity;
  shapes::Shape source, target;
  boost::optional<shapes::Vertex> deletedVertex;

  AmbiguityEntry(
    const double passAmbiguity,
    const shapes::Shape passSource,
    const shapes::Shape passTarget,
    boost::optional<shapes::Vertex> passDeletedIndex = boost::none
  ) : ambiguity(passAmbiguity),
      source(passSource),
      target(passTarget),
      deletedVertex(std::move(passDeletedIndex))
  {}
};

int main(int argc, char* argv[]) {
/* Set program options from command-line arguments */
  // Set up option parsing
  boost::program_options::options_description options_description("Recognized options");
  options_description.add_options()
    ("help", "Produce help message")
    ("s", boost::program_options::value<unsigned>(), "Source shape index")
    ("t", boost::program_options::value<unsigned>(), "Target shape index")
    ("a", boost::program_options::value<bool>(), "Calculate ambiguity of lowest mappings")
  ;

  // Parse
  boost::program_options::variables_map options_variables_map;
  boost::program_options::store(
    boost::program_options::parse_command_line(argc, argv, options_description),
    options_variables_map
  );
  boost::program_options::notify(options_variables_map);

  // Manage the results
  if(options_variables_map.count("help") > 0) {
    std::cout << options_description << nl
      << "The following shape indices are permissible:" << nl;
    printPermissibleSymmetries();
    return 0;
  }

  if(
    options_variables_map.count("s") > 0
    && options_variables_map.count("t") > 0
  ) {
    unsigned sourceShapeArg = options_variables_map["s"].as<unsigned>();
    unsigned targetShapeArg = options_variables_map["t"].as<unsigned>();

    if(
      sourceShapeArg >= shapes::allShapes.size()
      || targetShapeArg >= shapes::allShapes.size()
    ) {
      std::cout << "Specified shape out of bounds. Valid symmetries:" << nl << nl;
      printPermissibleSymmetries();
      return 1;
    }

    if(sourceShapeArg == targetShapeArg) {
      std::cout << "The source and target shape may not be identical." << nl << nl;
      printPermissibleSymmetries();
      return 1;
    }

    shapes::Shape sourceShape(shapes::allShapes.at(sourceShapeArg)),
                   targetShape(shapes::allShapes.at(targetShapeArg));

    int diff = (
      static_cast<int>(shapes::size(targetShape))
      - static_cast<int>(shapes::size(sourceShape))
    );

    if(std::abs(diff) > 1) {
      std::cout << "The selected symmetries must be adjacent in size!" << nl << nl;
      printPermissibleSymmetries();
      return 1;
    }

    const auto distortionComparator = [](const auto& a, const auto& b) -> bool {
      return (
        std::tie(a.angularDistortion, a.chiralDistortion)
        > std::tie(b.angularDistortion, b.chiralDistortion)
      );
    };

    if(diff == 1 || diff == 0) {
      const auto distortions = temple::sort(
        shapes::properties::shapeTransitionMappings(
          sourceShape,
          targetShape
        ),
        distortionComparator
      );

      printMappingsHeader();
      writeDistortions(distortions);
    } else {
      for(shapes::Vertex i {0}; i < shapes::size(sourceShape); ++i) {
        const auto distortions = temple::sort(
            shapes::properties::ligandLossTransitionMappings(
            sourceShape,
            targetShape,
            i
          ),
          distortionComparator
        );

        printMappingsHeader();
        writeDistortions(distortions);
        std::cout << nl << nl;
      }
    }
  }

  if(options_variables_map.count("a") > 0) {
    std::vector<AmbiguityEntry> ambiguities;

    for(const auto& sourceShape : shapes::allShapes) {
      for(const auto& targetShape : shapes::allShapes) {
        if(sourceShape == targetShape) {
          // Skip identity mapping
          continue;
        }

        int diff = (
          static_cast<int>(shapes::size(targetShape))
          - static_cast<int>(shapes::size(sourceShape))
        );

        if(diff == 1 || diff == 0) {
          auto distortions = shapes::properties::shapeTransitionMappings(
            sourceShape,
            targetShape
          );

          auto ambiguity = calculateAmbiguity(distortions);

          if(0.0 < ambiguity && ambiguity < 1.0) {
            ambiguities.emplace_back(
              ambiguity,
              sourceShape,
              targetShape
            );
          }
        } else if(diff == -1) {
          for(shapes::Vertex i {0}; i < shapes::size(sourceShape); ++i) {
            auto distortions = shapes::properties::ligandLossTransitionMappings(
              sourceShape,
              targetShape,
              i
            );

            auto ambiguity = calculateAmbiguity(distortions);

            if(0.0 < ambiguity && ambiguity < 1.0) {
              ambiguities.emplace_back(
                ambiguity,
                sourceShape,
                targetShape,
                i
              );
            }
          }
        }
      }
    }

    temple::inplace::sort(
      ambiguities,
      [](const auto& a, const auto& b) -> bool {
        return a.ambiguity < b.ambiguity;
      }
    );

    std::cout
      << "Ambiguity tries to quantify how bad choosing the index mapping with the" << nl
      << "lowest total distortion is over considering the next best index mapping." << nl
      << "Zero indicates that the choice is unambiguous, one that the choice is " << nl
      << "completely ambiguous. Ambiguity values excluding zero and one are shown" << nl
      << "(both are common). Idx is the index that is deleted when the target" << nl
      << "shape is smaller than the source shape." << nl << nl;


    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(ambiguityColumns[0]) << "Ambiguity"
      << std::setw(ambiguityColumns[1]) << "Source"
      << std::setw(ambiguityColumns[2]) << "Target"
      << std::setw(ambiguityColumns[3]) << "Idx" << nl;

    for(const auto& entry : ambiguities) {
      std::cout << std::setw(ambiguityColumns[0]) << entry.ambiguity
        << std::setw(ambiguityColumns[1]) << shapes::name(entry.source)
        << std::setw(ambiguityColumns[2]) << shapes::name(entry.target)
        << std::setw(ambiguityColumns[3]);

      if(entry.deletedVertex) {
        std::cout << entry.deletedVertex.value();
      } else {
        std::cout << " ";
      }

      std::cout << nl;
    }
  }

  return 0;
}