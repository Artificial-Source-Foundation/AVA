// SPDX-FileCopyrightText: 2015 Gregory Fiumara
// SPDX-License-Identifier: MIT

// Originally part of http://github.com/gfiumara/CIEDE2000 by Gregory Fiumara.

#pragma once

#include "ava/debug/print_members_on.h"
#include "utils/has_print_on.h"

namespace CIEDE2000 {
// This class defines a print_on method.
using utils::has_print_on::operator<<;

// A color in CIELAB colorspace.
struct LAB
{
  double l;     // Lightness
  double a;     // Color-opponent a dimension.
  double b;     // Color-opponent b dimension.

  AVA_DEBUG_PRINT_MEMBERS_ON
};

//
// Obtain Delta-E 2000 value.
//
// Based on the paper "The CIEDE2000 Color-Difference Formula:
// Implementation Notes, Supplementary Test Data, and Mathematical
// Observations" by Gaurav Sharma, Wencheng Wu, and Edul N. Dalal,
// from http://www.ece.rochester.edu/~gsharma/ciede2000/.
//
// Returns Delta-E difference between lab1 and lab2.
//
double CIEDE2000(LAB const& lab1, LAB const& lab2);

} // namespace CIEDE2000
