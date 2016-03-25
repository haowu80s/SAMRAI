/*************************************************************************
 *
 * This file is part of the SAMRAI distribution.  For full copyright
 * information, see COPYRIGHT and COPYING.LESSER.
 *
 * Copyright:     (c) 1997-2012 Lawrence Livermore National Security, LLC
 * Description:   Test program for RankGroup with TreeLoadBalancer.
 *
 ************************************************************************/
#include "SAMRAI/SAMRAI_config.h"

#include <iomanip>

#include "SAMRAI/mesh/BergerRigoutsos.h"
#include "SAMRAI/hier/BoxLevel.h"
#include "SAMRAI/hier/BoxLevelStatistics.h"
#include "SAMRAI/hier/IntVector.h"
#include "SAMRAI/geom/CartesianGridGeometry.h"
#include "SAMRAI/pdat/CellData.h"
#include "SAMRAI/pdat/CellVariable.h"
#include "SAMRAI/hier/Connector.h"
#include "SAMRAI/hier/BoxContainer.h"
#include "SAMRAI/hier/ConnectorStatistics.h"
#include "SAMRAI/hier/BoxLevelConnectorUtils.h"
#include "SAMRAI/hier/OverlapConnectorAlgorithm.h"
#include "SAMRAI/hier/MappingConnectorAlgorithm.h"
#include "SAMRAI/mesh/BalanceUtilities.h"
#include "SAMRAI/mesh/TreeLoadBalancer.h"
#include "SAMRAI/mesh/ChopAndPackLoadBalancer.h"
#include "SAMRAI/hier/VariableDatabase.h"
#include "SAMRAI/appu/VisItDataWriter.h"

#include "SAMRAI/tbox/InputDatabase.h"
#include "SAMRAI/tbox/InputManager.h"
#include "SAMRAI/tbox/MathUtilities.h"
#include "SAMRAI/tbox/SAMRAIManager.h"
#include "SAMRAI/tbox/TimerManager.h"
#include <vector>

using namespace SAMRAI;
using namespace tbox;

/*
 ************************************************************************
 *
 *
 *************************************************************************
 */

typedef std::vector<hier::Box> NodeVec;
typedef std::vector<hier::Box> BoxVec;

void
generatePrebalanceByUserBoxes(
   boost::shared_ptr<tbox::Database> database,
   const boost::shared_ptr<hier::PatchHierarchy>& hierarchy,
   const hier::IntVector& min_size,
   const hier::IntVector& max_gcw,
   hier::BoxLevel& balance_mapped_box_level,
   const hier::BoxLevel& anchor_mapped_box_level,
   hier::Connector& anchor_to_balance,
   hier::Connector& balacne_to_anchor);

void
generatePrebalanceByUserShells(
   const boost::shared_ptr<tbox::Database>& database,
   const boost::shared_ptr<hier::PatchHierarchy>& hierarchy,
   const hier::IntVector& min_size,
   const hier::IntVector& max_gcw,
   hier::BoxLevel& balance_mapped_box_level,
   const hier::BoxLevel& anchor_mapped_box_level,
   hier::Connector& anchor_to_balance,
   hier::Connector& balance_to_anchor);

void
sortNodes(
   hier::BoxLevel& new_mapped_box_level,
   hier::Connector& tag_to_new,
   hier::Connector& new_to_tag,
   bool sort_by_corners,
   bool sequentialize_global_indices);

