// SPDX-FileCopyrightText: 2015 Gregory Fiumara
// SPDX-License-Identifier: MIT

// Originally part of http://github.com/gfiumara/CIEDE2000 by Gregory Fiumara.

#include "CIEDE2000.h"

#define _USE_MATH_DEFINES
#include <cmath>

namespace CIEDE2000 {

constexpr double deg2Rad(double const deg)
{
  return (deg * (M_PI / 180.0));
}

double CIEDE2000(LAB const& lab1, LAB const& lab2)
{
  // "For these and all other numerical/graphical 􏰀delta E00 values
  // reported in this article, we set the parametric weighting factors
  // to unity(i.e., k_L = k_C = k_H = 1.0)." (Page 27).
  constexpr double k_L = 1.0, k_C = 1.0, k_H = 1.0;
  constexpr double deg360InRad = CIEDE2000::deg2Rad(360.0);
  constexpr double deg180InRad = CIEDE2000::deg2Rad(180.0);
  constexpr double pow25To7 = 6103515625.0; // pow(25, 7)

  //
  // Step 1
  //

  // Equation 2
  double C1 = std::sqrt((lab1.a * lab1.a) + (lab1.b * lab1.b));
  double C2 = std::sqrt((lab2.a * lab2.a) + (lab2.b * lab2.b));
  // Equation 3
  double barC = (C1 + C2) / 2.0;
  // Equation 4
  double G = 0.5 * (1 - std::sqrt(std::pow(barC, 7) / (std::pow(barC, 7) + pow25To7)));
  // Equation 5
  double a1Prime = (1.0 + G) * lab1.a;
  double a2Prime = (1.0 + G) * lab2.a;
  // Equation 6
  double CPrime1 = std::sqrt((a1Prime * a1Prime) + (lab1.b * lab1.b));
  double CPrime2 = std::sqrt((a2Prime * a2Prime) + (lab2.b * lab2.b));
  // Equation 7
  double hPrime1;
  if (lab1.b == 0 && a1Prime == 0)
    hPrime1 = 0.0;
  else
  {
    hPrime1 = std::atan2(lab1.b, a1Prime);
    // This must be converted to a hue angle in degrees between 0 and 360 by addition of 2􏰏 to negative hue angles.
    if (hPrime1 < 0)
      hPrime1 += deg360InRad;
  }
  double hPrime2;
  if (lab2.b == 0 && a2Prime == 0)
    hPrime2 = 0.0;
  else
  {
    hPrime2 = atan2(lab2.b, a2Prime);
    // This must be converted to a hue angle in degrees between 0 and 360 by addition of 2􏰏 to negative hue angles.
    if (hPrime2 < 0)
      hPrime2 += deg360InRad;
  }

  //
  // Step 2
  //

  // Equation 8
  double deltaLPrime = lab2.l - lab1.l;
  // Equation 9
  double deltaCPrime = CPrime2 - CPrime1;
  // Equation 10
  double deltahPrime;
  double CPrimeProduct = CPrime1 * CPrime2;
  if (CPrimeProduct == 0)
    deltahPrime = 0;
  else
  {
    // Avoid the fabs() call.
    deltahPrime = hPrime2 - hPrime1;
    if (deltahPrime < -deg180InRad)
      deltahPrime += deg360InRad;
    else if (deltahPrime > deg180InRad)
      deltahPrime -= deg360InRad;
  }
  // Equation 11
  double deltaHPrime = 2.0 * std::sqrt(CPrimeProduct) * std::sin(deltahPrime / 2.0);

  //
  // Step 3
  //

  // Equation 12
  double barLPrime = (lab1.l + lab2.l) / 2.0;
  // Equation 13
  double barCPrime = (CPrime1 + CPrime2) / 2.0;
  // Equation 14
  double hPrimeSum = hPrime1 + hPrime2;
  double barhPrime;
  if (CPrime1 * CPrime2 == 0)
    barhPrime = hPrimeSum;
  else
  {
    if (fabs(hPrime1 - hPrime2) <= deg180InRad)
      barhPrime = hPrimeSum / 2.0;
    else
    {
      if (hPrimeSum < deg360InRad)
        barhPrime = (hPrimeSum + deg360InRad) / 2.0;
      else
        barhPrime = (hPrimeSum - deg360InRad) / 2.0;
    }
  }
  // Equation 15
  double T = 1.0 - (0.17 * std::cos(barhPrime - deg2Rad(30.0))) + (0.24 * std::cos(2.0 * barhPrime)) + (0.32 * std::cos((3.0 * barhPrime) + deg2Rad(6.0))) -
             (0.20 * std::cos((4.0 * barhPrime) - deg2Rad(63.0)));
  // Equation 16
  double deltaTheta = deg2Rad(30.0) * std::exp(-std::pow((barhPrime - deg2Rad(275.0)) / deg2Rad(25.0), 2.0));
  // Equation 17
  double R_C = 2.0 * std::sqrt(std::pow(barCPrime, 7.0) / (std::pow(barCPrime, 7.0) + pow25To7));
  // Equation 18
  double S_L = 1 + ((0.015 * std::pow(barLPrime - 50.0, 2.0)) / std::sqrt(20 + std::pow(barLPrime - 50.0, 2.0)));
  // Equation 19
  double S_C = 1 + (0.045 * barCPrime);
  // Equation 20
  double S_H = 1 + (0.015 * barCPrime * T);
  // Equation 21
  double R_T = (-std::sin(2.0 * deltaTheta)) * R_C;

  // Equation 22
  double deltaE = std::sqrt(std::pow(deltaLPrime / (k_L * S_L), 2.0) + std::pow(deltaCPrime / (k_C * S_C), 2.0) + std::pow(deltaHPrime / (k_H * S_H), 2.0) +
                            (R_T * (deltaCPrime / (k_C * S_C)) * (deltaHPrime / (k_H * S_H))));

  return deltaE;
}

} // namespace CIEDE2000
