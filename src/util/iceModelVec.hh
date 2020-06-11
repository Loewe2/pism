// Copyright (C) 2008--2020 Ed Bueler, Constantine Khroulev, and David Maxwell
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

#ifndef __IceModelVec_hh
#define __IceModelVec_hh

#include <initializer_list>
#include <memory>               // shared_ptr
#include <cstdint>              // uint64_t
#include <set>

#include "Vector2.hh"
#include "StarStencil.hh"
#include "pism/util/IceGrid.hh"
#include "pism/util/io/IO_Flags.hh"
#include "pism/pism_config.hh"  // Pism_DEBUG
#include "pism/util/interpolation.hh" // InterpolationType

namespace pism {

class IceGrid;
class File;
class SpatialVariableMetadata;

namespace petsc {
class DM;
class Vec;
class Viewer;
} // end of namespace petsc

//! What "kind" of a vector to create: with or without ghosts.
enum IceModelVecKind {WITHOUT_GHOSTS=0, WITH_GHOSTS=1};

struct Range {
  double min, max;
};

// NB: Do not change the order of elements in this struct. IceModelVec2S::box() and
// IceModelVec2Int::int_box() depend on it.
template <typename T>
struct BoxStencil {
  T ij, n, nw, w, sw, s, se, e, ne;
};

class PetscAccessible {
public:
  virtual ~PetscAccessible() {}
  virtual void begin_access() const = 0;
  virtual void end_access() const = 0;
};

//! Makes sure that we call begin_access() and end_access() for all accessed IceModelVecs.
class AccessList {
public:
  AccessList();
  AccessList(std::initializer_list<const PetscAccessible *> vecs);
  AccessList(const PetscAccessible &v);
  ~AccessList();
  void add(const PetscAccessible &v);
  void add(const std::vector<const PetscAccessible*> vecs);
private:
  std::vector<const PetscAccessible*> m_vecs;
};

/*!
 * Interpolation helper. Does not check if points needed for interpolation are within the current
 * processor's sub-domain.
 */
template<class F, typename T>
T interpolate(const F &field, double x, double y) {
  auto grid = field.grid();

  int i_left = 0, i_right = 0, j_bottom = 0, j_top = 0;
  grid->compute_point_neighbors(x, y, i_left, i_right, j_bottom, j_top);

  auto w = grid->compute_interp_weights(x, y);

  return (w[0] * field(i_left,  j_bottom) +
          w[1] * field(i_right, j_bottom) +
          w[2] * field(i_right, j_top) +
          w[3] * field(i_left,  j_top));
}

//! \brief Abstract class for reading, writing, allocating, and accessing a
//! DA-based PETSc Vec (2D and 3D fields) from within IceModel.
/*!
  @anchor icemodelvec_use

  This class represents 2D and 3D fields in PISM. Its methods common to all
  the derived classes can be split (roughly) into six kinds:

  - memory allocation (create)
  - point-wise access (begin_access(), end_access())
  - arithmetic (range(), norm(), add(), shift(), scale(), set(), ...)
  - setting or reading metadata (set_attrs(), metadata())
  - file input/output (read, write, regrid)
  - tracking whether a field was updated (get_state_counter(), inc_state_counter())

  ## Memory allocation

  Creating an IceModelVec... object does not allocate memory for storing it
  (some IceModelVecs serve as "references" and don't have their own storage).
  To complete IceModelVec... creation, use the "create()" method:

  \code
  IceModelVec2S var;
  ierr = var.create(grid, "var_name", WITH_GHOSTS); CHKERRQ(ierr);
  // var is ready to use
  \endcode

  ("WITH_GHOSTS" means "can be used in computations using map-plane neighbors
  of grid points.)

  It is usually a good idea to set variable metadata right after creating it.
  The method set_attrs() is used throughout PISM to set commonly used
  attributes.

  ## Point-wise access

  PETSc performs some pointer arithmetic magic to allow convenient indexing of
  grid point values. Because of this one needs to surround the code using row,
  column or level indexes with begin_access() and end_access() calls:

  \code
  double foo;
  int i = 0, j = 0;
  IceModelVec2S var;
  // assume that var was allocated
  ierr = var.begin_access(); CHKERRQ(ierr);
  foo = var(i,j) * 2;
  ierr = var.end_access(); CHKERRQ(ierr);
  \endcode

  Please see [this page](@ref computational_grid) for a discussion of the
  organization of PISM's computational grid and examples of for-loops you will
  probably put between begin_access() and end_access().

  To ensure that ghost values are up to date add the following call
  before the code using ghosts:

  \code
  ierr = var.update_ghosts(); CHKERRQ(ierr);
  \endcode

  ## Reading and writing variables

  PISM can read variables either from files with data on a grid matching the
  current grid (read()) or, using bilinear interpolation, from files
  containing data on a different (but compatible) grid (regrid()).

  To write a field to a "prepared" NetCDF file, use write(). (A file is prepared
  if it contains all the necessary dimensions, coordinate variables and global
  metadata.)

  If you need to "prepare" a file, do:
  \code
  File file(grid.com, PISM_NETCDF3);
  io::prepare_for_output(file, *grid.ctx());
  \endcode

  A note about NetCDF write performance: due to limitations of the NetCDF
  (classic, version 3) format, it is significantly faster to
  \code
  for (all variables)
  var.define(...);

  for (all variables)
  var.write(...);
  \endcode

  as opposed to

  \code
  for (all variables) {
  var.define(...);
  var.write(...);
  }
  \endcode

  IceModelVec::define() is here so that we can use the first approach.

  ## Tracking if a field changed

  It is possible to track if a certain field changed with the help of
  get_state_counter() and inc_state_counter() methods.

  For example, PISM's SIA code re-computes the smoothed bed only if the bed
  deformation code updated it:

  \code
  if (bed->get_state_counter() > bed_state_counter) {
  ierr = bed_smoother->preprocess_bed(...); CHKERRQ(ierr);
  bed_state_counter = bed->get_state_counter();
  }
  \endcode

  The state counter is **not** updated automatically. For the code snippet above
  to work, a bed deformation model has to call inc_state_counter() after an
  update.
*/
class IceModelVec : public PetscAccessible {
public:
  IceModelVec();
  virtual ~IceModelVec();

