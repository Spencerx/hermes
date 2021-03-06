// This file is part of Hermes2D.
//
// Hermes2D is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Hermes2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Hermes2D.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __H2D_MESH_H
#define __H2D_MESH_H

#include "common.h"
#include "curved.h"

struct Element;
class HashTable;
class Space;
struct MItem;


/// \brief Stores one node of a mesh.
///
/// There are are two variants of this structure, depending on the value of
/// the member 'type':
/// <ol> <li> H2D_TYPE_VERTEX -- vertex node. Has physical coordinates x, y.
///      <li> H2D_TYPE_EDGE   -- edge node. Only stores edge marker and two element pointers.
/// </ol>
///
struct H2D_API Node
{
  int id;          ///< node id number
  unsigned ref:29; ///< the number of elements using the node
  unsigned type:1; ///< 0 = vertex node; 1 = edge node
  unsigned bnd:1;  ///< 1 = boundary node; 0 = inner node
  unsigned used:1; ///< array item usage flag

  union
  {
    struct // vertex node variant:
    {
      double x, y; ///< vertex node coordinates
    };
    struct // edge node variant:
    {// TODO: review pointer sizes for 64-bits !!!
      int marker;       ///< edge marker
      Element* elem[2]; ///< elements sharing the edge node
      Nurbs* nurbs;     ///< temporary curved edge ptr (only for loading the mesh)
    };
  };

  int p1, p2; ///< parent id numbers
  Node* next_hash; ///< next node in hash synonym list

  bool is_constrained_vertex() const { assert(type == H2D_TYPE_VERTEX); return ref <= 3 && !bnd; }

  void ref_element(Element* e = NULL);
  void unref_element(HashTable* ht, Element* e = NULL);
};


/// \brief Stores one element of a mesh.
///
/// The element can be a triangle or a quad (nvert == 3 or nvert = 4), active or inactive.
///
/// Vertex/node index number
///        [2]
///   (3)-------(2)
///    |         |
/// [3]|  quad.  |[1]
///    |         |
///   (0)-------(1)
///        [0]
/// Active elements are actual existing elements in the mesh, which take part in the
/// computation. Inactive elements are those which have once been active, but were refined,
/// ie., replaced by several other (smaller) son elements. The purpose of the union is the
/// following. Active elements store pointers to their vertex and edge nodes. Inactive
/// elements store pointers to thier son elements and to vertex nodes.
///
/// If an element has curved edges, the member 'cm' points to an associated CurvMap structure,
/// otherwise it is NULL.
///
struct H2D_API Element
{
  int id;            ///< element id number
  unsigned nvert:30; ///< number of vertices (3 or 4)
  unsigned active:1; ///< 0 = active, no sons; 1 = inactive (refined), has sons
  unsigned used:1;   ///< array item usage flag
  int marker;        ///< element marker
  int userdata;     ///< arbitrary user-defined data
  int iro_cache;     ///< increase in integration order, see RefMap::calc_inv_ref_order()
  Element* parent;   ///< pointer to the parent element for the current son
  bool visited;      ///< true if the element has been visited during assembling


  Node* vn[4];   ///< vertex node pointers
  union
  {
    Node* en[4];      ///< edge node pointers
    Element* sons[4]; ///< son elements (up to four)
  };

  CurvMap* cm; ///< curved mapping, NULL if not curvilinear

  bool is_triangle() const { return nvert == 3; }
  bool is_quad() const { return nvert == 4; }
  bool is_curved() const { return cm != NULL; }
  int  get_mode() const { return is_triangle() ? H2D_MODE_TRIANGLE : H2D_MODE_QUAD; }

  // helper functions to obtain the index of the next or previous vertex/edge
  int next_vert(int i) const { return (i < (int)nvert-1) ? i+1 : 0; }
  int prev_vert(int i) const { return (i > 0) ? i-1 : nvert-1; }

  bool hsplit() const { assert(!active); return sons[0] != NULL; }
  bool vsplit() const { assert(!active); return sons[2] != NULL; }
  bool bsplit() const { assert(!active); return sons[0] != NULL && sons[2] != NULL; }

  /// Returns a pointer to the neighboring element across the edge 'ie', or
  /// NULL if it does not exist or is across an irregular edge.
  Element* get_neighbor(int ie) const;

  /// Calculates the area of the element. For curved elements, this is only
  /// an approximation: the curvature is not accounted for.
  double get_area() const;

  /// Returns the length of the longest edge for triangles, and the
  /// length of the longer diagonal for quads. Ignores element curvature.
  double get_diameter() const;

