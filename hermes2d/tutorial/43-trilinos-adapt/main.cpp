#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace Teuchos;
using namespace RefinementSelectors;

//  The purpose of this example is to show how to use Trilinos while adapting mesh.
//  Solved by NOX solver, either using Newton's method or JFNK, with or without 
//  preconditioning. The underlying problem is benchmark "layer-internal".
//
//  PDE: -Laplace u = f.
//
//  Domain: Unit square.
//
//  BC: Nonhomogeneous Dirichlet.
//
//  Known exact solution, see functions fn() and fndd().
//
//  The following parameters can be changed:

const int P_INIT = 2;                    // Initial polynomial degree of all mesh elements.
const int INIT_REF_NUM = 1;              // Number of initial uniform mesh refinements.
const bool JFNK = true;                  // true = jacobian-free method,
                                         // false = Newton.
const bool PRECOND = true;               // Preconditioning by jacobian in case of jfnk,
                                         // default ML proconditioner in case of Newton.
const double THRESHOLD = 0.3;            // This is a quantitative parameter of the adapt(...) function and
                                         // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;                  // Adaptive strategy:
                                         // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                         //   error is processed. If more elements have similar errors, refine
                                         //   all to keep the mesh symmetric.
                                         // STRATEGY = 1 ... refine all elements whose error is larger
                                         //   than THRESHOLD times maximum element error.
                                         // STRATEGY = 2 ... refine all elements whose error is larger
                                         //   than THRESHOLD.
                                         // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO_H; // Predefined list of element refinement candidates. Possible values are
                                         // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                         // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                         // See User Documentation for details.
const int MESH_REGULARITY = -1;          // Maximum allowed level of hanging nodes:
                                         // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                         // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                         // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                         // Note that regular meshes are not supported, this is due to
                                         // their notoriously bad performance.
const double CONV_EXP = 0.5;             // Default value is 1.0. This parameter influences the selection of
                                         // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 1.0;             // Stopping criterion for adaptivity (rel. error tolerance between the
                                         // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;             // Adaptivity process stops when the number of degrees of freedom grows
                                         // over this limit. This is to prevent h-adaptivity to go on forever.

// Problem parameters.
double SLOPE = 60;                       // Slope of the layer inside the domain

// Exact solution.
static double fn(double x, double y)
{
  return atan(SLOPE * (sqrt(sqr(x-1.25) + sqr(y+0.25)) - M_PI/3));
}

static double fndd(double x, double y, double& dx, double& dy)
{
  double t = sqrt(sqr(x-1.25) + sqr(y+0.25));
  double u = t * (sqr(SLOPE) * sqr(t - M_PI/3) + 1);
  dx = SLOPE * (x-1.25) / u;
  dy = SLOPE * (y+0.25) / u;
  return fn(x, y);
}

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_ESSENTIAL;
}
// Essential (Dirichlet) boundary conditions.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return fn(x, y);
}

// Right-hand side.
template<typename Real>
Real rhs(Real x, Real y)
{
  Real t2 = sqr(y + 0.25) + sqr(x - 1.25);
  Real t = sqrt(t2);
  Real u = (sqr(M_PI - 3.0*t)*sqr(SLOPE) + 9.0);
  return 27.0/2.0 * sqr(2.0*y + 0.5) * (M_PI - 3.0*t) * pow(SLOPE,3.0) / (sqr(u) * t2) +
         27.0/2.0 * sqr(2.0*x - 2.5) * (M_PI - 3.0*t) * pow(SLOPE,3.0) / (sqr(u) * t2) -
          9.0/4.0 * sqr(2.0*y + 0.5) * SLOPE / (u * pow(t,3.0)) -
          9.0/4.0 * sqr(2.0*x - 2.5) * SLOPE / (u * pow(t,3.0)) +
          18.0 * SLOPE / (u * t);
}

// Preconditioner weak form.
template<typename Real, typename Scalar>
Scalar precond_form(int n, double *wt, Func<Real> *u_ext[], Func<Real> *vi, Func<Real> *vj, Geom<Real> *e, ExtData<Scalar> *ext)
{
  return int_grad_u_grad_v<Real, Scalar>(n, wt, vi, vj);
}

// Residual weak form.
template<typename Real, typename Scalar>
Scalar residual_form(int n, double *wt, Func<Real> *u_ext[], Func<Real> *vj, Geom<Real> *e, ExtData<Scalar> *ext)
{
  return int_grad_u_grad_v<Real, Scalar>(n, wt, u_ext[0], vj) + int_F_v<Real, Scalar>(n, wt, rhs, vj, e);
}