  typedef std::shared_ptr<IceModelVec> Ptr;
  typedef std::shared_ptr<const IceModelVec> ConstPtr;

  IceGrid::ConstPtr grid() const;
  unsigned int ndims() const;
  std::vector<int> shape() const;
  //! @brief Returns the number of degrees of freedom per grid point.
  unsigned int ndof() const;
  unsigned int stencil_width() const;
  std::vector<double> levels() const;

  virtual Range range() const;
  double norm(int n) const;
  std::vector<double> norm_all(int n) const;

  virtual void add(double alpha, const IceModelVec &x);
  virtual void shift(double alpha);
  virtual void scale(double alpha);

  void copy_from_vec(petsc::Vec &source);
  virtual void copy_from(const IceModelVec &source);
  petsc::Vec& vec();
  std::shared_ptr<petsc::DM> dm() const;

  virtual void set_name(const std::string &name);
  const std::string& get_name() const;

  void set_attrs(const std::string &pism_intent,
                 const std::string &long_name,
                 const std::string &units,
                 const std::string &glaciological_units,
                 const std::string &standard_name,
                 unsigned int component);

  virtual void read_attributes(const std::string &filename, int component = 0);
  virtual void define(const File &nc, IO_Type default_type = PISM_DOUBLE) const;

  void read(const std::string &filename, unsigned int time);
  void read(const File &nc, unsigned int time);

  void write(const std::string &filename) const;
  void write(const File &nc) const;

  void regrid(const std::string &filename, RegriddingFlag flag,
              double default_value = 0.0);
  void regrid(const File &nc, RegriddingFlag flag,
              double default_value = 0.0);

  virtual void begin_access() const;
  virtual void end_access() const;
  virtual void update_ghosts();
  virtual void update_ghosts(IceModelVec &destination) const;

  std::shared_ptr<petsc::Vec> allocate_proc0_copy() const;
  void put_on_proc0(petsc::Vec &onp0) const;
  void get_from_proc0(petsc::Vec &onp0);

  void set(double c);

  SpatialVariableMetadata& metadata(unsigned int N = 0);

  const SpatialVariableMetadata& metadata(unsigned int N = 0) const;

  int state_counter() const;
  void inc_state_counter();
  void set_time_independent(bool flag);

protected:
  struct Impl;
  Impl *m_impl;

  // will be cast to double**, double***, or Vector2** in derived classes
  // This is not hidden in m_impl to make it possible to inline operator()
  mutable void *m_array;

  void set_begin_access_use_dof(bool flag);