  // returns the edge orientation. This works for the unconstrained edges.
  int get_edge_orientation(int ie) {
      return (this->vn[ie]->id < this->vn[this->next_vert(ie)]->id) ? 0 : 1;
  }

  void ref_all_nodes();
  void unref_all_nodes(HashTable* ht);
};


#include "hash.h"


/// \brief Represents a finite element mesh.
///
///
///
class H2D_API Mesh : public HashTable
{
public:

  Mesh();
  ~Mesh() { 
    //printf("Calling Mesh::free() in ~Mesh().\n");
    free(); 
    dump_hash_stat(); 
  }
  /// Creates a copy of another mesh.
  void copy(const Mesh* mesh);
  /// Copies the coarsest elements of another mesh.
  void copy_base(Mesh* mesh);
  /// Copies the active elements of a converted mesh.
  void copy_converted(Mesh* mesh);
  /// Frees all data associated with the mesh.
  void free();

  /// Loads the mesh from a file. Aborts the program on error.
  /// \param filename [in] The name of the file. DEPRECATED
  void load_old(const char* filename);
  void load_stream(FILE *f);
  void load_str(char* mesh);

  /// DEPRECATED
  void load(const char* filename, bool debug = false);

  /// Saves the mesh, including all refinements, to a file.
  /// Caution: never overwrite hand-created meshes with this function --
  /// all comments in the original file will be lost. DEPRECATED.
  /// \param filename [in] The name of the file.
  void save(const char* filename);

  /// Creates the mesh from the given vertex, triangle, quad and marker arrays
  void create(int nv, double2* verts, int nt, int4* tris,
              int nq, int5* quads, int nm, int3* mark);

  /// Retrieves an element by its id number.
  Element* get_element(int id) const;

  /// Returns the total number of elements stored.
  int get_num_elements() const { 
    if (this == NULL) error("this == NULL in Mesh::get_num_elements()."); 
    return elements.get_num_items(); 
  }
  /// Returns the number of coarse mesh elements.
  int get_num_base_elements() const { 
    if (this == NULL) error("this == NULL in Mesh::get_num_base_elements()."); 
    return nbase; 
  }
  /// Returns the current number of active elements in the mesh.
  int get_num_active_elements() const { 
    if (this == NULL) error("this == NULL in Mesh::get_num_active_elements()."); 
    return nactive; 
  }
  /// Returns the maximum node id number plus one.
  int get_max_element_id() const { 
    if (this == NULL) error("this == NULL in Mesh::get_max_element_id()."); 
    return elements.get_size(); 
  }

  /// Refines an element.
  /// \param id [in] Element id number.
  /// \param refinement [in] Ignored for triangles. If the element
  /// is a quad, 0 means refine in both directions, 1 means refine
  /// horizontally (with respect to the reference domain), 2 means
  /// refine vertically.
  void refine_element(int id, int refinement = 0);

  /// Refines all elements.
  /// \param refinement [in] Same meaning as in refine_element().
  void refine_all_elements(int refinement = 0);

  /// Selects elements to refine according to a given criterion and
  /// performs 'depth' levels of refinements. The criterion function
  /// receives a pointer to an element to be considered.
  /// It must return -1 if the element is not to be refined, 0 if it
  /// should be refined uniformly, 1 if it is a quad and should be split
  /// horizontally or 2 if it is a quad and should be split vertically.
  void refine_by_criterion(int (*criterion)(Element* e), int depth);

  /// Performs repeated refinements of elements containing the given vertex.
  /// A mesh graded towards the vertex is created.
  void refine_towards_vertex(int vertex_id, int depth);

  /// Performs repeated refinements of elements touching a part of the
  /// boundary marked by 'marker'. Elements touching both by an edge or
  /// by a vertex are refined. 'aniso' allows or disables anisotropic
  /// splits of quads.
  void refine_towards_boundary(int marker, int depth, bool aniso = true);

  /// Regularizes the mesh by refining elements with hanging nodes of
  /// degree more than 'n'. As a result, n-irregular mesh is obtained.
  /// If n = 0, completely regular mesh is created. In this case, however,
  /// due to incompatible refinements, the element refinement hierarchy
  /// is removed and all elements become top-level elements. Also, total
  /// regularization does not work on curved elements.
  /// Returns an array of new element parents which can be passed to
  /// Space::distribute_orders(). The array must be deallocated with ::free().
  int* regularize(int n);

  /// Recursively removes all son elements of the given element and
  /// makes it active.
  void unrefine_element(int id);

  /// Unrefines all elements with immediate active sons. In effect, this
  /// shaves off one layer of refinements from the mesh. If done immediately
  /// after refine_all_elements(), this function reverts the mesh to its
  /// original state. However, it is not exactly an inverse to
  /// refine_all_elements().
  void unrefine_all_elements(bool keep_initial_refinements = true);