int main(
   int argc,
   char* argv[])
{
   /*
    * Initialize MPI, SAMRAI.
    */

   SAMRAI_MPI::init(&argc, &argv);
   SAMRAIManager::initialize();
   SAMRAIManager::startup();
   tbox::SAMRAI_MPI mpi(SAMRAI_MPI::getSAMRAIWorld());

   const int rank = mpi.getRank();
   int fail_count = 0;

   /*
    * Process command line arguments.  For each run, the input
    * filename must be specified.  Usage is:
    *
    * executable <input file name>
    */
   std::string input_filename;

   if (argc < 2) {
      TBOX_ERROR("USAGE:  " << argv[0] << " <input file> [case name]\n"
                            << "  options:\n"
                            << "  none at this time" << std::endl);
   } else {
      input_filename = argv[1];
   }

   std::string case_name;
   if (argc > 2) {
      case_name = argv[2];
   }

   {
      /*
       * Scope to force destruction of objects that would otherwise
       * leave allocated memory reported by the memory test.
       */

      /*
       * Create input database and parse all data in input file.
       */

      boost::shared_ptr<InputDatabase> input_db(
         new InputDatabase("input_db"));
      tbox::InputManager::getManager()->parseInputFile(input_filename, input_db);

      /*
       * Set up the timer manager.
       */
      if (input_db->isDatabase("TimerManager")) {
         TimerManager::createManager(input_db->getDatabase("TimerManager"));
      }

      /*
       * Retrieve "Main" section from input database.
       * The main database is used only in main().
       * The base_name variable is a base name for
       * all name strings in this program.
       */

      boost::shared_ptr<Database> main_db(input_db->getDatabase("Main"));

      const tbox::Dimension dim(static_cast<unsigned short>(main_db->getInteger("dim")));
      const int dimval = dim.getValue();

      std::string base_name = "unnamed";
      base_name = main_db->getStringWithDefault("base_name", base_name);

      /*
       * Modify basename for this particular run.
       * Add the number of processes and the case name.
       */
      if (!case_name.empty()) {
         base_name = base_name + '-' + case_name;
      }
      base_name = base_name + '-' + tbox::Utilities::intToString(
            mpi.getSize(),
            5);
      tbox::plog << "Added case name (" << case_name << ") and nprocs ("
                 << mpi.getSize() << ") to base name -> '"
                 << base_name << "'\n";

      if (!case_name.empty()) {
         tbox::plog << "Added case name (" << case_name << ") and nprocs ("
                    << mpi.getSize() << ") to base name -> '"
                    << base_name << "'\n";
      }

      /*
       * Start logging.
       */
      const std::string log_file_name = base_name + ".log";
      bool log_all_nodes = false;
      log_all_nodes = main_db->getBoolWithDefault("log_all_nodes",
            log_all_nodes);
      if (log_all_nodes) {
         PIO::logAllNodes(log_file_name);
      } else {
         PIO::logOnlyNodeZero(log_file_name);
      }

      plog << "Input database after initialization..." << std::endl;
      input_db->printClassData(plog);

      // tbox::TimerManager *tm = tbox::TimerManager::getManager();
      // boost::shared_ptr<tbox::Timer> t_search_tree_for_set =
      // tm->getTimer("apps::main::search_tree_for_set");

      /*
       * Parameters.  Some of these should be specified by input deck.
       */
      hier::IntVector ghost_cell_width(dim, 2);
      if (main_db->isInteger("ghost_cell_width")) {
         main_db->getIntegerArray("ghost_cell_width", &ghost_cell_width[0], dimval);
      }

      hier::IntVector min_size(dim, 8);
      if (main_db->isInteger("min_size")) {
         main_db->getIntegerArray("min_size", &min_size[0], dimval);
      }
      hier::IntVector max_size(dim, -1);
      if (main_db->isInteger("max_size")) {
         main_db->getIntegerArray("max_size", &max_size[0], dimval);
      }
      hier::IntVector bad_interval(dim, 2);
      hier::IntVector cut_factor(dim, 1);

      hier::OverlapConnectorAlgorithm oca;

      /*
       * Set up the domain from input.
       */
      hier::BoxContainer input_boxes(main_db->getDatabaseBoxArray("domain_boxes"));
      hier::BoxContainer domain_boxes;
      hier::LocalId local_id(0);
      for (hier::BoxContainer::iterator itr = input_boxes.begin();
           itr != input_boxes.end(); ++itr) {
         itr->setBlockId(hier::BlockId(0));
         domain_boxes.pushBack(hier::Box(*itr, local_id++, 0));
      }

      /*
       * Create hierarchy we can satisfy the load balancing
       * interface and dump visit output.
       *
       * anchor_mapped_box_level is used for level 0.
       * balance_mapped_box_level is used for level 1.
       */
      std::vector<double> xlo(dimval);
      std::vector<double> xhi(dimval);
      for (int i = 0; i < dimval; ++i) {
         xlo[i] = 0.0;
         xhi[i] = 1.0;
      }
      boost::shared_ptr<geom::CartesianGridGeometry> grid_geometry(
         new geom::CartesianGridGeometry(
            "GridGeometry",
            &xlo[0],
            &xhi[0],
            domain_boxes));

      boost::shared_ptr<hier::PatchHierarchy> hierarchy(
         new hier::PatchHierarchy(
            "Hierarchy",
            grid_geometry));

      hierarchy->setMaxNumberOfLevels(2);

      hier::BoxLevel domain_mapped_box_level(
         hier::IntVector(dim, 1),
         grid_geometry,
         mpi,
         hier::BoxLevel::GLOBALIZED);

      hier::BoxContainer::iterator domain_boxes_itr(domain_boxes);
      for (int i = 0; i < domain_boxes.size(); ++i, ++domain_boxes_itr) {
         domain_mapped_box_level.addBox(hier::Box(*domain_boxes_itr,
               hier::LocalId(i), 0));
      }

      /*
       * Set up the load balancers.
       */

      mesh::ChopAndPackLoadBalancer cut_and_pack_lb(
         dim,
         "ChopAndPackLoadBalancer",
         input_db->getDatabaseWithDefault("ChopAndPackLoadBalancer",
            boost::shared_ptr<tbox::Database>()));

      mesh::TreeLoadBalancer tree_lb(
         dim,
         "TreeLoadBalancer",
         input_db->getDatabaseWithDefault("TreeLoadBalancer",
            boost::shared_ptr<tbox::Database>()));
      tree_lb.setSAMRAI_MPI(mpi);

      mesh::LoadBalanceStrategy* lb = NULL;

      std::string load_balancer_type;
      if (main_db->isString("load_balancer_type")) {
         load_balancer_type = main_db->getString("load_balancer_type");
         if (load_balancer_type == "TreeLoadBalancer") {
            lb = &tree_lb;
         } else if (load_balancer_type == "ChopAndPackLoadBalancer") {
            lb = &cut_and_pack_lb;
         }
      }
      if (lb == NULL) {
         TBOX_ERROR(
            "Missing or bad load_balancer specification in Main database.\n"
            << "Specify load_balancer_type = STRING, where STRING can be\n"
            << "\"ChopAndPackLoadBalancer\" or \"TreeLoadBalancer\".");
      }

      /*
       * Set up data used by TreeLoadBalancer.
       */
      hier::BoxLevel anchor_mapped_box_level(hier::IntVector(dim, 1),
                                             grid_geometry);
      hier::BoxLevel balance_mapped_box_level(dim);
      hier::Connector balance_to_anchor;
      hier::Connector anchor_to_balance;
      hier::Connector balance_to_balance;

      {
         hier::BoxContainer anchor_boxes(main_db->getDatabaseBoxArray("anchor_boxes"));
         const int boxes_per_proc =
            (anchor_boxes.size() + anchor_mapped_box_level.getMPI().getSize()
             - 1) / anchor_mapped_box_level.getMPI().getSize();
         const int my_boxes_start = anchor_mapped_box_level.getMPI().getRank()
            * boxes_per_proc;
         const int my_boxes_stop =
            tbox::MathUtilities<int>::Min(my_boxes_start + boxes_per_proc,
               anchor_boxes.size());
         hier::BoxContainer::iterator anchor_boxes_itr(anchor_boxes);
         for (int i = 0; i < my_boxes_start; ++i) {
            ++anchor_boxes_itr;
         }
         for (int i = my_boxes_start; i < my_boxes_stop; ++i, ++anchor_boxes_itr) {
            anchor_mapped_box_level.addBox(*anchor_boxes_itr,
               hier::BlockId::zero());
         }
      }

      {
         /*
          * Load balance the anchor mapped_box_level, using the domain as its anchor.
          *
          * This is not a part of the performance test because does not
          * reflect the load balancer use in real apps.  We just neeed a
          * distributed anchor for the real loac balancing performance test.
          */
         hier::Connector anchor_to_domain(
            anchor_mapped_box_level,
            domain_mapped_box_level,
            hier::IntVector(dim, 2));
         hier::Connector domain_to_anchor(
            domain_mapped_box_level,
            anchor_mapped_box_level,
            hier::IntVector(dim, 2));
         oca.findOverlaps(anchor_to_domain);
         oca.findOverlaps(domain_to_anchor);

         tbox::plog << "\n\n\ninitial anchor loads:\n";
         mesh::BalanceUtilities::gatherAndReportLoadBalance(
            (double)anchor_mapped_box_level.getLocalNumberOfCells(),
            anchor_mapped_box_level.getMPI());

         const int nnodes = mpi.getSize();
         tbox::Array<int> active_ranks;
         if (nnodes == 1) {
            active_ranks.resizeArray(1);
            active_ranks[0] = 0;
         } else {
            active_ranks.resizeArray(nnodes / 2);
            for (int i = 0; i < nnodes / 2; i++) {
               active_ranks[i] = (i + 1) % (nnodes / 2);
            }
         }
         tbox::RankGroup rank_group_0(active_ranks, mpi);

         lb->loadBalanceBoxLevel(
            anchor_mapped_box_level,
            anchor_to_domain,
            domain_to_anchor,
            hierarchy,
            0,
            hier::Connector(),
            hier::Connector(),
            min_size,
            max_size,
            domain_mapped_box_level,
            bad_interval,
            cut_factor,
            rank_group_0);

         sortNodes(anchor_mapped_box_level,
            domain_to_anchor,
            anchor_to_domain,
            false,
            true);

         oca.assertOverlapCorrectness(anchor_to_domain);
         oca.assertOverlapCorrectness(domain_to_anchor);

         anchor_mapped_box_level.cacheGlobalReducedData();

         tbox::plog << "\n\n\nfinal anchor loads:\n";
         mesh::BalanceUtilities::gatherAndReportLoadBalance(
            (double)anchor_mapped_box_level.getLocalNumberOfCells(),
            anchor_mapped_box_level.getMPI());
      }

      {
         std::string box_gen_method("PrebalanceByUserBoxes");
         box_gen_method = main_db->getStringWithDefault("box_gen_method",
               box_gen_method);
         if (box_gen_method == "PrebalanceByUserBoxes") {
            generatePrebalanceByUserBoxes(
               main_db->getDatabase("PrebalanceByUserBoxes"),
               hierarchy,
               min_size,
               ghost_cell_width,
               balance_mapped_box_level,
               anchor_mapped_box_level,
               anchor_to_balance,
               balance_to_anchor);
         } else if (box_gen_method == "PrebalanceByUserShells") {
            generatePrebalanceByUserShells(
               main_db->getDatabase("PrebalanceByUserShells"),
               hierarchy,
               min_size,
               ghost_cell_width,
               balance_mapped_box_level,
               anchor_mapped_box_level,
               anchor_to_balance,
               balance_to_anchor);
         } else {
            TBOX_ERROR("Bad box_gen_method: '" << box_gen_method << "'");
         }
      }

      {
         /*
          * Output "before" data.
          */
         balance_mapped_box_level.cacheGlobalReducedData();
         tbox::plog << "\n\n\nBefore:\n";
         mesh::BalanceUtilities::gatherAndReportLoadBalance(
            (double)balance_mapped_box_level.getLocalNumberOfCells(),
            balance_mapped_box_level.getMPI());

         hier::BoxLevelStatistics anchor_stats(anchor_mapped_box_level);
         tbox::plog << "Anchor mapped_box_level node stats:\n";
         anchor_stats.printBoxStats(tbox::plog, "AL-> ");
         tbox::plog << "Anchor mapped_box_level:\n";
         anchor_mapped_box_level.recursivePrint(tbox::plog, "AL-> ", 2);

         hier::BoxLevelStatistics balance_stats(anchor_mapped_box_level);
         tbox::plog << "Balance mapped_box_level node stats:\n";
         balance_stats.printBoxStats(tbox::plog, "BL-> ");
         tbox::plog << "Balance mapped_box_level:\n";
         balance_mapped_box_level.recursivePrint(tbox::plog, "BL-> ", 2);

         hier::ConnectorStatistics balance_anchor_stats(balance_to_anchor);
         tbox::plog << "balance_to_anchor edge stats:\n";
         balance_anchor_stats.printNeighborStats(tbox::plog, "BA-> ");
         tbox::plog << "balance_to_anchor:\n";
         balance_to_anchor.recursivePrint(tbox::plog, "BA-> ");

         hier::ConnectorStatistics anchor_balance_stats(anchor_to_balance);
         tbox::plog << "anchor_to_balance edge stats:\n";
         anchor_balance_stats.printNeighborStats(tbox::plog, "AB-> ");
         tbox::plog << "anchor_to_balance:\n";
         anchor_to_balance.recursivePrint(tbox::plog, "AB-> ");
      }

      {

         const int nnodes = mpi.getSize();
         tbox::RankGroup rank_group_1(mpi);
         if (nnodes == 1) {
            rank_group_1.setMinMax(0, 0);
         } else {
            rank_group_1.setMinMax(0, (nnodes / 2) - 1);
         }

         /*
          * Load balance the unbalanced mapped_box_level.
          */
         lb->loadBalanceBoxLevel(
            balance_mapped_box_level,
            balance_to_anchor,
            anchor_to_balance,
            hierarchy,
            1,
            hier::Connector(),
            hier::Connector(),
            min_size,
            max_size,
            domain_mapped_box_level,
            bad_interval,
            cut_factor,
            rank_group_1);

         oca.assertOverlapCorrectness(balance_to_anchor);
         oca.assertOverlapCorrectness(anchor_to_balance);

         sortNodes(balance_mapped_box_level,
            anchor_to_balance,
            balance_to_anchor,
            false,
            true);
      }

      /*
       * Get the balance_to_balance for edge statistics.
       */
      oca.bridge(
         balance_to_balance,
         balance_to_anchor,
         anchor_to_balance,
         balance_to_anchor,
         anchor_to_balance);

      {
         /*
          * Output "after" data.
          */
         balance_mapped_box_level.cacheGlobalReducedData();
         tbox::plog << "\n\n\nAfter:\n";
         mesh::BalanceUtilities::gatherAndReportLoadBalance(
            (double)balance_mapped_box_level.getLocalNumberOfCells(),
            balance_mapped_box_level.getMPI());

         hier::BoxLevelStatistics balance_stats(balance_mapped_box_level);
         tbox::plog << "Balance mapped_box_level node stats:\n";
         balance_stats.printBoxStats(tbox::plog, "BL-> ");
         tbox::plog << "Balance mapped_box_level:\n";
         balance_mapped_box_level.recursivePrint(tbox::plog, "BL-> ", 2);

         hier::ConnectorStatistics balance_balance_stats(balance_to_balance);
         tbox::plog << "balance_to_balance edge stats:\n";
         balance_balance_stats.printNeighborStats(tbox::plog, "BB-> ");
         tbox::plog << "balance_to_balance:\n";
         balance_to_balance.recursivePrint(tbox::plog, "BB-> ");

         hier::ConnectorStatistics balance_anchor_stats(balance_to_anchor);
         tbox::plog << "balance_to_anchor edge stats:\n";
         balance_anchor_stats.printNeighborStats(tbox::plog, "BA-> ");
         tbox::plog << "balance_to_anchor:\n";
         balance_to_anchor.recursivePrint(tbox::plog, "BA-> ");

         hier::ConnectorStatistics anchor_balance_stats(anchor_to_balance);
         tbox::plog << "anchor_to_balance edge stats:\n";
         anchor_balance_stats.printNeighborStats(tbox::plog, "AB-> ");
         tbox::plog << "anchor_to_balance:\n";
         anchor_to_balance.recursivePrint(tbox::plog, "AB-> ");

         // Dump summary statistics to output.
         mesh::BalanceUtilities::gatherAndReportLoadBalance(
            (double)balance_mapped_box_level.getLocalNumberOfCells(),
            balance_mapped_box_level.getMPI(),
            tbox::plog);
      }

      const hier::BoxLevel& layer0 = anchor_mapped_box_level;
      hierarchy->makeNewPatchLevel(
         0,
         layer0);

      const hier::BoxLevel& layer1 = balance_mapped_box_level;
      hierarchy->makeNewPatchLevel(
         1,
         layer1);

#ifdef HAVE_HDF5
#if 0
      if (dimval == 2 || dimval == 3) {
         /*
          * Create the VisIt data writer.
          * Write the plot file.
          */
         DerivedVisOwnerData owner_writer;
         const std::string visit_filename = base_name + ".visit";
         appu::VisItDataWriter visit_data_writer(dim,
                                                 "VisIt Writer",
                                                 visit_filename);
         visit_data_writer.registerDerivedPlotQuantity("Owner",
            "SCALAR",
            &owner_writer);
         visit_data_writer.writePlotData(hierarchy, 0);
      }
#endif
#endif

   }

   /*
    * Print input database again to fully show usage.
    */
   plog << "Input database after running..." << std::endl;
   tbox::InputManager::getManager()->getInputDatabase()->printClassData(plog);

   tbox::pout << "\nPASSED:  rank_group" << std::endl;

   /*
    * Exit properly by shutting down services in correct order.
    */
   tbox::plog << "\nShutting down..." << std::endl;

   /*
    * Shut down.
    */
   SAMRAIManager::shutdown();
   SAMRAIManager::finalize();

   if (fail_count == 0) {
      SAMRAI_MPI::finalize();
   } else {
      std::cout << "Process " << std::setw(5) << rank << " aborting."
                << std::endl;
      tbox::Utilities::abort("Aborting due to nonzero fail count",
         __FILE__, __LINE__);
   }

   return fail_count;
}