  virtual void read_impl(const File &nc, unsigned int time);
  virtual void regrid_impl(const File &nc, RegriddingFlag flag,
                                     double default_value = 0.0);
  virtual void write_impl(const File &nc) const;

  virtual void checkCompatibility(const char *function, const IceModelVec &other) const;

  //! @brief Check array indices and warn if they are out of range.
  void check_array_indices(int i, int j, unsigned int k) const;
  void reset_attrs(unsigned int N);

  void copy_to_vec(std::shared_ptr<petsc::DM> destination_da, petsc::Vec &destination) const;
  void get_dof(std::shared_ptr<petsc::DM> da_result, petsc::Vec &result, unsigned int n,
               unsigned int count=1) const;
  void set_dof(std::shared_ptr<petsc::DM> da_source, petsc::Vec &source, unsigned int n,
               unsigned int count=1);
private:
  size_t size() const;
  // disable copy constructor and the assignment operator:
  IceModelVec(const IceModelVec &other);
  IceModelVec& operator=(const IceModelVec&);
public:
  //! Dump an IceModelVec to a file. *This is for debugging only.*
  //! Uses const char[] to make it easier to call it from gdb.
  void dump(const char filename[]) const;

  uint64_t fletcher64() const;
  std::string checksum() const;
  void print_checksum(const char *prefix = "") const;

  typedef pism::AccessList AccessList;
protected:
  void put_on_proc0(petsc::Vec &parallel, petsc::Vec &onp0) const;
  void get_from_proc0(petsc::Vec &onp0, petsc::Vec &parallel);
};

bool set_contains(const std::set<std::string> &S, const IceModelVec &field);

class IceModelVec2S;

/** Class for a 2d DA-based Vec.

    As for the difference between IceModelVec2 and IceModelVec2S, the
    former can store fields with more than 1 "degree of freedom" per grid
    point (such as 2D fields on the "staggered" grid, with the first
    degree of freedom corresponding to the i-offset and second to
    j-offset). */
class IceModelVec2 : public IceModelVec {
public:
  IceModelVec2();
  IceModelVec2(IceGrid::ConstPtr grid, const std::string &short_name,
               IceModelVecKind ghostedp, unsigned int stencil_width, int dof);

  typedef std::shared_ptr<IceModelVec2> Ptr;
  typedef std::shared_ptr<const IceModelVec2> ConstPtr;

  static Ptr To2D(IceModelVec::Ptr input);

  virtual void view(int viewer_size) const;
  virtual void view(std::shared_ptr<petsc::Viewer> v1,
                    std::shared_ptr<petsc::Viewer> v2) const;
  // component-wise access:
  virtual void get_component(unsigned int n, IceModelVec2S &result) const;
  virtual void set_component(unsigned int n, const IceModelVec2S &source);
  inline double& operator() (int i, int j, int k);
  inline const double& operator() (int i, int j, int k) const;
  void create(IceGrid::ConstPtr grid, const std::string &short_name,
              IceModelVecKind ghostedp, unsigned int stencil_width, int dof);
protected:
  virtual void read_impl(const File &nc, const unsigned int time);
  virtual void regrid_impl(const File &nc, RegriddingFlag flag,
                                     double default_value = 0.0);
  virtual void write_impl(const File &nc) const;
};

//! A "fat" storage vector for combining related fields (such as SSAFEM coefficients).
template<typename T>
class IceModelVec2Fat : public IceModelVec2 {
public:
  IceModelVec2Fat(IceGrid::ConstPtr grid, const std::string &short_name,
                  IceModelVecKind ghostedp, unsigned int stencil_width = 1)
    : IceModelVec2(grid, short_name, ghostedp, stencil_width,
                   sizeof(T) / sizeof(double)) {
    set_begin_access_use_dof(false);
  }

  T** array() {
    return reinterpret_cast<T**>(m_array);
  }

  inline T& operator()(int i, int j) {
#if (Pism_DEBUG==1)
    check_array_indices(i, j, 0);
#endif
    return static_cast<T**>(m_array)[j][i];
  }

  inline const T& operator()(int i, int j) const {
#if (Pism_DEBUG==1)
    check_array_indices(i, j, 0);
#endif
    return static_cast<T**>(m_array)[j][i];
  }

