#ifndef INCLUDE_MOLASSEMBLER_BOND_ORDERS_H
#define INCLUDE_MOLASSEMBLER_BOND_ORDERS_H

#include "Delib/BondOrderCollection.h"
#include "Delib/ElementTypeCollection.h"

#include "molassembler/detail/AngstromWrapper.h"

/*! @file
 *
 * Contains the interpretation of bond orders from 3D coordinates
 */

namespace molassembler {

//! Calculates a bond order collection via UFF-like bond distance modelling
Delib::BondOrderCollection uffBondOrders(
  const Delib::ElementTypeCollection& elements,
  const AngstromWrapper& angstromWrapper
);

} // namespace molassembler

#endif