/*
 ***********************************************************************
 ***********************************************************************
 */
void generatePrebalanceByUserShells(
   const boost::shared_ptr<tbox::Database>& database,
   const boost::shared_ptr<hier::PatchHierarchy>& hierarchy,
   const hier::IntVector& min_size,
   const hier::IntVector& max_gcw,
   hier::BoxLevel& balance_mapped_box_level,
   const hier::BoxLevel& anchor_mapped_box_level,
   hier::Connector& anchor_to_balance,
   hier::Connector& balance_to_anchor)
{

   const tbox::Dimension dim(hierarchy->getDim());
   const int dimval = dim.getValue();

   /*
    * Starting at shell origin, tag cells with centroids
    * at radii[0]<r<radii[1], radii[2]<r<radii[3], and so on.
    */
   tbox::Array<double> radii;
   double efficiency_tol = 0.75;
   double combine_tol = 0.75;

   std::vector<double> r0(dimval);
   for (int d = 0; d < dimval; ++d) r0[d] = 0;

   boost::shared_ptr<tbox::Database> abr_db;
   if (database) {
      efficiency_tol = database->getDoubleWithDefault("efficiency_tol",
            efficiency_tol);
      combine_tol = database->getDoubleWithDefault("combine_tol", combine_tol);
      if (database->isDouble("r0")) {
         database->getDoubleArray("r0", &r0[0], dimval);
      }
      if (database->isDouble("radii")) {
         radii = database->getDoubleArray("radii");
      }
      abr_db = database->getDatabaseWithDefault("BergerRigoutsos", abr_db);
      TBOX_ASSERT(radii.size() % 2 == 0);
   }

   const int tag_val = 1;

   hier::VariableDatabase* vdb =
      hier::VariableDatabase::getDatabase();
   boost::shared_ptr<geom::CartesianGridGeometry> grid_geometry(
      hierarchy->getGridGeometry(),
      boost::detail::dynamic_cast_tag());

   boost::shared_ptr<hier::PatchLevel> tag_level(
      new hier::PatchLevel(
         anchor_mapped_box_level,
         grid_geometry,
         vdb->getPatchDescriptor()));

   boost::shared_ptr<pdat::CellVariable<int> > tag_variable(
      new pdat::CellVariable<int>(dim, "TagVariable"));

   boost::shared_ptr<hier::VariableContext> default_context(
      vdb->getContext("TagVariable"));

   const int tag_id = vdb->registerVariableAndContext(
         tag_variable,
         default_context,
         hier::IntVector(dim, 0));

   tag_level->allocatePatchData(tag_id);

   const double* xlo = grid_geometry->getXLower();
   const double* h = grid_geometry->getDx();
   for (hier::PatchLevel::iterator pi(tag_level->begin());
        pi != tag_level->end(); ++pi) {
      const boost::shared_ptr<hier::Patch>& patch = *pi;
      boost::shared_ptr<pdat::CellData<int> > tag_data(
         patch->getPatchData(tag_id),
         boost::detail::dynamic_cast_tag());

      tag_data->getArrayData().undefineData();

      pdat::CellData<int>::iterator ciend(tag_data->getGhostBox(), false);
      for (pdat::CellData<int>::iterator ci(tag_data->getGhostBox(), true);
           ci != ciend; ++ci) {
         const pdat::CellIndex& idx = *ci;
         double rr = 0;
         std::vector<double> r(dimval);
         for (int d = 0; d < dimval; ++d) {
            r[d] = xlo[d] + h[d] * (idx(d) + 0.5) - r0[d];
            rr += r[d] * r[d];
         }
         rr = sqrt(rr);
         for (int i = 0; i < radii.size(); i += 2) {
            if (radii[i] < rr && rr < radii[i + 1]) {
               (*tag_data)(idx) = tag_val;
               break;
            }
         }
      }
   }

   mesh::BergerRigoutsos abr(dim, abr_db);
   abr.setMPI(anchor_mapped_box_level.getMPI());
   abr.findBoxesContainingTags(
      balance_mapped_box_level,
      anchor_to_balance,
      balance_to_anchor,
      tag_level,
      tag_id,
      tag_val,
      anchor_mapped_box_level.getGlobalBoundingBox(0),
      min_size,
      efficiency_tol,
      combine_tol,
      max_gcw,
      hier::BlockId::zero(),
      hier::LocalId(0));

   /*
    * The clustering step generated Connectors to/from the temporary
    * tag_level->getBoxLevel(), which is not the same as the
    * anchor BoxLevel.  We need to reset the Connectors to use
    * the anchor_mapped_box_level instead.
    */
   anchor_to_balance.setBase(anchor_mapped_box_level);
   anchor_to_balance.setHead(balance_mapped_box_level, true);
   balance_to_anchor.setBase(balance_mapped_box_level);
   balance_to_anchor.setHead(anchor_mapped_box_level, true);
}

