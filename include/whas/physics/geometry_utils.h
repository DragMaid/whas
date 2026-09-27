#pragma once
#include <vector>
#include <raylib.h>
#include <box2d/box2d.h>

namespace GeometryUtils {

struct Point {
    float x, y;
};

// Simplified Marching Squares for pixel grids.
// Returns a list of polygons (each a vector of points).
std::vector<std::vector<Point>> MarchingSquares(const std::vector<bool>& mask, int width, int height);

// Douglas-Peucker algorithm to simplify a polyline.
std::vector<Point> DouglasPeucker(const std::vector<Point>& points, float epsilon);

// Triangulates a simple polygon using ear clipping.
// Returns a list of triangles (each 3 points).
std::vector<std::vector<Point>> Triangulate(const std::vector<Point>& polygon);

} // namespace GeometryUtils
