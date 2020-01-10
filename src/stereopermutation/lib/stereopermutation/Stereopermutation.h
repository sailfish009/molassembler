/*!@file
 * @copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 * @brief Base class to describe substituent arrangements in shapes
 *
 * Contains the base class employed for describing the particular manner in
 * which substituents are arranged in various shapes.
 */

#ifndef INCLUDE_MOLASSEMBLER_STEREOPERMUTATIONS_STEREOPERMUTATION_H
#define INCLUDE_MOLASSEMBLER_STEREOPERMUTATIONS_STEREOPERMUTATION_H

#include "temple/Preprocessor.h"
#include "temple/OperatorSuppliers.h"

#include "boost/optional/optional_fwd.hpp"
#include "boost/functional/hash.hpp"

#include <vector>
#include <map>

namespace Scine {

//! @brief Data classes for permutational spatial arrangement modeling
namespace stereopermutation {

/*! @brief Represent abstract stereopermutation around atom center
 *
 * This class represents a simplified model of a sterically unique assignment
 * of a set of ligands to a stereocenter. It exists to uniquely identify the
 * steric configuration at this stereocenter, and provides methods to assist
 * a systematic generation of all possible configurations. It is generalized
 * over a number of shapes which are encoded in a separate library.
 */
class Stereopermutation : public temple::crtp::LexicographicComparable<Stereopermutation> {
public:
//!@name Member types
//!@{
  //! Type used to represent links between shape vertices
  using OrderedLinks = std::vector<
    std::pair<unsigned, unsigned>
  >;
//!@}

//!@name Static functions
//!@{
  /*! @brief Rotate characters
   *
   * @complexity{@math{\Theta(N)}}
   */
  static std::vector<char> permuteCharacters(
    const std::vector<char>& characters,
    const std::vector<unsigned>& permutation
  );

  /*! @brief Rotate links
   *
   * @complexity{@math{\Theta(L)}, not linear in shape size since it is small and constant}
   */
  static OrderedLinks permuteLinks(
    const OrderedLinks& links,
    const std::vector<unsigned>& permutation
  );
//!@}

//!@name Member data
//!@{
  //! Abstract representation of ranked substituents
  std::vector<char> characters;
  //! Links between characters
  OrderedLinks links;
//!@}

//!@name Special member functions
//!@{
  // Do not default instantiate
  Stereopermutation() = delete;
  /*!
   * @brief Construct an Stereopermutation from a list of ligand characters and
   *   a list of bonded indices referencing the ligand characters.
   *
   * @param passCharacters A vector of chars signifying abstract ligands.
   * @param passLinks A vector of pairs. Describes which ligand characters
   *  are bonded to one another.
   */
  Stereopermutation(
    std::vector<char> passCharacters,
    OrderedLinks passLinks = {}
  );
//!@}

//!@name Information
//!@{
  /*! @brief Applies a shape vertex permutation.
   *
   * @complexity{@math{O(N + L)}}
   */
  Stereopermutation applyPermutation(const std::vector<unsigned>& permutation) const;

  //!@brief Gets a map of ligand symbol character to shape vertex positions
  std::map<
    char,
    std::vector<unsigned>
  > getCharMap() const;

  //! A string for display
  std::string toString() const;
//!@}

//!@name Operators
//!@{
  //! Yields members tied to tuple for crtp operator suppliers
  inline auto tie() const {
    return std::tie(characters, links);
  }
//!@}
};

PURITY_WEAK std::size_t hash_value(const Stereopermutation& assignment);

} // namespace stereopermutation
} // namespace Scine

#endif