  void transform(double2x2 m, double2 t);
  void transform(void (*fn)(double* x, double* y));

  /// Loads the entire internal state from a (binary) file. DEPRECATED
  void load_raw(FILE* f);
  /// Saves the entire internal state to a (binary) file. DEPRECATED
  void save_raw(FILE* f);

  /// For internal use.
  int get_edge_sons(Element* e, int edge, int& son1, int& son2);
  /// For internal use.
  unsigned get_seq() const { return seq; }
  /// For internal use.
  void set_seq(unsigned seq) { this->seq = seq; }
  /// For internal use.
  Element* get_element_fast(int id) const { return &(elements[id]);}
  /// Refines all triangle elements to quads.
  /// It can refine a triangle element into three quadrilaterals.
  /// Note: this function creates a base mesh -- it can only be 
  /// used before any other mesh refinement function is called.
  void convert_triangles_to_quads();
  /// Refines all quad elements to triangles.
  /// It refines a quadrilateral element into two triangles.
  /// Note: this function creates a base mesh -- it can only be 
  /// used before any other mesh refinement function is called.
  void convert_quads_to_triangles();

protected:
  H2D_API_USED_TEMPLATE(Array<Element>);
  Array<Element> elements;
  int nbase, ntopvert;
  int nactive, ninitial;
  unsigned seq;

  Element* create_triangle(int marker, Node* v0, Node* v1, Node* v2, CurvMap* cm);
  Element* create_quad(int marker, Node* v0, Node* v1, Node* v2, Node* v3, CurvMap* cm);

  void refine_triangle(Element* e);
  void refine_quad(Element* e, int refinement);
  void unrefine_element_internal(Element* e);

  Nurbs* reverse_nurbs(Nurbs* nurbs);
  Node*  get_base_edge_node(Element* base, int edge);

  int* parents;
  int parents_size;

  int  get_edge_degree(Node* v1, Node* v2);
  void assign_parent(Element* e, int i);
  void regularize_triangle(Element* e);
  void regularize_quad(Element* e);
  void flatten();

  void refine_triangle_to_quads(Element* e);
  void refine_element_to_quads(int id);

  void refine_quad_to_triangles(Element* e);
  void refine_element_to_triangles(int id);

  friend class H2DReader;
};


// helper macros for easy iteration through all elements, nodes etc. in a mesh
#define for_all_elements(e, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_element_id(); _id < _max; _id++) \
          if (((e) = (mesh)->get_element_fast(_id))->used)

#define for_all_base_elements(e, mesh) \
        for (int _id = 0; _id < (mesh)->get_num_base_elements(); _id++) \
          if (((e) = (mesh)->get_element_fast(_id))->used)

#define for_all_active_elements(e, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_element_id(); _id < _max; _id++) \
          if (((e) = (mesh)->get_element_fast(_id))->used) \
            if ((e)->active)

#define for_all_inactive_elements(e, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_element_id(); _id < _max; _id++) \
          if (((e) = (mesh)->get_element_fast(_id))->used) \
            if (!(e)->active)

#define for_all_nodes(n, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_node_id(); _id < _max; _id++) \
          if (((n) = (mesh)->get_node(_id))->used)

#define for_all_vertex_nodes(n, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_node_id(); _id < _max; _id++) \
          if (((n) = (mesh)->get_node(_id))->used) \
            if (!(n)->type)

#define for_all_edge_nodes(n, mesh) \
        for (int _id = 0, _max = (mesh)->get_max_node_id(); _id < _max; _id++) \
          if (((n) = (mesh)->get_node(_id))->used) \
            if ((n)->type)

/// \brief Determines the position on an edge.
/// \details Used for the retrieval of boundary condition values.
///
struct EdgePos
{
  int v1, v2; ///< edge endpoint vertex id numbers
  int marker; ///< edge marker
  int edge;   ///< element edge number (0..2 for triangles, 0..3 for quads)
  double t;   ///< position between v1 and v2 in the range [0..1]

  double lo, hi; ///< for internal use
  Element* base; ///< for internal use
  Space *space, *space_u, *space_v; ///< for internal use
};


const int TOP_LEVEL_REF = 123456;


// General purpose geometry functions

double vector_length(double a_1, double a_2);
bool same_line(double p_1, double p_2, double q_1, double q_2, double r_1, double r_2);
bool is_convex(double a_1, double a_2, double b_1, double b_2);
void check_triangle(int i, Node *&v0, Node *&v1, Node *&v2);
void check_quad(int i, Node *&v0, Node *&v1, Node *&v2, Node *&v3);

#endif
