// Copyright (C) 2004-2010 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
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

#include <cmath>
#include <cstring>
#include <petscda.h>
#include "iceModel.hh"


//! Compute vector driving stress at base of ice on the regular grid.
/*!
Computes the driving stress at the base of the ice:
   \f[ \tau_d = - \rho g H \nabla h \f]

If transformForSurfaceGradient is TRUE then the surface gradient
\f$\nabla h\f$ is computed by the gradient of the
transformed variable  \f$\eta= H^{(2n+2)/n}\f$ (frequently, \f$\eta= H^{8/3}\f$).
Because this quantity is more regular at ice sheet margins, we get a 
better surface gradient.  When the thickness at a grid point is very small
(below \c minThickEtaTransform in the procedure), the formula is slightly 
modified to give a lower driving stress.

In floating parts the surface gradient is always computed by the regular formula.
 
Saves it in user supplied Vecs \c vtaudx and \c vtaudy, which are treated 
as global.  (I.e. we do not communicate ghosts.)
 */
PetscErrorCode IceModel::computeDrivingStress(IceModelVec2 &vtaudx, IceModelVec2 &vtaudy) {
  PetscErrorCode ierr;

  const PetscScalar n       = ice->exponent(), // frequently n = 3
                    etapow  = (2.0 * n + 2.0)/n,  // = 8/3 if n = 3
                    invpow  = 1.0 / etapow,  // = 3/8
                    dinvpow = (- n - 2.0) / (2.0 * n + 2.0); // = -5/8
  const PetscScalar minThickEtaTransform = 5.0; // m
  const PetscScalar dx=grid.dx, dy=grid.dy;

  bool compute_surf_grad_inward_ssa = config.get_flag("compute_surf_grad_inward_ssa");

  ierr =    vh.begin_access();    CHKERRQ(ierr);
  ierr =    vH.begin_access();  CHKERRQ(ierr);
  ierr =  vbed.begin_access();  CHKERRQ(ierr);
  ierr = vMask.begin_access();  CHKERRQ(ierr);

  ierr = vtaudx.begin_access(); CHKERRQ(ierr);
  ierr = vtaudy.begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscScalar pressure = ice->rho * standard_gravity * vH(i,j);
      if (pressure <= 0.0) {
        vtaudx(i,j) = 0.0;
        vtaudy(i,j) = 0.0;
      } else {
        PetscScalar h_x = 0.0, h_y = 0.0;
	// FIXME: we need to handle grid periodicity correctly.
        if (vMask.is_grounded(i,j) && (transformForSurfaceGradient == PETSC_TRUE)) {
	  // in grounded case, differentiate eta = H^{8/3} by chain rule
	  if (vH(i,j) > 0.0) {
	    const PetscScalar myH = (vH(i,j) < minThickEtaTransform) ?
	      minThickEtaTransform : vH(i,j);
	    const PetscScalar eta = pow(myH, etapow), factor = invpow * pow(eta, dinvpow);
	    h_x = factor * (pow(vH(i+1,j),etapow) - pow(vH(i-1,j),etapow)) / (2*dx);
	    h_y = factor * (pow(vH(i,j+1),etapow) - pow(vH(i,j-1),etapow)) / (2*dy);
	  }
	  // now add bed slope to get actual h_x,h_y
	  // FIXME: there is no reason to assume user's bed is periodized; see vertical
	  //   velocity computation
	  h_x += vbed.diff_x(i,j);
	  h_y += vbed.diff_y(i,j);
        } else {  // floating or eta transformation is not used
          if (compute_surf_grad_inward_ssa) {
	    h_x = vh.diff_x_p(i,j);
	    h_y = vh.diff_y_p(i,j);
          } else {
	    h_x = vh.diff_x(i,j);
	    h_y = vh.diff_y(i,j);
	  }
        }

        vtaudx(i,j) = - pressure * h_x;
        vtaudy(i,j) = - pressure * h_y;
      }
    }
  }

  ierr =   vbed.end_access(); CHKERRQ(ierr);
  ierr =     vh.end_access(); CHKERRQ(ierr);
  ierr =     vH.end_access(); CHKERRQ(ierr);
  ierr =  vMask.end_access(); CHKERRQ(ierr);
  ierr = vtaudx.end_access(); CHKERRQ(ierr);
  ierr = vtaudy.end_access(); CHKERRQ(ierr);

  return 0;
}


