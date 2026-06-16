#include "whas/physics/geometry_utils.h"
#include <cmath>
#include <algorithm>

namespace GeometryUtils {

namespace {
float Distance(Point p1, Point p2) {
    float dx = p1.x - p2.x;
    float dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
}

float DistanceToSegment(Point p, Point s1, Point s2) {
    float l2 = (s1.x - s2.x) * (s1.x - s2.x) + (s1.y - s2.y) * (s1.y - s2.y);
    if (l2 == 0.0) return Distance(p, s1);
    float t = ((p.x - s1.x) * (s2.x - s1.x) + (p.y - s1.y) * (s2.y - s1.y)) / l2;
    t = std::max(0.0f, std::min(1.0f, t));
    return Distance(p, {s1.x + t * (s2.x - s1.x), s1.y + t * (s2.y - s1.y)});
}

bool IsConvex(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) >= 0;
}

bool IsPointInTriangle(Point p, Point a, Point b, Point c) {
    auto cross = [](Point a, Point b, Point c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    float cp1 = cross(a, b, p);
    float cp2 = cross(b, c, p);
    float cp3 = cross(c, a, p);
    return (cp1 >= 0 && cp2 >= 0 && cp3 >= 0) || (cp1 <= 0 && cp2 <= 0 && cp3 <= 0);
}

struct Edge {
    int v1, v2;
    bool operator<(const Edge& other) const {
        if (v1 != other.v1) return v1 < other.v1;
        return v2 < other.v2;
    }
};

} // namespace

std::vector<std::vector<Point>> MarchingSquares(const std::vector<bool>& mask, int width, int height) {
    // Standard case-based Marching Squares
    // Vertices are at (x, y) corners of pixels. Grid is (W+1) x (H+1).
    // Pixels are at (x+0.5, y+0.5).
    
    auto get = [&](int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        return mask[y * width + x];
    };

    struct Segment { Point p1, p2; };
    std::vector<Segment> segments;

    for (int y = -1; y < height; ++y) {
        for (int x = -1; x < width; ++x) {
            // Check the 4 pixels around the vertex (x+1, y+1)
            // But standard Marching Squares uses pixels as vertices.
            // Let's use the pixel centers as vertices for the isosurface.
            // Or better: pixels are the field, vertices are at corners.
            int caseIdx = 0;
            if (get(x, y))     caseIdx |= 1;
            if (get(x+1, y))   caseIdx |= 2;
            if (get(x+1, y+1)) caseIdx |= 4;
            if (get(x, y+1))   caseIdx |= 8;

            float fx = (float)x + 0.5f;
            float fy = (float)y + 0.5f;

            Point top = {fx + 0.5f, fy};
            Point right = {fx + 1.0f, fy + 0.5f};
            Point bottom = {fx + 0.5f, fy + 1.0f};
            Point left = {fx, fy + 0.5f};

            switch (caseIdx) {
                case 1:  segments.push_back({left, top}); break;
                case 2:  segments.push_back({top, right}); break;
                case 3:  segments.push_back({left, right}); break;
                case 4:  segments.push_back({right, bottom}); break;
                case 5:  segments.push_back({left, top}); segments.push_back({right, bottom}); break;
                case 6:  segments.push_back({top, bottom}); break;
                case 7:  segments.push_back({left, bottom}); break;
                case 8:  segments.push_back({bottom, left}); break;
                case 9:  segments.push_back({bottom, top}); break;
                case 10: segments.push_back({bottom, left}); segments.push_back({top, right}); break;
                case 11: segments.push_back({bottom, right}); break;
                case 12: segments.push_back({right, left}); break;
                case 13: segments.push_back({right, top}); break;
                case 14: segments.push_back({top, left}); break;
                default: break;
            }
        }
    }

    // Stitch segments into loops
    std::vector<std::vector<Point>> loops;
    while (!segments.empty()) {
        std::vector<Point> loop;
        loop.push_back(segments.back().p1);
        Point current = segments.back().p2;
        segments.pop_back();

        bool closed = false;
        while (!closed) {
            bool found = false;
            for (size_t i = 0; i < segments.size(); ++i) {
                if (std::abs(segments[i].p1.x - current.x) < 0.01f && std::abs(segments[i].p1.y - current.y) < 0.01f) {
                    current = segments[i].p2;
                    loop.push_back(current);
                    segments.erase(segments.begin() + i);
                    found = true;
                    break;
                } else if (std::abs(segments[i].p2.x - current.x) < 0.01f && std::abs(segments[i].p2.y - current.y) < 0.01f) {
                    current = segments[i].p1;
                    loop.push_back(current);
                    segments.erase(segments.begin() + i);
                    found = true;
                    break;
                }
            }
            if (!found || (std::abs(loop[0].x - current.x) < 0.01f && std::abs(loop[0].y - current.y) < 0.01f)) {
                closed = true;
            }
        }
        if (loop.size() > 2) loops.push_back(loop);
    }

    return loops;
}

std::vector<Point> DouglasPeucker(const std::vector<Point>& points, float epsilon) {
    if (points.size() < 3) return points;

    int maxIdx = -1;
    float maxDist = 0;

    for (int i = 1; i < (int)points.size() - 1; ++i) {
        float d = DistanceToSegment(points[i], points[0], points.back());
        if (d > maxDist) {
            maxDist = d;
            maxIdx = i;
        }
    }

    if (maxDist > epsilon) {
        std::vector<Point> left(points.begin(), points.begin() + maxIdx + 1);
        std::vector<Point> right(points.begin() + maxIdx, points.end());
        auto resLeft = DouglasPeucker(left, epsilon);
        auto resRight = DouglasPeucker(right, epsilon);
        resLeft.pop_back();
        resLeft.insert(resLeft.end(), resRight.begin(), resRight.end());
        return resLeft;
    } else {
        return {points[0], points.back()};
    }
}

std::vector<std::vector<Point>> Triangulate(const std::vector<Point>& polygon) {
    std::vector<std::vector<Point>> triangles;
    if (polygon.size() < 3) return triangles;

    std::vector<Point> vertices = polygon;
    // Remove duplicate last point if it exists
    if (Distance(vertices.front(), vertices.back()) < 0.01f) vertices.pop_back();
    if (vertices.size() < 3) return triangles;

    // Ensure clockwise winding
    float area = 0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        size_t j = (i + 1) % vertices.size();
        area += vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
    }
    if (area < 0) std::reverse(vertices.begin(), vertices.end());

    int timeout = 1000;
    while (vertices.size() > 3 && timeout-- > 0) {
        bool earFound = false;
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t prev = (i + vertices.size() - 1) % vertices.size();
            size_t next = (i + 1) % vertices.size();

            Point a = vertices[prev], b = vertices[i], c = vertices[next];

            if (IsConvex(a, b, c)) {
                bool containsPoint = false;
                for (size_t j = 0; j < vertices.size(); ++j) {
                    if (j == prev || j == i || j == next) continue;
                    if (IsPointInTriangle(vertices[j], a, b, c)) {
                        containsPoint = true;
                        break;
                    }
                }

                if (!containsPoint) {
                    triangles.push_back({a, b, c});
                    vertices.erase(vertices.begin() + i);
                    earFound = true;
                    break;
                }
            }
        }
        if (!earFound) break; 
    }
    if (vertices.size() == 3) triangles.push_back({vertices[0], vertices[1], vertices[2]});

    return triangles;
}

} // namespace GeometryUtils