int main(int argc, char* argv[])
{
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("square.mesh", &mesh);

  // Perform initial mesh refinements.
  for (int i=0; i < INIT_REF_NUM; i++)  mesh.refine_all_elements();

  // Create an H1 space with default shapeset.
  H1Space space(&mesh, bc_types, essential_bc_values, P_INIT);
  //info("Number of DOF: %d", space.get_num_dofs());

  // Initialize the weak formulation.
  WeakForm wf(1, JFNK ? true : false);
  if (PRECOND) wf.add_matrix_form(callback(precond_form), H2D_SYM);
  wf.add_vector_form(callback(residual_form));

  // Initialize views.
  ScalarView sview("Coarse mesh solution", new WinGeom(0, 0, 440, 350));
  OrderView  oview("Coarse mesh", new WinGeom(450, 0, 400, 350));

  // DOF convergence graphs.
  SimpleGraph graph_dof_est, graph_dof_exact;

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Adaptivity loop:
  int as = 1;
  bool done = false;
  Solution sln, ref_sln;
  do
  {
    info("---- Adaptivity step %d:", as);
   
    // Initialize finite element problem.
    FeProblem fep(&wf, &space);

    // Initialize NOX solver.
    NoxSolver solver(&fep);

    // Choose preconditioner.
    RCP<Precond> pc = rcp(new MlPrecond("sa"));
    if (PRECOND)
    {
      if (JFNK) solver.set_precond(pc);
      else solver.set_precond("ML");
    }

    // Assemble on coarse mesh and solve the matrix problem using NOX.
    int ndof = get_num_dofs(&space);
    info("Coarse mesh problem (ndof: %d): Assembling by FeProblem, solving by NOX.", ndof);
    bool solved = solver.solve();
    if (solved)
    {
      double* coeffs = solver.get_solution_vector();
      sln.set_coeff_vector(&space, coeffs);

      info("Coarse Solution info:");
      info(" Number of nonlin iterations: %d (norm of residual: %g)", 
        solver.get_num_iters(), solver.get_residual());
      info(" Total number of iterations in linsolver: %d (achieved tolerance in the last step: %g)", 
        solver.get_num_lin_iters(), solver.get_achieved_tol());

      // Time measurement.
      cpu_time.tick();

      // View the solution and mesh.
      sview.show(&sln);
      oview.show(&space);

      // Skip visualization time.
      cpu_time.tick(HERMES_SKIP);
    }
    else
      error("NOX failed on coarse mesh.");

    // Create uniformly refined reference mesh.
    Mesh rmesh; rmesh.copy(&mesh); 
    rmesh.refine_all_elements();
    // Reference FE space.
    H1Space rspace(&rmesh, bc_types, essential_bc_values, P_INIT);
    int order_increase = 1;
    rspace.copy_orders(&space, order_increase); // increase orders by one

    // Initialize FE problem on reference mesh.
    FeProblem ref_fep(&wf, &rspace);

    // Initialize NOX solver.
    NoxSolver ref_solver(&ref_fep);
    if (PRECOND)
    {
      if (JFNK) ref_solver.set_precond(pc);
      else ref_solver.set_precond("ML");
    }

    // Assemble on fine mesh and solve the matrix problem using NOX.
    ndof = get_num_dofs(&rspace);
    info("Fine mesh problem (ndof: %d): Assembling by FeProblem, solving by NOX.", ndof);
    solved = ref_solver.solve();
    if (solved)
    {
      double* coeffs = ref_solver.get_solution_vector();
      ref_sln.set_coeff_vector(&rspace, coeffs);

      info("Reference solution info:");
      info(" Number of nonlin iterations: %d (norm of residual: %g)",
            ref_solver.get_num_iters(), ref_solver.get_residual());
      info(" Total number of iterations in linsolver: %d (achieved tolerance in the last step: %g)",
            ref_solver.get_num_lin_iters(), ref_solver.get_achieved_tol());
    }
    else
      error("NOX failed on fine mesh.");

    // Calculate element errors.
    info("Calculating error (est).");
    Adapt hp(&space, H2D_H1_NORM);
    hp.set_solutions(&sln, &ref_sln);
    double err_est_rel = hp.calc_elem_errors(H2D_TOTAL_ERROR_REL | H2D_ELEMENT_ERROR_REL) * 100;
 
    // Calculate exact error.
    Solution* exact = new Solution(&mesh, fndd);
    double err_exact_rel = hp.calc_elem_errors(H2D_TOTAL_ERROR_REL | H2D_ELEMENT_ERROR_REL) * 100;

    // Report results.
    info("ndof_coarse: %d, ndof_fine: %d, err_est: %g%%, err_exact: %g%%", 
	 get_num_dofs(&space), get_num_dofs(&rspace), err_est_rel, err_exact_rel);

    // Add entries to DOF convergence graphs.
    graph_dof_exact.add_values(space.get_num_dofs(), err_exact_rel);
    graph_dof_exact.save("conv_dof_exact.dat");
    graph_dof_est.add_values(space.get_num_dofs(), err_est_rel);
    graph_dof_est.save("conv_dof_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel < ERR_STOP) done = true;
    else {
      info("Adapting the coarse mesh.");
      done = hp.adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);

      if (get_num_dofs(&space) >= NDOF_STOP) done = true;
    }

    as++;
  }
  while (done == false);

  // Wait for all views to be closed.
  View::wait();
  return 0;
}