//! Update the surface elevation and the flow-type mask when the geometry has changed.
/*!
This procedure should be called whenever necessary to maintain consistency of geometry.

For instance, it should be called when either ice thickness or bed elevation change. 
In particular we always want \f$h = H + b\f$ to apply at grounded points, and, on the
other hand, we want the mask to reflect that the ice is floating if the floatation 
criterion applies at a point.

There is one difficult case.  When a point was floating and becomes grounded we generally
do not know whether to mark it as \c MASK_SHEET so that the SIA applies or \c MASK_DRAGGING
so that the SSA applies.  For now there is a vote-by-neighbors scheme (among the grounded 
neighbors).  When the \c MASK_DRAGGING points have plastic till bases this is not an issue.
 */
PetscErrorCode IceModel::updateSurfaceElevationAndMask() {
  PetscErrorCode ierr;
  PetscScalar **h, **bed, **H, **mask;

  if (ocean == PETSC_NULL) {  SETERRQ(1,"PISM ERROR: ocean == PETSC_NULL");  }
  PetscReal currentSeaLevel;
  ierr = ocean->sea_level_elevation(grid.year, dt / secpera, currentSeaLevel); CHKERRQ(ierr);

  bool is_dry_simulation = config.get_flag("is_dry_simulation"),
    do_plastic_till = config.get_flag("do_plastic_till"),
    use_ssa_velocity = config.get_flag("use_ssa_velocity");

  double ocean_rho = config.get("sea_water_density");

  ierr =    vh.get_array(h);    CHKERRQ(ierr);
  ierr =    vH.get_array(H);    CHKERRQ(ierr);
  ierr =  vbed.get_array(bed);  CHKERRQ(ierr);
  ierr = vMask.get_array(mask); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // take this opportunity to check that vH(i,j) >= 0
      if (vH(i,j) < 0) {
        SETERRQ2(1,"Thickness negative at point i=%d, j=%d",i,j);
      }

      const PetscScalar hgrounded = bed[i][j] + vH(i,j),
                        hfloating = currentSeaLevel + (1.0 - ice->rho/ocean_rho) * vH(i,j);

      if (is_dry_simulation) {
        // Don't update mask; potentially one would want to do SSA
        //   dragging ice shelf in dry case and/or ignore mean sea level elevation.
        h[i][j] = hgrounded;
      } else if (vMask.value(i,j) == MASK_OCEAN_AT_TIME_0) {
        // mask takes priority over bed in this case (note sea level may change).
        // Example Greenland case: if mask say Ellesmere is OCEAN0,
        //   then never want ice on Ellesmere.
        // If mask says OCEAN0 then don't change the mask and also don't change
        // the thickness; massContExplicitStep() is in charge of that.
        // Almost always the next line is equivalent to h[i][j] = 0.
        h[i][j] = hfloating;  // ignore bed and treat it like deep ocean
      } else {
        if (vMask.is_floating(i,j)) {
          // check whether you are actually floating or grounded
          if (hgrounded > hfloating+1.0) {
            h[i][j] = hgrounded; // actually grounded so update h
            if (use_ssa_velocity) {
              if (do_plastic_till) {
                // we are using SSA-as-a-sliding-law, so we know what to do:
                //   all grounded points become DRAGGING
                mask[i][j] = MASK_DRAGGING_SHEET;
              } else {
                // we do not know how to set this point, which just became grounded
                mask[i][j] = MASK_UNKNOWN;
              }
            } else {
              // we do not have any ice handled by SSA, so it must be SHEET
              mask[i][j] = MASK_SHEET;
            }
          } else {
            h[i][j] = hfloating; // actually floating so update h
          }
        } else { 
          // mask says it is grounded, so set everything according to 
          //   newly-evaluated flotation criterion
          if (hgrounded > hfloating-1.0) {
            h[i][j] = hgrounded; // actually grounded so update h
            if (use_ssa_velocity && do_plastic_till) {
              // we are using SSA-as-a-sliding-law, so grounded points become DRAGGING
              mask[i][j] = MASK_DRAGGING_SHEET;
            }
          } else {
            h[i][j] = hfloating; // actually floating so update h
            mask[i][j] = MASK_FLOATING;
          }
        }

        // deal with the confusing case, which is when it is grounded,
        //   it was marked FLOATING, and the user wants some SIA points
        //   and some SSA points
        if (vMask.value(i,j) == MASK_UNKNOWN) {
          // determine type of grounded ice by vote-by-neighbors
          //   (BOX stencil neighbors!):
	  // FIXME: this should be made clearer
          const PetscScalar neighmasksum = 
                PismModMask(mask[i-1][j+1]) + PismModMask(mask[i][j+1]) + PismModMask(mask[i+1][j+1]) +
                PismModMask(mask[i-1][j])   +                           + PismModMask(mask[i+1][j])  +
                PismModMask(mask[i-1][j-1]) + PismModMask(mask[i][j-1]) + PismModMask(mask[i+1][j-1]);
          // make SHEET if either all neighbors are SHEET or at most one is 
          //   DRAGGING; if any are floating then ends up DRAGGING:
          if (neighmasksum <= (7*MASK_SHEET + MASK_DRAGGING_SHEET + 0.1)) { 
            mask[i][j] = MASK_SHEET;
          } else { // otherwise make DRAGGING
            mask[i][j] = MASK_DRAGGING_SHEET;
          }
        }
        
      }

    }
  }

  ierr =         vh.end_access(); CHKERRQ(ierr);
  ierr =         vH.end_access(); CHKERRQ(ierr);
  ierr =       vbed.end_access(); CHKERRQ(ierr);
  ierr =      vMask.end_access(); CHKERRQ(ierr);

  ierr = vh.beginGhostComm(); CHKERRQ(ierr);
  ierr = vh.endGhostComm(); CHKERRQ(ierr);

  ierr = vMask.beginGhostComm(); CHKERRQ(ierr);
  ierr = vMask.endGhostComm(); CHKERRQ(ierr);
  return 0;
}


