/*************************************************************************
 *
 * This file is part of the SAMRAI distribution.  For full copyright
 * information, see COPYRIGHT and COPYING.LESSER.
 *
 * Copyright:     (c) 1997-2012 Lawrence Livermore National Security, LLC
 * Description:   Conservative linear refine operator for side-centered
 *                double data on a Cartesian mesh.
 *
 ************************************************************************/

#ifndef included_geom_CartesianSideDoubleConservativeLinearRefine_C
#define included_geom_CartesianSideDoubleConservativeLinearRefine_C

#include "SAMRAI/geom/CartesianSideDoubleConservativeLinearRefine.h"
#include "SAMRAI/geom/CartesianPatchGeometry.h"
#include "SAMRAI/hier/Index.h"
#include "SAMRAI/pdat/SideData.h"
#include "SAMRAI/pdat/SideVariable.h"
#include "SAMRAI/tbox/Array.h"
#include "SAMRAI/tbox/Utilities.h"

#include <cfloat>
#include <cmath>

/*
 *************************************************************************
 *
 * External declarations for FORTRAN  routines.
 *
 *************************************************************************
 */

extern "C" {

#ifdef __INTEL_COMPILER
#pragma warning (disable:1419)
#endif

// in cartrefine1d.f:
void F77_FUNC(cartclinrefsidedoub1d, CARTCLINREFSIDEDOUB1D) (const int&,
   const int&,
   const int&, const int&,
   const int&, const int&,
   const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *);
// in cartrefine2d.f:
void F77_FUNC(cartclinrefsidedoub2d0, CARTCLINREFSIDEDOUB2D0) (const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *, double *, double *);
void F77_FUNC(cartclinrefsidedoub2d1, CARTCLINREFSIDEDOUB2D1) (const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int&, const int&, const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *, double *, double *);
// in cartrefine3d.f:
void F77_FUNC(cartclinrefsidedoub3d0, CARTCLINREFSIDEDOUB3D0) (const int&,
   const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *, double *,
   double *, double *, double *);
void F77_FUNC(cartclinrefsidedoub3d1, CARTCLINREFSIDEDOUB3D1) (const int&,
   const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *, double *,
   double *, double *, double *);
void F77_FUNC(cartclinrefsidedoub3d2, CARTCLINREFSIDEDOUB3D2) (const int&,
   const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int&, const int&, const int&,
   const int *, const double *, const double *,
   const double *, double *,
   double *, double *, double *,
   double *, double *, double *);
}

