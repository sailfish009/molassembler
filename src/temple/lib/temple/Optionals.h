/*!@file
 * @copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 * @brief Helper functions for dealing with optional types
 *
 * Implements optional-returning function composition syntactic sugar to avoid
 * repetitive patterns when dealing with lots of optionals.
 */

#ifndef INCLUDE_MOLASSEMBLER_TEMPLE_OPTIONALS_H
#define INCLUDE_MOLASSEMBLER_TEMPLE_OPTIONALS_H

#include <boost/optional.hpp>
#include "temple/Traits.h"

namespace Scine {
namespace temple {
namespace optionals {

/*! @brief Monadic bind with function of signature T -> U
 *
 * @tparam UnaryFunction: Function of signature T -> U
 * @tparam OptionalType: Optional type being used
 *
 * @returns OptionalType<U>
 */
template<
  template<typename> class OptionalType,
  typename T,
  class UnaryFunction
>
auto map(const OptionalType<T>& optional, UnaryFunction&& function) {
  using U = decltype(function(std::declval<T>()));

  if(optional) {
    return OptionalType<U> {function(optional.value())};
  }

  return OptionalType<U> {};
}

/*! @brief Monadic bind with function of signature T -> Optional<U>
 *
 * @tparam UnaryFunction: Function of signature T -> Optional<U>
 * @tparam OptionalType: Passed optional type
 *
 * @returns OptionalType<U>
 */
template<
  template<typename> class OptionalType,
  typename T,
  class UnaryFunction
>
auto flatMap(const OptionalType<T>& optional, UnaryFunction&& function) {
  using OptionalU = decltype(function(std::declval<T>()));

  if(optional) {
    return OptionalU {function(optional.value())};
  }

  return OptionalU {};
}

} // namespace optionals
} // namespace temple
} // namespace Scine

#endif