//! Update the thickness from the horizontal velocity and the surface and basal mass balance.
/*! 
The partial differential equation describing the conservation of mass in the map-plane
(parallel to the geoid) is
  \f[ \frac{\partial H}{\partial t} = M - S - \nabla\cdot \mathbf{q} \f]
where 
  \f[ \mathbf{q} = \bar{\mathbf{U}} H. \f]
In these equations \f$H\f$ is the ice thickness, 
\f$M\f$ is the surface mass balance (accumulation or ablation), \f$S\f$ is the basal 
mass balance (e.g. basal melt or freeze-on), and \f$\bar{\mathbf{U}}\f$ is the vertically-averaged
horizontal velocity of the ice.  This procedure uses conservation of mass to update the ice thickness.

The PISMSurfaceModel pointed to by IceModel::surface provides \f$M\f$.  The PISMOceanModel
pointed to by IceModel::ocean provides \f$S\f$ at locations below floating ice (ice shelves).

The map-plane flux of the ice \f$\mathbf{q}\f$ is defined by the above formula.  Nonetheless
the mass flux is split into the parts caused by non-sliding SIA-type deformation and 
caused by a nonzero basal sliding velocity:
  \f[ \mathbf{q} = - D \nabla h + \mathbf{U}_b H.\f]
Here \f$D\f$ is the (positive, scalar) effective diffusivity of the SIA and 
\f$\mathbf{U}_b\f$ is the basal sliding velocity.

The methods used are first-order explicit in time.  The derivatives in 
\f$\nabla \cdot \mathbf{q}\f$ are computed by centered finite difference methods.  In the case 
of the SIA contribution, the value of \f$D \nabla h\f$ is already stored in 
\c Vec \c vuvbar on the staggered grid by velocitySIAStaggered().  It is differenced in 
the standard centered manner (with averaging of the thickness onto the staggered grid).

Basal sliding may come from SSA or from a sliding law in SIA (the latter is usually inferior as a
physical model).  The divergence of \f$\mathbf{U}_b H\f$ is computed by upwinding after expanding
  \f[ \nabla\cdot (\mathbf{U}_b H) = \mathbf{U}_B \cdot \nabla H + (\nabla \cdot \mathbf{U}_B) H.\f]
That is, in the case of pure basal sliding the mass conservation equation is regarded as an 
advection equation with source term,
  \f[ \frac{\partial H}{\partial t} + \mathbf{U}_b \cdot \nabla H 
                             = M - S - (\nabla \cdot \mathbf{U}_b) H.\f]
The product of velocity and the gradient of thickness on the left is computed by first-order
upwinding.  Note that the CFL condition for this advection scheme is checked; see 
computeMax2DSlidingSpeed() and determineTimeStep().

Note that if the point is flagged as \c FLOATING_OCEAN0 then the thickness is set to
zero.  Note that the rate of thickness change \f$\partial H/\partial t\f$ is computed and saved,
as is the rate of volume loss or gain.
 */
