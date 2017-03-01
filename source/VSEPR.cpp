#include "VSEPR.h"

/* TODO
 * - Changes needed to ensure correctness in complex environments:
 *   
 *   - concept of dative bonds is required
 *     -> How to detect if a bond is dative?
 */

namespace LocalGeometry {

Symmetry::Name VSEPR::determineGeometry(
  const Delib::ElementType& centerAtomType,
  const unsigned& nSites,
  const std::vector<LigandType>& ligands,
  const int& formalCharge
) {
  if(nSites <= 1) throw std::logic_error(
    "Don't use a model on terminal atoms! Single bonds don't "
    "have stereochemistry!"
  );

  if(
    std::any_of(
      ligands.begin(),
      ligands.end(),
      [](const auto& ligand) {
        return std::get<2>(ligand).size() > 1;
      }
    )
  ) throw std::logic_error(
    "The ligand set includes ligands with multiple atoms on a site!"
    " This is just VSEPR, there should be no eta bond situations!"
  );
  
  // get uncharged VE count
  auto VEOption = MoleculeManip::AtomInfo::mainGroupVE(centerAtomType);

  // return 
  if(!VEOption) {
    std::stringstream ss;
    ss << "You used VSEPR on a non-main group atom type: "
      << Delib::ElementInfo::symbol(centerAtomType)
      << "! That's not allowed.";
    throw std::logic_error(
      ss.str().c_str()
    );
  }

  // calculate X, E (VSEPR parameters)
  const unsigned& X = nSites;
  const int E = std::ceil(
    (
      static_cast<double>(VEOption.value()) 
      - formalCharge 
      - std::accumulate(
        ligands.begin(),
        ligands.end(),
        0.0,
        [](const double& carry, const auto& ligand) {
          /* can abort multiple ways:
           * vector front() is end() -> no ligands => API misuse
           * bondWeights has no entry for bty => error in bondWeights
           */
          return carry + bondWeights.at(
            std::get<2>(ligand).front().second
            // vec (pairs) ---^ pair -^ bty -^
          );
        }
      )
    ) / 2.0
  );

  if(E < 0) throw std::logic_error(
    "For some reason, E is < 0 in VSEPR. That shouldn't happen."
  );

  switch(X + E) {
    case 2: {
      return Symmetry::Name::Linear;
    }; break;
    case 3: {
      if(X == 3) return Symmetry::Name::TrigonalPlanar;
      else return Symmetry::Name::Bent;
    }; break;
    case 4: {
      if(X == 4) return Symmetry::Name::Tetrahedral;
      else if(X == 3) return Symmetry::Name::TrigonalPyramidal;
      else return Symmetry::Name::Bent;
    }; break;
    case 5: {
      if(X == 5) return Symmetry::Name::TrigonalBiPyramidal;
      else if(X == 4) return Symmetry::Name::Seesaw;
      else if(X == 3) return Symmetry::Name::TShaped;
      else return Symmetry::Name::Linear;
    }; break;
    case 6: {
      if(X == 6) return Symmetry::Name::Octahedral;
      else if(X == 5) return Symmetry::Name::SquarePyramidal;
      else return Symmetry::Name::SquarePlanar;
    }; break;
    case 7: {
      if(X == 7) return Symmetry::Name::PentagonalBiPyramidal;
      else if(X == 6) return Symmetry::Name::PentagonalPyramidal;
      else return Symmetry::Name::PentagonalPlanar;
    }; break;
    case 8: {
      return Symmetry::Name::SquareAntiPrismatic;
    }; break;
    default: {
      std::stringstream ss;
      ss << "Could not find a fitting symmetry for your X + E case: "
        << "X = " << X << ", E = " << E << ".";
      throw std::logic_error(
        ss.str().c_str()
      );
    }; break;
  }
}

} // eo namespace
