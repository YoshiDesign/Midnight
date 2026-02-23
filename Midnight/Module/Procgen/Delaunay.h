#pragma once
#include <span>
#include <memory_resource>
#include "Core/Math/Vector.h"
#include "Core/Math/Math.h"
#include "Module/Procgen/Types.h"

/*
 * TODO: 
 *  - fwd declarations and less header noise.
 *  - We ended up going the route of independent functions instead composing a DelaunayMeshView.
 *    Unsure of what the prognosis looks like. I think this is only a maintainability concern.
 * 
 * [IMPORTANT]
 * These functions should be completely unaware of the SpatialGrid.
 * The spatial grid is used to access up the triangles that these functions operate on.
 */

namespace aveng {

	struct AllPoints;
	struct Triangulation;
	struct VoronoiCell;

	/* Triangulation Essentials */
	
	// Primary function of the Triangulation phase
	Triangulation* TriangulateBowyerWatson(
		std::span<const Vec2> points,
		std::pmr::memory_resource* scratchMr,
		std::pmr::memory_resource* finalMr
	);

	// Primary function of the Triangulation phase
	void BuildHalfEdgeMesh(
		std::span<const Vec2> vertexPos,
		Triangulation& out,
		SiteIndex vertexCount,
		std::pmr::memory_resource* scratchMr // for temporary edge map
	);

	/* Mathematical Helpers - Heavily used in simulation */

	// Barycentric weights for p in triangle triID. ok=false if degenerate or invalid.
	bool Barycentric(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const Vec2& p,
		BaryWeights& outW,
		float eps = 1e-12f
	);

	// Sample scalar field defined per site (same indexing as AllPoints::pts).
	bool SampleScalar(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const Vec2& p,
		const float* valuesAtSites, size_t valuesCount,
		float& outValue
	);

	// Constant gradient (dhdx, dhdz) over triangle for scalar values at sites.
	bool TriangleGradient(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const float* valuesAtSites, size_t valuesCount,
		float& outDhdx,
		float& outDhdz
	);

	// Triangle normal from heights at each site (terrain: (x, height, z)).
	bool TriangleNormal(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const float* heights, size_t heightsCount,
		Vec3& outN
	);

	inline Vec3 TriangleNormalFromGradient(float dhdx, float dhdz) {
		// n = normalize(-dhdx, 1, -dhdz)
		return Vec3{ -dhdx, 1.f, -dhdz }.normalized();
	}

	bool SlopeAngleRadians(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const float* heights, size_t heightsCount,
		float& outAngleRad
	);

	bool SlopePercent(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const float* heights, size_t heightsCount,
		float& outPercent
	);

	// Voronoi cell circumcenter polygon around a site (walk the 1-ring using half-edges).
	VoronoiCell VoronoiCellForSite(
		const AllPoints& pts,
		const Triangulation& tri,
		SiteIndex site,
		std::pmr::memory_resource* mr,
		bool doAngleSort = true
	);

	// SoA centric struct!
	/*struct DelaunayMeshView {

		std::span<const Vec2>  vertexPos;
		std::span<const Triangle> tris;
		std::span<const HalfEdge> halfEdges;
		std::span<const TriangleCache> cache;*/


		// std::pmr::vector<Triangle> Triangulate(std::span<const Vec2> points);

		// Barycentric returns barycentric weights (wa, wb, wc) for point p in triangle triID.
		// Triangle vertices are the *sites* (A,B,C). The weights satisfy:
		//
		//   p = wa*A + wb*B + wc*C
		//   wa + wb + wc = 1
		//
		// Early exit if the triangle is degenerate (area ~ 0).
		// If you want "inside triangle" test: wa>=0 && wb>=0 && wc>=0 (with epsilon).
		/// bool Barycentric(uint32_t ti, const Vec2& p, float& wa, float& wb, float& wc) const;

		// SampleScalar linearly interpolates a scalar field defined per site (vertex)
		// across a triangle using barycentric weights.
		//
		// valuesAtSites must have length >= len(m.Sites).
		// Early exit if degenerate tri or invalid indexing.
		/// float SampleScalar(int triID,Vec2 p, std::span<const float> valuesAtSites) const;

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
		/// Vec2 TriangleGradient(int triID, std::span<const float> valuesAtSites) const; // returns Vec2{dhdx, dhdy}

		// TriangleNormal computes the face normal for a triangle given heights at each site.
		// The normal is computed using the cross product of two edges in 3D space.
		// Returns normal, or early exit if the triangle is degenerate.
		//
		// For terrain: X = east, Y = up (height), Z = north.
		// CCW winding produces an upward-facing normal.
		/// Vec3 TriangleNormal(int triID, std::span<const float> heights);

		// TriangleNormalFromGradient computes the face normal from the height gradient.
		// This is an alternative method using the pre-computed gradient (dhdx, dhdz).
		//
		// The gradient represents the slope in X and Z directions. The normal is:
		//   n = normalize(-dhdx, 1, -dhdz)
		//
		// This method is efficient when you already have the gradient.
		/// Vec3 TriangleNormalFromGradient(float dhdx, float dhdz);

		// AllFaceNormals computes face normals for all triangles in the mesh.
		// Returns a vector, parallel to m.tris, containing the normalized face normal for each triangle.
		/// std::vector<Vec3> AllFaceNormals(std::span<const float> heights);

		// SlopeAngle returns the slope angle in radians for a triangle.
		// 0 = flat, π/2 = vertical cliff.
		/// float SlopeAngle(int triID, std::span<const float> heights);

		// SlopePercent returns the slope as a percentage (rise/run * 100).
		// A 45° slope returns 100%.
		/// float SlopePercent(int triID, std::span<const float> heights);

		// Later ToDo - Voronoi usage
		// VoronoiCellForSite(site SiteIndex) VoronoiCell

		// Later TODO : This is for the Voronoi dual
		// int findAnyOutgoing(SiteIndex site);
		// This was meant for early prototyping - Also only used with the Voronoi dual
		// angleSortAround(center Vec2, tris[]int, verts[]Vec2)

	// };

}