PetscErrorCode IceModel::massContExplicitStep() {
  PetscErrorCode ierr;

  const PetscScalar   dx = grid.dx, dy = grid.dy;
  bool do_ocean_kill = config.get_flag("ocean_kill"),
    floating_ice_killed = config.get_flag("floating_ice_killed"),
    include_bmr_in_continuity = config.get_flag("include_bmr_in_continuity"),
    do_superpose = config.get_flag("do_superpose");

  if (surface != NULL) {
    ierr = surface->ice_surface_mass_flux(grid.year, dt / secpera, acab); CHKERRQ(ierr);
  } else { SETERRQ(1,"PISM ERROR: surface == NULL"); }

  if (ocean != NULL) {
    ierr = ocean->shelf_base_mass_flux(grid.year, dt / secpera, shelfbmassflux); CHKERRQ(ierr);
  } else { SETERRQ(2,"PISM ERROR: ocean == NULL"); }

  const PetscScalar inC_fofv = 1.0e-4 * PetscSqr(secpera),
                    outC_fofv = 2.0 / pi;

  IceModelVec2 vHnew = vWork2d[0];
  ierr = vH.copy_to(vHnew); CHKERRQ(ierr);

  PetscScalar **uvbar[2], **ubarssa, **vbarssa, **ub, **vb,
    **bmr_gnded;

  ierr = vH.begin_access(); CHKERRQ(ierr);
  ierr = vbasalMeltRate.get_array(bmr_gnded); CHKERRQ(ierr);
  ierr = vuvbar[0].get_array(uvbar[0]); CHKERRQ(ierr);
  ierr = vuvbar[1].get_array(uvbar[1]); CHKERRQ(ierr);
  ierr = vub.get_array(ub); CHKERRQ(ierr);
  ierr = vvb.get_array(vb); CHKERRQ(ierr);
  ierr = acab.begin_access(); CHKERRQ(ierr);
  ierr = shelfbmassflux.begin_access(); CHKERRQ(ierr);
  ierr = vubarSSA.get_array(ubarssa); CHKERRQ(ierr);
  ierr = vvbarSSA.get_array(vbarssa); CHKERRQ(ierr);
  ierr = vMask.begin_access();  CHKERRQ(ierr);
  ierr = vHnew.begin_access(); CHKERRQ(ierr);

  PetscScalar icecount = 0.0;
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (vH(i,j) > 0.0)  icecount++;

      // get thickness averaged onto staggered grid;
      //    note Div Q = Div (- f(v) D grad h + (1-f(v)) U_b H) 
      //    in  -ssa -super case; note f(v) is on regular grid;
      //    compare broadcastSSAVelocity(); note uvbar[o] is SIA result:
      //    uvbar[0] H = - D h_x
      PetscScalar He, Hw, Hn, Hs;
      if ( do_superpose && (vMask.value(i,j) == MASK_DRAGGING_SHEET) ) {
        const PetscScalar
          fv  = 1.0 - outC_fofv * atan( inC_fofv *
                      ( PetscSqr(ubarssa[i][j]) + PetscSqr(vbarssa[i][j]) ) ),
          fve = 1.0 - outC_fofv * atan( inC_fofv *
                      ( PetscSqr(ubarssa[i+1][j]) + PetscSqr(vbarssa[i+1][j]) ) ),
          fvw = 1.0 - outC_fofv * atan( inC_fofv *
                      ( PetscSqr(ubarssa[i-1][j]) + PetscSqr(vbarssa[i-1][j]) ) ),
          fvn = 1.0 - outC_fofv * atan( inC_fofv *
                      ( PetscSqr(ubarssa[i][j+1]) + PetscSqr(vbarssa[i][j+1]) ) ),
          fvs = 1.0 - outC_fofv * atan( inC_fofv *
                      ( PetscSqr(ubarssa[i][j-1]) + PetscSqr(vbarssa[i][j-1]) ) );
        const PetscScalar fvH = fv * vH(i,j);
        He = 0.5 * (fvH + fve * vH(i+1,j));
        Hw = 0.5 * (fvw * vH(i-1,j) + fvH);
        Hn = 0.5 * (fvH + fvn * vH(i,j+1));
        Hs = 0.5 * (fvs * vH(i,j-1) + fvH);
      } else {
        He = 0.5 * (vH(i,j) + vH(i+1,j));
        Hw = 0.5 * (vH(i-1,j) + vH(i,j));
        Hn = 0.5 * (vH(i,j) + vH(i,j+1));
        Hs = 0.5 * (vH(i,j-1) + vH(i,j));
      }

      // staggered grid Div(Q) for SIA (non-sliding) deformation part;
      //    Q = - D grad h = Ubar H    in non-sliding case
      PetscScalar divQ = 0.0;
      if (computeSIAVelocities == PETSC_TRUE) {
        divQ =  (uvbar[0][i][j] * He - uvbar[0][i-1][j] * Hw) / dx
              + (uvbar[1][i][j] * Hn - uvbar[1][i][j-1] * Hs) / dy;
      }

      // basal sliding part: split  Div(v H)  by product rule into  v . grad H
      //    and  (Div v) H; use upwinding on first and centered on second
      divQ +=  ub[i][j] * ( ub[i][j] < 0 ? vH(i+1,j)-vH(i,j)
                                         : vH(i,j)-vH(i-1,j) ) / dx
             + vb[i][j] * ( vb[i][j] < 0 ? vH(i,j+1)-vH(i,j)
                                         : vH(i,j)-vH(i,j-1) ) / dy;

      divQ += vH(i,j) * ( (ub[i+1][j] - ub[i-1][j]) / (2.0*dx)
                          + (vb[i][j+1] - vb[i][j-1]) / (2.0*dy) );

      vHnew(i,j) += (acab(i,j) - divQ) * dt; // include M

      if (include_bmr_in_continuity) { // include S
        if (vMask.is_floating(i,j)) {
	  vHnew(i,j) -= shelfbmassflux(i,j) * dt;
        } else {
           vHnew(i,j) -= bmr_gnded[i][j] * dt;
        }
      }

      // apply free boundary rule: negative thickness becomes zero
      if (vHnew(i,j) < 0)
        vHnew(i,j) = 0.0;

      // the following conditionals, both -ocean_kill and -float_kill, are also applied in 
      //   IceModel::computeMax2DSlidingSpeed() when determining CFL
      
      // force zero thickness at points which were originally ocean (if "-ocean_kill");
      //   this is calving at original calving front location
      if ( do_ocean_kill && (vMask.value(i,j) == MASK_OCEAN_AT_TIME_0) )
        vHnew(i,j) = 0.0;

      // force zero thickness at points which are floating (if "-float_kill");
      //   this is calving at grounding line
      if ( floating_ice_killed && vMask.is_floating(i,j) )
        vHnew(i,j) = 0.0;

    } // end of the inner for loop
  } // end of the outer for loop

  ierr = vbasalMeltRate.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);
  ierr = vuvbar[0].end_access(); CHKERRQ(ierr);
  ierr = vuvbar[1].end_access(); CHKERRQ(ierr);
  ierr = vub.end_access(); CHKERRQ(ierr);
  ierr = vvb.end_access(); CHKERRQ(ierr);
  ierr = vubarSSA.end_access(); CHKERRQ(ierr);
  ierr = vvbarSSA.end_access(); CHKERRQ(ierr);
  ierr = acab.end_access(); CHKERRQ(ierr);
  ierr = shelfbmassflux.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vHnew.end_access(); CHKERRQ(ierr);

  // compute dH/dt (thickening rate) for viewing and for saving at end; only diagnostic
  ierr = vHnew.add(-1.0, vH, vdHdt); CHKERRQ(ierr); // vdHdt = vHnew - vH
  ierr = vdHdt.scale(1.0/dt); CHKERRQ(ierr);	    // vdHdt = vdHdt / dt

  // average value of dH/dt; also d(volume)/dt
  PetscScalar gicecount;
  ierr = PetscGlobalSum(&icecount, &gicecount, grid.com); CHKERRQ(ierr);

  ierr = vdHdt.sum(gdHdtav); CHKERRQ(ierr);
  dvoldt = gdHdtav * grid.dx * grid.dy;  // m^3/s
  gdHdtav = gdHdtav / gicecount; // m/s

  // now that dHdt is correctly calculated and summed, mask out for diagnostic display
  ierr = vdHdt.mask_by(vHnew, GSL_NAN); CHKERRQ(ierr);

  // finally copy vHnew into vH and communicate ghosted values
  ierr = vHnew.beginGhostComm(vH); CHKERRQ(ierr);
  ierr = vHnew.endGhostComm(vH); CHKERRQ(ierr);

  // Check if the ice thickness is exceeded the height of the computational box
  // and extend the grid if necessary:
  ierr = check_maximum_thickness(); CHKERRQ(ierr);

  return 0;
}

