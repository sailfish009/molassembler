/*!@file
 * @copyright This code is licensed under the 3-clause BSD license.
 *   Copyright ETH Zurich, Laboratory for Physical Chemistry, Reiher Group.
 *   See LICENSE.txt
 */

#include "boost/test/unit_test.hpp"

#include "molassembler/DistanceGeometry/DistanceBoundsMatrix.h"
#include "molassembler/DistanceGeometry/TetrangleSmoothing.h"

using namespace Scine;
using namespace molassembler;
using namespace distance_geometry;



BOOST_AUTO_TEST_CASE(TriangleSmoothingFloydExplicit) {
  Eigen::Matrix4d input;
  input <<   0.0,   1.0, 100.0,   1.0,
             1.0,   0.0,   1.0, 100.0,
             0.5,   1.0,   0.0,   1.0,
             1.0,   0.5,   1.0,   0.0;

  Eigen::Matrix4d expected;
  expected << 0.0, 1.0, 2.0, 1.0,
              1.0, 0.0, 1.0, 2.0,
              0.5, 1.0, 0.0, 1.0,
              1.0, 0.5, 1.0, 0.0;

  Eigen::MatrixXd a = input;
  DistanceBoundsMatrix::smooth(a);
  BOOST_CHECK_MESSAGE(
    a.isApprox(expected, 1e-10),
    "Triangle smoothing of example matrix does not give triangle inequality limits: Expected\n"
    << expected << "\nGot:\n" << a << "\n"
  );
}

BOOST_AUTO_TEST_CASE(TetrangleSmoothingExplicit) {
  Eigen::Matrix4d input;
  input <<   0.0,   1.0, 100.0,   1.0,
             1.0,   0.0,   1.0, 100.0,
             0.5,   1.0,   0.0,   1.0,
             1.0,   0.5,   1.0,   0.0;

  Eigen::Matrix4d expected;
  expected << 0.0, 1.0, 2.0, 1.0,
              1.0, 0.0, 1.0, 2.0,
              0.5, 1.0, 0.0, 1.0,
              1.0, 0.5, 1.0, 0.0;

  Eigen::Matrix4d a = input;
  const unsigned iterations = tetrangleSmooth(a);

  BOOST_CHECK_MESSAGE(
    (a.array() <= expected.array()).all(),
    "Tetrangle smoothing (" << iterations << " iterations) of example matrix "
    << "does not give values smaller than the inequality limits: Expected at most\n"
    << expected << "\nGot:\n" << a << "\n"
  );
}

BOOST_AUTO_TEST_CASE(TriangleSmoothingDetectsViolations) {
  Eigen::Matrix3d impossibleBounds;
  impossibleBounds << 0.0, 1.0, 4.0,
                      1.0, 0.0, 2.0,
                      4.0, 2.0, 0.0;

  BOOST_CHECK_THROW(DistanceBoundsMatrix::smooth(impossibleBounds), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TetrangleSmoothingDetectsViolations) {
  Eigen::Matrix3d impossibleBounds;
  impossibleBounds << 0.0, 1.0, 4.0,
                      1.0, 0.0, 2.0,
                      4.0, 2.0, 0.0;

  BOOST_CHECK_THROW(tetrangleSmooth(impossibleBounds), std::runtime_error);
}