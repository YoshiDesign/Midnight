#pragma once
#include <vector>
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types.h"

namespace aveng {

	class DelaunayMesh {
	public:

		bool Barycentric(uint32_t ti, const Vec2& p, float& wa, float& wb, float& wc) const;

		std::vector<Site> Sites;
		std::vector<Triangle> Tris;
		std::vector<HalfEdge> HalfEdges;
		std::vector<Vec3> FaceNormals;

	private:

	};

}