  inline StarStencil<T> star(int i, int j) const {
    const auto &self = *this;

    StarStencil<T> result;

    result.ij = self(i,j);
    result.e =  self(i+1,j);
    result.w =  self(i-1,j);
    result.n =  self(i,j+1);
    result.s =  self(i,j-1);

    return result;
  }

};

class IceModelVec2V;

/** A class for storing and accessing scalar 2D fields.
    IceModelVec2S is just IceModelVec2 with "dof == 1" */
class IceModelVec2S : public IceModelVec2 {
  friend class IceModelVec2V;
  friend class IceModelVec2Stag;
public:
  IceModelVec2S();
  IceModelVec2S(IceGrid::ConstPtr grid, const std::string &name,
                IceModelVecKind ghostedp, int width = 1);

  typedef std::shared_ptr<IceModelVec2S> Ptr;
  typedef std::shared_ptr<const IceModelVec2S> ConstPtr;

  static Ptr To2DScalar(IceModelVec::Ptr input);

  /*!
   * Interpolation helper. See the pism::interpolate() for details.
   */
  double interpolate(double x, double y) const {
    return pism::interpolate<IceModelVec2S, double>(*this, x, y);
  }

  // does not need a copy constructor, because it does not add any new data members
  using IceModelVec2::create;
  void create(IceGrid::ConstPtr grid, const std::string &name,
              IceModelVecKind ghostedp, int width = 1);
  virtual void copy_from(const IceModelVec &source);
  double** array();
  double const* const* array() const;
  virtual void set_to_magnitude(const IceModelVec2S &v_x, const IceModelVec2S &v_y);
  virtual void set_to_magnitude(const IceModelVec2V &input);
  virtual void mask_by(const IceModelVec2S &M, double fill = 0.0);
  virtual void add(double alpha, const IceModelVec &x);
  virtual void add(double alpha, const IceModelVec &x, IceModelVec &result) const;
  virtual double sum() const;
  virtual double min() const;
  virtual double max() const;
  virtual double absmax() const;
  virtual double diff_x(int i, int j) const;
  virtual double diff_y(int i, int j) const;
  virtual double diff_x_p(int i, int j) const;
  virtual double diff_y_p(int i, int j) const;

  //! Provides access (both read and write) to the internal double array.
  /*!
    Note that i corresponds to the x direction and j to the y.
  */
  inline double& operator() (int i, int j);
  inline const double& operator()(int i, int j) const;
  inline StarStencil<double> star(int i, int j) const;
  inline BoxStencil<double> box(int i, int j) const;
};


//! \brief A simple class "hiding" the fact that the mask is stored as
//! floating-point scalars (instead of integers).
class IceModelVec2Int : public IceModelVec2S {
public:
  IceModelVec2Int();
  IceModelVec2Int(IceGrid::ConstPtr grid, const std::string &name,
                  IceModelVecKind ghostedp, int width = 1);

  typedef std::shared_ptr<IceModelVec2Int> Ptr;
  typedef std::shared_ptr<const IceModelVec2Int> ConstPtr;

  inline int as_int(int i, int j) const;
  inline StarStencil<int> int_star(int i, int j) const;
  inline BoxStencil<int> int_box(int i, int j) const;
};

/** Class for storing and accessing 2D vector fields used in IceModel.
    IceModelVec2V is IceModelVec2 with "dof == 2". (Plus some extra methods, of course.)
*/
class IceModelVec2V : public IceModelVec2 {
public:
  IceModelVec2V();
  IceModelVec2V(IceGrid::ConstPtr grid, const std::string &short_name,
                IceModelVecKind ghostedp, unsigned int stencil_width = 1);
  ~IceModelVec2V();

  typedef std::shared_ptr<IceModelVec2V> Ptr;
  typedef std::shared_ptr<const IceModelVec2V> ConstPtr;

  static Ptr ToVector(IceModelVec::Ptr input);

  void create(IceGrid::ConstPtr grid, const std::string &short_name,
              IceModelVecKind ghostedp, unsigned int stencil_width = 1);
  virtual void copy_from(const IceModelVec &source);
  virtual void add(double alpha, const IceModelVec &x);
  virtual void add(double alpha, const IceModelVec &x, IceModelVec &result) const;

  // I/O:
  Vector2** array();
  inline Vector2& operator()(int i, int j);
  inline const Vector2& operator()(int i, int j) const;
  inline StarStencil<Vector2> star(int i, int j) const;

