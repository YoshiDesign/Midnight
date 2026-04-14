#pragma once
#include <span>
#include <memory_resource>
#include "Module/Procgen/Types2.h"
#include "Core/Math/Math.h"
#include "Core/Math/Vector.h"

namespace procgen {

	struct AllPoints;
	struct Triangulation;
	struct VoronoiCell;

	/* Triangulation Essentials */

	// Primary function of the Triangulation phase
	Triangulation* TriangulateBowyerWatson(
		std::span<const aveng::Vec2> points,
		std::pmr::memory_resource* scratchMr,
		std::pmr::memory_resource* finalMr
	);

	// Primary function of the Triangulation phase
	void BuildHalfEdgeMesh(
		std::span<const aveng::Vec2> vertexPos,
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
		const aveng::Vec2& p,
		aveng::BaryWeights& outW,
		float eps = 1e-12f
	);

	// Sample scalar field defined per site (same indexing as AllPoints::pts).
	bool SampleScalar(
		const AllPoints& pts,
		const Triangulation& tri,
		TriIndex triID,
		const aveng::Vec2& p,
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
		aveng::Vec3& outN
	);

	inline aveng::Vec3 TriangleNormalFromGradient(float dhdx, float dhdz) {
		// n = normalize(-dhdx, 1, -dhdz)
		return aveng::Vec3{ -dhdx, 1.f, -dhdz }.normalized();
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

}