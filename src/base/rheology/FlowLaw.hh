// Copyright (C) 2004-2015 Jed Brown, Ed Bueler, and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef __flowlaws_hh
#define __flowlaws_hh

#include <string>

#include "base/enthalpyConverter.hh"

namespace pism {

class IceModelVec2S;
class IceModelVec3;

class Config;

// This uses the definition of squared second invariant from Hutter and several others, namely the output is
// \f$ D^2 = \frac 1 2 D_{ij} D_{ij} \f$ where incompressibility is used to compute \f$ D_{zz} \f$
static inline double secondInvariant_2D(double u_x, double u_y,
                                        double v_x, double v_y) {
  return 0.5 * (u_x * u_x + v_y * v_y + (u_x + v_y)*(u_x + v_y) + 0.5*(u_y + v_x)*(u_y + v_x));
}

// The squared second invariant of a symmetric strain rate tensor in compressed form [u_x, v_y, 0.5(u_y+v_x)]
static inline double secondInvariantDu_2D(const double Du[]) {
  return 0.5 * (Du[0] * Du[0] + Du[1] * Du[1] + (Du[0] + Du[1]) * (Du[0] + Du[1]) + 2.0 * Du[2] * Du[2]);
}

//! Ice flow laws.
namespace rheology {

//! Abstract class containing the constitutive relation for the flow of ice (of
//! the Paterson-Budd type).
/*!
  This is the interface which most of PISM uses for rheology.

  The current implementation of stress-balance computations in PISM restrict
  possible choices of rheologies to ones that

  - are power laws

  - allow factoring out a temperature- (or enthalpy-) dependent ice hardness
    factor

  - can be represented in the viscosity form

  @note FlowLaw derived classes should implement hardness... in
  terms of softness... That way in many cases we only need to
  re-implement softness... to turn one flow law into another.
*/
class FlowLaw {
public:
  FlowLaw(const std::string &prefix, const Config &config,
          EnthalpyConverter::Ptr EC);
  virtual ~FlowLaw();

  void effective_viscosity(double hardness, double gamma,
                           double *nu, double *dnu) const;

  std::string name() const;
  double exponent() const;
  double enhancement_factor() const;
  EnthalpyConverter::Ptr EC() const;

  double hardness(double E, double p) const;
  double softness(double E, double p) const;
  double flow(double stress, double E,
              double pressure, double grainsize) const;

protected:
  virtual double flow_impl(double stress, double E,
                           double pressure, double grainsize) const;
  virtual double hardness_impl(double E, double p) const;
  virtual double softness_impl(double E, double p) const = 0;

protected:
  std::string m_name;

  double m_rho,          //!< ice density
    m_beta_CC_grad, //!< Clausius-Clapeyron gradient
    m_melting_point_temp;  //!< for water, 273.15 K
  EnthalpyConverter::Ptr m_EC;

  double softness_paterson_budd(double T_pa) const;

  double m_schoofLen, m_schoofVel, m_schoofReg, m_viscosity_power,
    m_hardness_power,
    m_A_cold, m_A_warm, m_Q_cold, m_Q_warm,  // see Paterson & Budd (1982)
    m_crit_temp;

  double m_standard_gravity,
    m_ideal_gas_constant,
    m_e,                          // flow enhancement factor
    m_n;                          // power law exponent
};

double averaged_hardness(const FlowLaw &ice,
                         double ice_thickness,
                         int kbelowH,
                         const double *zlevels,
                         const double *enthalpy);

void averaged_hardness_vec(const FlowLaw &ice,
                           const IceModelVec2S &ice_thickness,
                           const IceModelVec3  &enthalpy,
                           IceModelVec2S &result);

// Helper functions:
bool FlowLawUsesGrainSize(FlowLaw *);

} // end of namespace rheology
} // end of namespace pism

#endif // __flowlaws_hh