  /*!
   * Interpolation helper. See the pism::interpolate() for details.
   */
  Vector2 interpolate(double x, double y) const {
    return pism::interpolate<IceModelVec2V, Vector2>(*this, x, y);
  }
};

//! \brief A class for storing and accessing internal staggered-grid 2D fields.
//! Uses dof=2 storage. This class is identical to IceModelVec2V, except that
//! components are not called `u` and `v` (to avoid confusion).
class IceModelVec2Stag : public IceModelVec2 {
public:
  IceModelVec2Stag(IceGrid::ConstPtr grid, const std::string &short_name,
                   IceModelVecKind ghostedp, unsigned int stencil_width = 1);

  typedef std::shared_ptr<IceModelVec2Stag> Ptr;
  typedef std::shared_ptr<const IceModelVec2Stag> ConstPtr;

  static Ptr ToStaggered(IceModelVec::Ptr input);

  void create(IceGrid::ConstPtr grid, const std::string &short_name,
              IceModelVecKind ghostedp, unsigned int stencil_width = 1);
  virtual void staggered_to_regular(IceModelVec2S &result) const;
  virtual void staggered_to_regular(IceModelVec2V &result) const;
  virtual std::vector<double> absmaxcomponents() const;

  //! Returns the values at interfaces of the cell i,j using the staggered grid.
  /*! The ij member of the return value is set to 0, since it has no meaning in
    this context.
  */
  inline StarStencil<double> star(int i, int j) const;
};

//! \brief A virtual class collecting methods common to ice and bedrock 3D
//! fields.
class IceModelVec3D : public IceModelVec {
public:
  IceModelVec3D();
  virtual ~IceModelVec3D();

  void set_column(int i, int j, double c);
  void set_column(int i, int j, const double *valsIN);
  double* get_column(int i, int j);
  const double* get_column(int i, int j) const;

  // testing methods (for use from Python)
  void set_column(int i, int j, const std::vector<double> &valsIN);
  const std::vector<double> get_column_vector(int i, int j) const;

  virtual double getValZ(int i, int j, double z) const;
  virtual bool isLegalLevel(double z) const;

  inline double& operator() (int i, int j, int k);
  inline const double& operator() (int i, int j, int k) const;
protected:
  void allocate(IceGrid::ConstPtr mygrid, const std::string &short_name,
                IceModelVecKind ghostedp, const std::vector<double> &levels,
                unsigned int stencil_width = 1);
};


//! Class for a 3d DA-based Vec for ice scalar quantities.
class IceModelVec3 : public IceModelVec3D {
public:
  IceModelVec3();
  IceModelVec3(IceGrid::ConstPtr mygrid, const std::string &short_name,
               IceModelVecKind ghostedp,
               unsigned int stencil_width = 1);

  virtual ~IceModelVec3();

  typedef std::shared_ptr<IceModelVec3> Ptr;
  typedef std::shared_ptr<const IceModelVec3> ConstPtr;

  static Ptr To3DScalar(IceModelVec::Ptr input);

  void create(IceGrid::ConstPtr mygrid, const std::string &short_name,
              IceModelVecKind ghostedp,
              unsigned int stencil_width = 1);

  void  getHorSlice(IceModelVec2S &gslice, double z) const;
  void  getSurfaceValues(IceModelVec2S &gsurf, const IceModelVec2S &myH) const;

  void sumColumns(IceModelVec2S &output, double A, double B) const;
};

/**
 * Convert a PETSc Vec from the units in `from` into units in `to` (in place).
 *
 * @param v data to convert
 * @param system unit system
 * @param spec1 source unit specification string
 * @param spec2 destination unit specification string
 */
void convert_vec(petsc::Vec &v, std::shared_ptr<units::System> system,
                 const std::string &spec1, const std::string &spec2);

class IceModelVec2CellType;

/*!
 * Average a scalar field from the staggered grid onto the regular grid by considering
 * only ice-covered grid.
 *
 * If `include_floating_ice` is true, include floating ice, otherwise consider grounded
 * icy cells only.
 */
void staggered_to_regular(const IceModelVec2CellType &cell_type,
                          const IceModelVec2Stag &input,
                          bool include_floating_ice,
                          IceModelVec2S &result);

/*!
 * Average a vector field from the staggered grid onto the regular grid by considering
 * only ice-covered grid.
 *
 * If `include_floating_ice` is true, include floating ice, otherwise consider grounded
 * icy cells only.
 */
void staggered_to_regular(const IceModelVec2CellType &cell_type,
                          const IceModelVec2Stag &input,
                          bool include_floating_ice,
                          IceModelVec2V &result);

} // end of namespace pism

// include inline methods; contents are wrapped in namespace pism {...}
#include "IceModelVec_inline.hh"

#endif /* __IceModelVec_hh */