namespace SAMRAI {
namespace geom {

CartesianSideDoubleConservativeLinearRefine::
CartesianSideDoubleConservativeLinearRefine(
   const tbox::Dimension& dim):
   hier::RefineOperator(dim, "CONSERVATIVE_LINEAR_REFINE")
{
}

CartesianSideDoubleConservativeLinearRefine::~
CartesianSideDoubleConservativeLinearRefine()
{
}

int
CartesianSideDoubleConservativeLinearRefine::getOperatorPriority() const
{
   return 0;
}

hier::IntVector
CartesianSideDoubleConservativeLinearRefine::getStencilWidth() const
{
   return hier::IntVector::getOne(getDim());
}

void
CartesianSideDoubleConservativeLinearRefine::refine(
   hier::Patch& fine,
   const hier::Patch& coarse,
   const int dst_component,
   const int src_component,
   const hier::BoxOverlap& fine_overlap,
   const hier::IntVector& ratio) const
{
   const tbox::Dimension& dim(getDim());
   TBOX_DIM_ASSERT_CHECK_DIM_ARGS3(dim, fine, coarse, ratio);

   boost::shared_ptr<pdat::SideData<double> > cdata(
      coarse.getPatchData(src_component),
      boost::detail::dynamic_cast_tag());
   boost::shared_ptr<pdat::SideData<double> > fdata(
      fine.getPatchData(dst_component),
      boost::detail::dynamic_cast_tag());

   const pdat::SideOverlap* t_overlap =
      dynamic_cast<const pdat::SideOverlap *>(&fine_overlap);

   TBOX_ASSERT(t_overlap != NULL);

   TBOX_ASSERT(cdata);
   TBOX_ASSERT(fdata);
   TBOX_ASSERT(cdata->getDepth() == fdata->getDepth());

   const hier::IntVector& directions(fdata->getDirectionVector());

   TBOX_ASSERT(directions ==
      hier::IntVector::min(directions, cdata->getDirectionVector()));

   const hier::Box cgbox(cdata->getGhostBox());

   const hier::Index cilo = cgbox.lower();
   const hier::Index cihi = cgbox.upper();
   const hier::Index filo = fdata->getGhostBox().lower();
   const hier::Index fihi = fdata->getGhostBox().upper();

   const boost::shared_ptr<CartesianPatchGeometry> cgeom(
      coarse.getPatchGeometry(),
      boost::detail::dynamic_cast_tag());
   const boost::shared_ptr<CartesianPatchGeometry> fgeom(
      fine.getPatchGeometry(),
      boost::detail::dynamic_cast_tag());

   for (int axis = 0; axis < dim.getValue(); axis++) {
      const hier::BoxContainer& boxes = t_overlap->getDestinationBoxContainer(axis);

      for (hier::BoxContainer::const_iterator b(boxes);
           b != boxes.end(); ++b) {

         hier::Box fine_box(*b);
         TBOX_DIM_ASSERT_CHECK_DIM_ARGS1(dim, fine_box);

         fine_box.upper(axis) -= 1;

         const hier::Box coarse_box = hier::Box::coarsen(fine_box, ratio);
         const hier::Index ifirstc = coarse_box.lower();
         const hier::Index ilastc = coarse_box.upper();
         const hier::Index ifirstf = fine_box.lower();
         const hier::Index ilastf = fine_box.upper();

         const hier::IntVector tmp_ghosts(dim, 0);
         tbox::Array<double> diff0(cgbox.numberCells(0) + 2);
         pdat::SideData<double> slope0(cgbox, 1, tmp_ghosts,
                                       directions);

         for (int d = 0; d < fdata->getDepth(); d++) {
            if ((dim == tbox::Dimension(1))) {
               if (directions(axis)) {
                  F77_FUNC(cartclinrefsidedoub1d, CARTCLINREFSIDEDOUB1D) (
                     ifirstc(0), ilastc(0),
                     ifirstf(0), ilastf(0),
                     cilo(0), cihi(0),
                     filo(0), fihi(0),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(0, d),
                     fdata->getPointer(0, d),
                     diff0.getPointer(), slope0.getPointer(0));
               }
            } else if ((dim == tbox::Dimension(2))) {
               tbox::Array<double> diff1(cgbox.numberCells(1) + 2);
               pdat::SideData<double> slope1(cgbox, 1, tmp_ghosts,
                                             directions);

               if (axis == 0 && directions(0)) {
                  F77_FUNC(cartclinrefsidedoub2d0, CARTCLINREFSIDEDOUB2D0) (
                     ifirstc(0), ifirstc(1), ilastc(0), ilastc(1),
                     ifirstf(0), ifirstf(1), ilastf(0), ilastf(1),
                     cilo(0), cilo(1), cihi(0), cihi(1),
                     filo(0), filo(1), fihi(0), fihi(1),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(0, d),
                     fdata->getPointer(0, d),
                     diff0.getPointer(), slope0.getPointer(0),
                     diff1.getPointer(), slope1.getPointer(0));
               }
               if (axis == 1 && directions(1)) {
                  F77_FUNC(cartclinrefsidedoub2d1, CARTCLINREFSIDEDOUB2D1) (
                     ifirstc(0), ifirstc(1), ilastc(0), ilastc(1),
                     ifirstf(0), ifirstf(1), ilastf(0), ilastf(1),
                     cilo(0), cilo(1), cihi(0), cihi(1),
                     filo(0), filo(1), fihi(0), fihi(1),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(1, d),
                     fdata->getPointer(1, d),
                     diff1.getPointer(), slope1.getPointer(1),
                     diff0.getPointer(), slope0.getPointer(1));
               }
            } else if ((dim == tbox::Dimension(3))) {
               tbox::Array<double> diff1(cgbox.numberCells(1) + 2);
               pdat::SideData<double> slope1(cgbox, 1, tmp_ghosts,
                                             directions);

               tbox::Array<double> diff2(cgbox.numberCells(2) + 2);
               pdat::SideData<double> slope2(cgbox, 1, tmp_ghosts,
                                             directions);

               if (axis == 0 && directions(0)) {
                  F77_FUNC(cartclinrefsidedoub3d0, CARTCLINREFSIDEDOUB3D0) (
                     ifirstc(0), ifirstc(1), ifirstc(2),
                     ilastc(0), ilastc(1), ilastc(2),
                     ifirstf(0), ifirstf(1), ifirstf(2),
                     ilastf(0), ilastf(1), ilastf(2),
                     cilo(0), cilo(1), cilo(2),
                     cihi(0), cihi(1), cihi(2),
                     filo(0), filo(1), filo(2),
                     fihi(0), fihi(1), fihi(2),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(0, d),
                     fdata->getPointer(0, d),
                     diff0.getPointer(), slope0.getPointer(0),
                     diff1.getPointer(), slope1.getPointer(0),
                     diff2.getPointer(), slope2.getPointer(0));
               }
               if (axis == 1 && directions(1)) {
                  F77_FUNC(cartclinrefsidedoub3d1, CARTCLINREFSIDEDOUB3D1) (
                     ifirstc(0), ifirstc(1), ifirstc(2),
                     ilastc(0), ilastc(1), ilastc(2),
                     ifirstf(0), ifirstf(1), ifirstf(2),
                     ilastf(0), ilastf(1), ilastf(2),
                     cilo(0), cilo(1), cilo(2),
                     cihi(0), cihi(1), cihi(2),
                     filo(0), filo(1), filo(2),
                     fihi(0), fihi(1), fihi(2),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(1, d),
                     fdata->getPointer(1, d),
                     diff1.getPointer(), slope1.getPointer(1),
                     diff2.getPointer(), slope2.getPointer(1),
                     diff0.getPointer(), slope0.getPointer(1));
               }
               if (axis == 2 && directions(2)) {
                  F77_FUNC(cartclinrefsidedoub3d2, CARTCLINREFSIDEDOUB3D2) (
                     ifirstc(0), ifirstc(1), ifirstc(2),
                     ilastc(0), ilastc(1), ilastc(2),
                     ifirstf(0), ifirstf(1), ifirstf(2),
                     ilastf(0), ilastf(1), ilastf(2),
                     cilo(0), cilo(1), cilo(2),
                     cihi(0), cihi(1), cihi(2),
                     filo(0), filo(1), filo(2),
                     fihi(0), fihi(1), fihi(2),
                     &ratio[0],
                     cgeom->getDx(),
                     fgeom->getDx(),
                     cdata->getPointer(2, d),
                     fdata->getPointer(2, d),
                     diff2.getPointer(), slope2.getPointer(2),
                     diff0.getPointer(), slope0.getPointer(2),
                     diff1.getPointer(), slope1.getPointer(2));
               }
            } else {
               TBOX_ERROR(
                  "CartesianSideDoubleConservativeLinearRefine error...\n"
                  << "dim > 3 not supported." << std::endl);
            }
         }
      }
   }
}

}
}
#endif