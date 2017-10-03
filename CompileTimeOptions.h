/* If USE_ALTERNATE_TETRAHEDRA is defined, a reduced set of tetrahedra
 * is used to subdivide higher symmetries. This may provide less information 
 * about the geometry when used but should improve performance as fewer 
 * tetrahedron volumes must be calculated.
 */
//#define USE_ALTERNATE_TETRAHEDRA

/* If USE_CONSTEXPR_SQUARE_ANTIPRISMATIC_LOOKUP_TABLE is defined, a table of all
 * angles resulting from a predefined set of positions is generated and that
 * symmetry's angle function turns into what is essentially a lookup table.
 */
//#define USE_CONSTEXPR_SQUARE_ANTIPRISMATIC_LOOKUP_TABLE

/* If USE_CONSTEXPR_TRANSITION_MAPPINGS is defined, a data structure containing
 * the best index mappings between symmetries is generated at compile-time.
 * Transitions are alterations of the symmetry primitives in three-dimensional
 * space which can involve loss of a ligand position, merely a rearrangement to
 * another spatial symmetry of the same size, or the gain of a ligand position.
 *
 * Defining this preprocessor variable is currently discouraged since the
 * compile-time cost introduced is excessive in comparison to the runtime cost.
 * Although the program is well-formed, GCC version 7.2.0 does not recognize it
 * as such due to compiler bugs. Circumventing the bugs with additional
 * measures increases the compilation cost to at least 32 GB of memory, at
 * which compilation is aborted and considered failed here. Clang 4.0.0 can
 * compile the full library of dynamic and constexpr algorithms and execute
 * them at compile-time to generate a subset of the complete data structure
 * (transitions up to and within symmetries of size 5) within roughly a minute,
 * but a full treatment of symmetries up to size 8 would take a minimum of 41
 * hours.
 *
 * Executing the same constexpr functions at runtime and comparing them with the
 * implementations directly intended for runtime use takes around 3 minutes.
 *
 * To the implementor, it is unclear what causes this enormous disparity in
 * execution time needed at compile time and run time, when the algorithms being
 * executed are clearly the same. In GCC, memoization of the arguments and
 * results of constexpr functions could be the cause of ballooning memory costs,
 * since nothing of the sort is incurred in Clang, which does not memoize calls.
 *
 * The constexpr algorithm laid bare here is the first such program of the
 * author, and thus the costs of the compile-time branching and instantiation
 * methods are most likely not optimal.
 *
 * See the Rule of Chiel, from Odin Holmes' "Type Based Metaprogramming is not
 * Dead" talk at C++Now 2017: https://www.youtube.com/watch?v=EtU4RDCCsiU&t=9m08
 *
 * Certainly, the constexpr algorithm requires many type instantiations since
 * array-like types are reinstantiated for any new maximum size estimate, heavy
 * use of SFINAE is made for compile-time if expressions, and there are a great
 * number of function templates around, which could explain a heavy cost of
 * compiling the functions, but there is no such cost. The cost comes from
 * executing the constexpr functions *at compile time* instead of at runtime.
 */
//#define USE_CONSTEXPR_TRANSITION_MAPPINGS
