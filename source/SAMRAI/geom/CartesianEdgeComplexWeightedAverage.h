/*************************************************************************
 *
 * This file is part of the SAMRAI distribution.  For full copyright
 * information, see COPYRIGHT and COPYING.LESSER.
 *
 * Copyright:     (c) 1997-2012 Lawrence Livermore National Security, LLC
 * Description:   Weighted averaging operator for edge-centered complex data on
 *                a Cartesian mesh.
 *
 ************************************************************************/

#ifndef included_geom_CartesianEdgeComplexWeightedAverage
#define included_geom_CartesianEdgeComplexWeightedAverage

#include "SAMRAI/SAMRAI_config.h"

#include "SAMRAI/hier/CoarsenOperator.h"
#include "SAMRAI/hier/Box.h"
#include "SAMRAI/hier/IntVector.h"
#include "SAMRAI/hier/Patch.h"

#include <boost/shared_ptr.hpp>
#include <string>

namespace SAMRAI {
namespace geom {

/**
 * Class CartesianEdgeComplexWeightedAverage implements conservative
 * edge-weighted averaging for edge-centered complex patch data defined over a
 * Cartesian mesh.  It is derived from the hier::CoarsenOperator base class.
 * The numerical operations for theaveraging use FORTRAN numerical routines.
 *
 * @see hier::CoarsenOperator
 */

class CartesianEdgeComplexWeightedAverage:
   public hier::CoarsenOperator
{
public:
   /**
    * Uninteresting default constructor.
    */
   explicit CartesianEdgeComplexWeightedAverage(
      const tbox::Dimension& dim);

   /**
    * Uninteresting virtual destructor.
    */
   virtual ~CartesianEdgeComplexWeightedAverage();

   /**
    * The priority of edge-centered complex weighted averaging is 0.
    * It will be performed before any user-defined coarsen operations.
    */
   int
   getOperatorPriority() const;

   /**
    * The stencil width of the weighted averaging operator is the vector of
    * zeros.  That is, its stencil does not extend outside the fine box.
    */
   hier::IntVector
   getStencilWidth() const;

   /**
    * Coarsen the source component on the fine patch to the destination
    * component on the coarse patch using the edge-centered complex weighted
    * averaging operator.  Coarsening is performed on the intersection of
    * the destination patch and the coarse box.  It is assumed that the
    * fine patch contains sufficient data for the stencil width of the
    * coarsening operator.
    */
   void
   coarsen(
      hier::Patch& coarse,
      const hier::Patch& fine,
      const int dst_component,
      const int src_component,
      const hier::Box& coarse_box,
      const hier::IntVector& ratio) const;
};

}
}
#endif