/*!@file
 * @copyright This code is licensed under the 3-clause BSD license.
 *   Copyright ETH Zurich, Laboratory of Physical Chemistry, Reiher Group.
 *   See LICENSE.txt for details.
 * @brief Calculates properties of molecules
 */

#ifndef INCLUDE_MOLASSEMBLER_DESCRIPTORS_H
#define INCLUDE_MOLASSEMBLER_DESCRIPTORS_H

#include "Molassembler/Export.h"

namespace Scine {
namespace Molassembler {

// Forward-declare molcule
class Molecule;

/*! @brief Calculates a number of freely rotatable bonds in a molecule
 *
 * The number of rotatable bonds is calculated as a bond-wise sum of
 * contributions that is rounded at the end:
 *
 * A bond can contribute to the number of rotatable bonds if
 * - It is of bond type Single
 * - Neither atom connected by the bond is terminal
 * - There is no assigned BondStereopermutator on the bond
 *
 * A bond meeting the prior criteria:
 * - If not part of a cycle, contributes a full rotatable bond to the sum
 * - If part of a cycle, contributes (S - 3) / S to the sum (where S is
 *   the cycle size).
 *
 * @complexity{@math{\Theta(B)} where @math{B} is the number of bonds}
 *
 * @warning The number of rotatable bonds is an unphysical descriptor and
 *   definitions differ across libraries. Take the time to read the algorithm
 *   description implemented here and do some testing. If need be, all
 *   information used by this algorithm is accessible from the Molecule
 *   interface, and a custom algorithm can be implemented.
 */
MASM_EXPORT unsigned numRotatableBonds(const Molecule& mol);

} // namespace Molassembler
} // namespace Scine

#endif