/*
 ***********************************************************************
 ***********************************************************************
 */
void generatePrebalanceByUserBoxes(
   boost::shared_ptr<tbox::Database> database,
   const boost::shared_ptr<hier::PatchHierarchy>& hierarchy,
   const hier::IntVector& min_size,
   const hier::IntVector& max_gcw,
   hier::BoxLevel& balance_mapped_box_level,
   const hier::BoxLevel& anchor_mapped_box_level,
   hier::Connector& anchor_to_balance,
   hier::Connector& balance_to_anchor)
{
   NULL_USE(hierarchy);
   NULL_USE(min_size);

   const tbox::Dimension& dim(hierarchy->getDim());

   hier::BoxContainer balance_boxes(database->getDatabaseBoxArray("balance_boxes"));
   tbox::Array<int> initial_owners(1);
   initial_owners[0] = 0;
   initial_owners = database->getIntegerArray("initial_owners");

   balance_mapped_box_level.initialize(hier::IntVector(dim, 1),
      hierarchy->getGridGeometry(),
      anchor_mapped_box_level.getMPI());
   hier::BoxContainer::iterator balance_boxes_itr(balance_boxes);
   for (int i = 0; i < balance_boxes.size(); ++i, ++balance_boxes_itr) {
      const int owner = i % initial_owners.size();
      if (owner == balance_mapped_box_level.getMPI().getRank()) {
         balance_boxes_itr->setBlockId(hier::BlockId(0)); 
         balance_mapped_box_level.addBox(hier::Box(*balance_boxes_itr,
               hier::LocalId(i), owner));
      }
   }
   balance_to_anchor.setBase(balance_mapped_box_level);
   balance_to_anchor.setHead(anchor_mapped_box_level);
   balance_to_anchor.setWidth(max_gcw, true);
   anchor_to_balance.setBase(anchor_mapped_box_level);
   anchor_to_balance.setHead(balance_mapped_box_level);
   anchor_to_balance.setWidth(max_gcw, true);
   hier::OverlapConnectorAlgorithm oca;
   oca.findOverlaps(balance_to_anchor);
   oca.findOverlaps(anchor_to_balance);
}

/*
 ***********************************************************************
 ***********************************************************************
 */
void sortNodes(
   hier::BoxLevel& new_mapped_box_level,
   hier::Connector& tag_to_new,
   hier::Connector& new_to_tag,
   bool sort_by_corners,
   bool sequentialize_global_indices)
{
   const hier::MappingConnectorAlgorithm mca;

   hier::Connector sorting_map;
   hier::BoxLevel seq_mapped_box_level(new_mapped_box_level.getDim());
   hier::BoxLevelConnectorUtils dlbg_edge_utils;
   dlbg_edge_utils.makeSortingMap(
      seq_mapped_box_level,
      sorting_map,
      new_mapped_box_level,
      sort_by_corners,
      sequentialize_global_indices);

   mca.modify(tag_to_new,
      new_to_tag,
      sorting_map,
      &new_mapped_box_level);
}