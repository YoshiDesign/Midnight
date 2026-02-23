#pragma once
#include <vector>
#include <span>
#include <memory_resource>
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types.h"

namespace aveng {

	void BuildHalfEdgeMesh(
		std::span<const Vec2> vertexPos,
		Triangulation& out,
		SiteIndex vertexCount,
		std::pmr::memory_resource* scratchMr // for temporary edge map
	);

	Triangulation* TriangulateBowyerWatson(
		std::span<const Vec2> points,
		std::pmr::memory_resource* scratchMr,
		std::pmr::memory_resource* finalMr
	);

	// SoA centric struct!
	struct DelaunayMeshView {

		std::span<const Vec2>  vertexPos;
		std::span<const Triangle> tris;
		std::span<const HalfEdge> halfEdges;
		std::span<const TriangleCache> cache;

		// std::pmr::vector<Triangle> Triangulate(std::span<const Vec2> points);

		// Barycentric returns barycentric weights (wa, wb, wc) for point p in triangle triID.
		// Triangle vertices are the *sites* (A,B,C). The weights satisfy:
		//
		//   p = wa*A + wb*B + wc*C
		//   wa + wb + wc = 1
		//
		// Early exit if the triangle is degenerate (area ~ 0).
		// If you want "inside triangle" test: wa>=0 && wb>=0 && wc>=0 (with epsilon).
		bool Barycentric(uint32_t ti, const Vec2& p, float& wa, float& wb, float& wc) const;

		// SampleScalar linearly interpolates a scalar field defined per site (vertex)
		// across a triangle using barycentric weights.
		//
		// valuesAtSites must have length >= len(m.Sites).
		// Early exit if degenerate tri or invalid indexing.
		float SampleScalar(int triID,Vec2 p, std::span<const float> valuesAtSites) const;

		// TriangleGradient returns the constant (positive) gradient of a linearly interpolated scalar field
		// over the triangle (triID), assuming the scalar values are defined at the triangle's sites.
		//
		// For terrain height h(x,z), interpret:
		//   dhdx = ∂h/∂x
		//   dhdy = ∂h/∂y  (in your terrain: this is ∂h/∂z)
		//
		// This is extremely useful for slope computation because the gradient is constant per triangle.
		//
		// Returns (dhdx, dhdy), early exit if degenerate triangle or bad inputs.
		Vec2 TriangleGradient(int triID, std::span<const float> valuesAtSites) const; // returns Vec2{dhdx, dhdy}

		// TriangleNormal computes the face normal for a triangle given heights at each site.
		// The normal is computed using the cross product of two edges in 3D space.
		// Returns normal, or early exit if the triangle is degenerate.
		//
		// For terrain: X = east, Y = up (height), Z = north.
		// CCW winding produces an upward-facing normal.
		Vec3 TriangleNormal(int triID, std::span<const float> heights);

		// TriangleNormalFromGradient computes the face normal from the height gradient.
		// This is an alternative method using the pre-computed gradient (dhdx, dhdz).
		//
		// The gradient represents the slope in X and Z directions. The normal is:
		//   n = normalize(-dhdx, 1, -dhdz)
		//
		// This method is efficient when you already have the gradient.
		Vec3 TriangleNormalFromGradient(float dhdx, float dhdz);

		// AllFaceNormals computes face normals for all triangles in the mesh.
		// Returns a vector, parallel to m.tris, containing the normalized face normal for each triangle.
		std::vector<Vec3> AllFaceNormals(std::span<const float> heights);

		// SlopeAngle returns the slope angle in radians for a triangle.
		// 0 = flat, π/2 = vertical cliff.
		float SlopeAngle(int triID, std::span<const float> heights);

		// SlopePercent returns the slope as a percentage (rise/run * 100).
		// A 45° slope returns 100%.
		float SlopePercent(int triID, std::span<const float> heights);

		// Later ToDo - Voronoi usage
		// VoronoiCellForSite(site SiteIndex) VoronoiCell

		// Later TODO : This is for the Voronoi dual
		// int findAnyOutgoing(SiteIndex site);
		// This was meant for early prototyping - Also only used with the Voronoi dual
		// angleSortAround(center Vec2, tris[]int, verts[]Vec2)

	};

}