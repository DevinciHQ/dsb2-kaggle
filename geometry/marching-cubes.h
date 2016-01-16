#ifndef GEOMETRY_MARCHING_CUBES_H_
#define GEOMETRY_MARCHING_CUBES_H_ 1

#include <cstddef>
#include <cstdint>
#include <vector>

#include "geometry/vector.h"

void Triangulate(const float* field, const size_t dim_x, const size_t dim_y,
                 const size_t dim_z, std::vector<XYZ>& vertices,
                 std::vector<XYZ>& normals,
                 std::vector<uint32_t>& indices);

#endif  // !GEOMETRY_MARCHING_CUBES_H_
