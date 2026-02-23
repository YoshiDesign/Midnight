#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <format>
#include <span>
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"

/*
 *	While you're here, weary traveler, please consider the following advice from a robot:
 *
 *    Headers should not:
 *        Implicitly generate heavy destructors
 *        Force complete-type requirements in widely included files
 *        Leak large template instantiations everywhere
 *
 *    Out-of-line special members:
 *        Localize template instantiation
 *        Reduce compile times
 *        Prevent incomplete-type traps
 *        Improve build stability
 *
 *    This is professional C++ hygiene.
 * 
 * * * When you see:
 *      - forward-declared type
 *      - unique_ptr<T> inside a 
 *      - struct defined in a header
 *
 *     Immediately ask
 *      - Where is the destructor instantiated"
 * 
 *     If the answer is "implicitly everywhere" -> move it out-of-line.
 */

namespace aveng {

	namespace fs = std::filesystem;

	class Debug {
	public:
		
        static void writeBlueNoiseDataToFile(const fs::path& path,
            const std::pmr::vector<Vec2>& data)
        {
            // Ensure parent directory exists
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }

            std::ofstream file(path);

            if (!file)
            {
                throw std::runtime_error(
                    std::format("Failed to open debug file: {}", path.string()));
            }

            file << "--- Blue Noise Points ---\n";
            file << "Count: " << data.size() << "\n\n";

            // Column header
            file << "Index,X,Z\n";

            for (size_t i = 0; i < data.size(); ++i)
            {
                file << i << ","
                    << data[i].x << ","
                    << data[i].y << "\n";
            }
        }

        static void writeHeightDataToFile(const fs::path& path,
            const std::span<float> heights)
        {
            // Ensure parent directory exists
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }
            std::ofstream file(path);
            if (!file)
            {
                throw std::runtime_error(
                    std::format("Failed to open debug file: {}", path.string()));
            }
            file << "--- Height Data ---\n";
            file << "Count: " << heights.size() << "\n\n";
            // Column header
            file << "Index,Height\n";
            for (size_t i = 0; i < heights.size(); ++i)
            {
                file << i << ","
                    << heights[i] << "\n";
            }
		}

        static void writeTriangulationDataToFile(const fs::path& path, Triangulation* tri)
        {
            // Members:
            // tri->tris;
            // tri->halfEdges;
            // tri->cache; -- meh, if the rest is correct this is implicitly correct.
            // tri->circumcenters;
            // tri->triEdge0;
            // tri->siteEdge;

            // Ensure parent directory exists
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }
            std::ofstream file(path);
            if (!file)
            {
                throw std::runtime_error(
                    std::format("Failed to open debug file: {}", path.string()));
            }
            file << "--- Triangulation Data ---\n";
            file << "Triangle Count: " << tri->tris.size() << "\n";
			file << "halfEdges Count: " << tri->halfEdges.size() << "\n";     // size should == tri->tris.size() * 3
			file << "circumcenters Count: " << tri->circumcenters.size() << "\n"; // size should == tri->tris.size()
			file << "triEdge0 Count: " << tri->triEdge0.size() << "\n"; // size should == tri->tris.size()
			file << "siteEdge Count: " << tri->siteEdge.size() << "\n\n"; // size should == vertexCount (allPoints count)
            // Column header
            file << "Triangle_Index,A,B,C,halfEdge,circumcenter,triEdge0Index,siteEdgeIndex\n";

            const size_t T = tri->tris.size();

            for (size_t i = 0; i < T; ++i)
            {
                const auto& t = tri->tris[i];

                // Triangle vertices
                const auto A = t.A;
                const auto B = t.B;
                const auto C = t.C;

                // Half-edge: assuming 3 half-edges per triangle (3*i + {0,1,2})
                const size_t he0 = 3 * i + 0;
                const size_t he1 = 3 * i + 1;
                const size_t he2 = 3 * i + 2;

                // Circumcenter (1 per triangle)
                const auto& cc = tri->circumcenters[i];

                // triEdge0: one entry per triangle
                const auto triEdge0Index = tri->triEdge0[i];

                // siteEdge: one entry per vertex (may be -1 or invalid if boundary)
                // We can’t meaningfully emit a single siteEdge per triangle,
                // so just dump the three vertex entries.
                const auto siteEdgeA = tri->siteEdge[A];
                const auto siteEdgeB = tri->siteEdge[B];
                const auto siteEdgeC = tri->siteEdge[C];

                file
                    << i << ","
                    << A << "," << B << "," << C << ","
                    << he0 << "|" << he1 << "|" << he2 << ","
                    << cc.x << ":" << cc.y << ","
                    << triEdge0Index << ","
                    << siteEdgeA << "|" << siteEdgeB << "|" << siteEdgeC
                    << "\n";
            }

            // Paste this directly into your writeTriangulationDataToFile() after the CSV dump (or before).
// Assumes:
// - tri->halfEdges is a flat array of size 3*T
// - Each half-edge stores an int `twin` (=-1 if boundary) OR `opposite` etc. (rename below)
// - "next in triangle" is e^1/e^2 mapping via (e%3) (standard for flat 3-per-tri storage)
//
// If your half-edge struct member isn't named `twin`, change HE_TWIN(e) below.

            using HEIndex = int;

            auto HE_NEXT = [](HEIndex e) -> HEIndex {
                const HEIndex b = e - (e % 3);
                const HEIndex o = e % 3;
                return b + ((o + 1) % 3);
            };
            auto HE_PREV = [](HEIndex e) -> HEIndex {
                const HEIndex b = e - (e % 3);
                const HEIndex o = e % 3;
                return b + ((o + 2) % 3);
            };
            auto TRI_OF = [](HEIndex e) -> size_t { return static_cast<size_t>(e / 3); };

            // ---- CHANGE THIS if your member name differs (twin / opp / opposite / pair / etc.)
            auto HE_TWIN = [&](HEIndex e) -> HEIndex {
                // Example if your struct is: struct HalfEdge { int twin; int site; ... };
                return static_cast<HEIndex>(tri->halfEdges[static_cast<size_t>(e)].twin);
            };

            // If your Triangle type differs (a,b,c), update this accessor:
            auto TRI_V = [&](size_t triIndex, int corner) -> int {
                const auto& t = tri->tris[triIndex];
                if (corner == 0) return static_cast<int>(t.A);
                if (corner == 1) return static_cast<int>(t.B);
                return static_cast<int>(t.C);
            };

            // Directed edge endpoints for half-edge index e:
            // local corner i = e%3 corresponds to:
            //   e == 3*t + 0 : a->b
            //   e == 3*t + 1 : b->c
            //   e == 3*t + 2 : c->a
            auto HE_FROM = [&](HEIndex e) -> int {
                const size_t ti = TRI_OF(e);
                const int o = e % 3;
                return TRI_V(ti, o);                 // 0->a, 1->b, 2->c
            };
            auto HE_TO = [&](HEIndex e) -> int {
                const size_t ti = TRI_OF(e);
                const int o = e % 3;
                return TRI_V(ti, (o + 1) % 3);       // next corner
            };

            // The canonical "rotate around the origin vertex (FROM vertex)" step:
            // rotateFrom(e) = twin(prev(e))  (common and robust)
            // - Why: prev(e) ends at FROM(e), then twin flips across to adjacent tri, still ends at that vertex.
            auto ROTATE_AROUND_FROM = [&](HEIndex e) -> HEIndex {
                const HEIndex p = HE_PREV(e);
                const HEIndex tw = HE_TWIN(p);
                return tw; // may be -1 at boundary
            };

            // Alternate rotate around the destination vertex (TO vertex):
            // rotateTo(e) = twin(next(e))
            auto ROTATE_AROUND_TO = [&](HEIndex e) -> HEIndex {
                const HEIndex n = HE_NEXT(e);
                const HEIndex tw = HE_TWIN(n);
                return tw; // may be -1
            };

            auto dumpEdge = [&](std::ostream& os, const char* label, HEIndex e) {
                if (e < 0) { os << label << " = -1 (boundary)\n"; return; }
                const size_t ti = TRI_OF(e);
                os << label << " e=" << e
                    << " tri=" << ti
                    << " (" << HE_FROM(e) << "->" << HE_TO(e) << ")"
                    << " twin=" << HE_TWIN(e)
                    << " next=" << HE_NEXT(e)
                    << " prev=" << HE_PREV(e)
                    << "\n";
            };

            auto checkTwinInvolution = [&](HEIndex e, std::ostream& os) -> bool {
                const HEIndex t = HE_TWIN(e);
                if (t < 0) return true; // boundary ok
                const HEIndex tt = HE_TWIN(t);
                if (tt != e) {
                    os << "[FAIL] twin(twin(e)) != e for e=" << e
                        << " twin=" << t << " twin(twin)=" << tt << "\n";
                    return false;
                }
                // Endpoints must reverse
                if (!(HE_FROM(e) == HE_TO(t) && HE_TO(e) == HE_FROM(t))) {
                    os << "[FAIL] twin endpoints don't reverse for e=" << e
                        << " (" << HE_FROM(e) << "->" << HE_TO(e) << ") vs twin "
                        << t << " (" << HE_FROM(t) << "->" << HE_TO(t) << ")\n";
                    return false;
                }
                return true;
            };

            auto rotateAroundVertexValidate = [&](int v, std::ostream& os) {
                os << "\n--- Rotate Around Vertex v=" << v << " ---\n";
                if (v < 0 || static_cast<size_t>(v) >= tri->siteEdge.size()) {
                    os << "[WARN] v out of range for siteEdge\n";
                    return;
                }

                HEIndex start = static_cast<HEIndex>(tri->siteEdge[static_cast<size_t>(v)]);
                if (start < 0) {
                    os << "[INFO] siteEdge[v] is -1 (isolated or not set)\n";
                    return;
                }

                // We want an edge that has FROM == v for rotateFrom traversal; if siteEdge points to a TO edge,
                // pivot by going to prev/next until we find one.
                // Try up to 3 local rotations within the same triangle.
                HEIndex e = start;
                bool foundFrom = false;
                for (int k = 0; k < 3; ++k) {
                    if (HE_FROM(e) == v) { foundFrom = true; break; }
                    e = HE_NEXT(e);
                }
                if (!foundFrom) {
                    os << "[WARN] Could not find outgoing half-edge FROM v within start triangle. "
                        "siteEdge might encode a different convention.\n";
                    dumpEdge(os, "start", start);
                    return;
                }

                dumpEdge(os, "seed(outgoing)", e);

                // Traverse fan around v using twin(prev(e)) until we return or hit boundary.
                // A well-formed interior vertex should loop back to the start.
                const int maxSteps = 128;
                HEIndex cur = e;
                int steps = 0;

                while (steps++ < maxSteps) {
                    // Invariants along the way
                    (void)checkTwinInvolution(cur, os);

                    const HEIndex nxt = ROTATE_AROUND_FROM(cur);
                    if (nxt < 0) {
                        os << "[INFO] Hit boundary after " << steps - 1
                            << " rotations. (v is likely on hull / clipped domain.)\n";
                        break;
                    }

                    // nxt should also be outgoing from v (FROM==v) if the convention matches
                    if (HE_FROM(nxt) != v) {
                        os << "[FAIL] rotate produced edge not outgoing from v. "
                            << "Expected FROM=" << v << " got FROM=" << HE_FROM(nxt) << "\n";
                        dumpEdge(os, "cur", cur);
                        dumpEdge(os, "nxt", nxt);
                        break;
                    }

                    dumpEdge(os, "rot", nxt);

                    if (nxt == cur) {
                        os << "[FAIL] rotate got stuck (nxt == cur) at step " << steps << "\n";
                        break;
                    }
                    if (nxt == e) {
                        os << "[OK] Closed loop back to seed after " << steps - 1 << " rotations.\n";
                        break;
                    }
                    cur = nxt;
                }

                if (steps >= maxSteps) {
                    os << "[WARN] Exceeded maxSteps without closing. Likely a bad twin/next/prev or non-manifold.\n";
                }
            };

            auto randomInt = [&](uint64_t& state, uint32_t bound) -> uint32_t {
                // xorshift64*
                state ^= state >> 12;
                state ^= state << 25;
                state ^= state >> 27;
                const uint64_t r = state * 2685821657736338717ULL;
                return static_cast<uint32_t>(r % bound);
            };

            file << "\n--- HalfEdge Validation ---\n";

            // 1) Random edge checks: twin involution + reversed endpoints
            {
                const int samples = 64;
                uint64_t rng = 0xC0FFEE1234ULL;

                const uint32_t E = static_cast<uint32_t>(tri->halfEdges.size());
                int fails = 0;

                for (int i = 0; i < samples; ++i) {
                    HEIndex e = static_cast<HEIndex>(randomInt(rng, E));
                    if (!checkTwinInvolution(e, file)) {
                        dumpEdge(file, "badEdge", e);
                        ++fails;
                    }
                }

                if (fails == 0) file << "[OK] Random twin checks passed (" << samples << " samples)\n";
                else file << "[WARN] Twin checks had " << fails << " failures out of " << samples << "\n";
            }

            // 2) Random vertex fan traversal checks
            {
                const int samples = 16;
                uint64_t rng = 0xBADC0DE55ULL;

                const uint32_t V = static_cast<uint32_t>(tri->siteEdge.size());
                for (int i = 0; i < samples; ++i) {
                    const int v = static_cast<int>(randomInt(rng, V));
                    rotateAroundVertexValidate(v, file);
                }
            }

            // 3) Demonstrate the sequence you mentioned (twin->next->twin->next->...)
            // This "rotate around TO vertex" can be used if you pick the right starting edge.
            // We'll just print a short walk for a random edge for visual inspection.
            {
                file << "\n--- Demo: twin->next walk (visual inspection) ---\n";
                uint64_t rng = 0x123456789ULL;
                const uint32_t E = static_cast<uint32_t>(tri->halfEdges.size());
                HEIndex e = static_cast<HEIndex>(randomInt(rng, E));
                dumpEdge(file, "seed", e);

                for (int k = 0; k < 8; ++k) {
                    HEIndex t = HE_TWIN(e);
                    if (t < 0) { file << "step " << k << ": twin hit boundary\n"; break; }
                    HEIndex n = HE_NEXT(t);
                    dumpEdge(file, "twin", t);
                    dumpEdge(file, "next", n);
                    e = n;
                }

                file << "(Note) The robust vertex-rotation used above is twin(prev(e)) for FROM-vertex fan traversal.\n";
            }

        }

        static void writeSgridDataToFile(const fs::path& path, const SpatialGrid* sg)
        {
            // Ensure parent directory exists
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }

            std::ofstream file(path);

            if (!file)
            {
                throw std::runtime_error(
                    std::format("Failed to open debug file: {}", path.string()));
            }

            file << "--- Spatial Grid Validation ---\n";

            auto ok = [&](bool cond, const char* msg)
            {
                file << (cond ? "[OK] " : "[!!] ") << msg << "\n";
                return cond;
            };

            auto num = [&](double v) {
                file << v;
            };

            // ---- Basic existence / wiring ----
            ok(sg != nullptr, "SpatialGrid pointer is non-null");
            if (!sg) return;

            ok(sg->tri != nullptr, "sg->tri is wired (Triangulation*)");
            ok(sg->pts != nullptr, "sg->pts is wired (AllPoints*)");
            ok(sg->hf != nullptr, "sg->hf  is wired (HeightField*)");

            // ---- Header summary ----
            file << "\n--- Summary ---\n";
            file << "Bounds(minX,minZ,maxX,maxZ): ";
            num(sg->minx); file << ","; num(sg->minz); file << ","; num(sg->maxx); file << ","; num(sg->maxz); file << "\n";
            file << "cellSize: "; num(sg->cellSize); file << "\n";
            file << "grid (w,h): " << sg->gridw << "," << sg->gridh << "\n";

            // Spans
            const size_t triCount = sg->tris.size();
            const size_t vertCount = sg->vertexPos.size();
            const size_t heightCount = sg->heights.size();

            file << "tris.size: " << triCount << "\n";
            file << "vertexPos.size: " << vertCount << "\n";
            file << "heights.size: " << heightCount << "\n";
            file << "vertexCount field: " << sg->vertexCount << "\n";

            ok(sg->cellSize > 0.0f, "cellSize > 0");
            ok(sg->maxx > sg->minx && sg->maxz > sg->minz, "bounds are non-degenerate (max > min)");
            ok(sg->gridw > 0 && sg->gridh > 0, "grid dimensions > 0");

            ok(vertCount == heightCount, "vertexPos.size == heights.size");
            ok(sg->vertexCount == heightCount, "sg->vertexCount matches heights.size");
            ok(triCount > 0, "triangle count > 0");

            // ---- Consistency with record-owned vectors (if present) ----
            if (sg->tri) {
                ok(sg->tri->tris.size() == triCount, "triangulation->tris.size matches sg->tris.size");
            }
            if (sg->pts) {
                ok(sg->pts->pts.size() == vertCount, "allPoints->pts.size matches sg->vertexPos.size");
            }
            if (sg->hf) {
                ok(sg->hf->heights.size() == heightCount, "heightField->heights.size matches sg->heights.size");
            }

            // ---- Triangle index validation ----
            file << "\n--- Triangle Index Validation ---\n";
            size_t badTri = 0;
            size_t degTri = 0;

            const auto inRange = [&](uint32_t i) { return static_cast<size_t>(i) < vertCount; };

            // sample-limited reporting (avoid huge output)
            constexpr size_t kMaxReport = 20;
            size_t reported = 0;

            for (size_t ti = 0; ti < triCount; ++ti)
            {
                const Triangle& t = sg->tris[ti];

                const bool ra = inRange(t.A);
                const bool rb = inRange(t.B);
                const bool rc = inRange(t.C);
                if (!(ra && rb && rc))
                {
                    ++badTri;
                    if (reported++ < kMaxReport)
                    {
                        file << "[!!] tri " << ti << " has out-of-range index: ("
                            << t.A << "," << t.B << "," << t.C << ")\n";
                    }
                    continue;
                }

                if (t.A == t.B || t.B == t.C || t.A == t.C)
                {
                    ++degTri;
                    if (reported++ < kMaxReport)
                    {
                        file << "[!!] tri " << ti << " is degenerate (repeated index): ("
                            << t.A << "," << t.B << "," << t.C << ")\n";
                    }
                }
            }

            ok(badTri == 0, "all triangle indices are in-range");
            ok(degTri == 0, "no degenerate triangles (repeated indices)");
            file << "badTri: " << badTri << "\n";
            file << "degTri: " << degTri << "\n";

            // ---- Cell coverage validation ----
            //
            // We don't assume internal storage details (cells array type / layout),
            // but we can still validate that the bounds -> grid mapping is sane by
            // re-deriving cell coords for some vertices and checking they're in-range.
            //
            file << "\n--- Cell Mapping Sanity ---\n";

            const float invCell = (sg->cellSize > 0.0f) ? (1.0f / sg->cellSize) : 0.0f;

            auto clampi = [](int v, int lo, int hi) {
                if (v < lo) return lo;
                if (v > hi) return hi;
                return v;
            };

            auto cellOf = [&](const Vec2& p, int& cx, int& cz)
            {
                // match the build-side convention: int((x - min)/cellSize)
                // then clamp into [0, w-1], [0, h-1]
                const float fx = (p.x - sg->minx) * invCell;
                const float fz = (p.y - sg->minz) * invCell;

                cx = static_cast<int>(fx);
                cz = static_cast<int>(fz);

                cx = clampi(cx, 0, static_cast<int>(sg->gridw) - 1);
                cz = clampi(cz, 0, static_cast<int>(sg->gridh) - 1);
            };

            // sample some vertices deterministically (no RNG dependency)
            auto pickIndex = [&](size_t k) -> size_t {
                if (vertCount == 0) return 0;
                // a cheap LCG-ish stride pattern
                return (k * 2654435761u) % vertCount;
            };

            constexpr size_t kSamples = 64;
            size_t outOfBoundsPts = 0;

            for (size_t s = 0; s < kSamples && s < vertCount; ++s)
            {
                const size_t vi = pickIndex(s);
                const Vec2 p = sg->vertexPos[vi];

                // raw (unclamped) for reporting
                const float fx = (p.x - sg->minx) * invCell;
                const float fz = (p.y - sg->minz) * invCell;
                const int rawX = static_cast<int>(fx);
                const int rawZ = static_cast<int>(fz);

                const bool inGrid =
                    (rawX >= 0 && rawX < static_cast<int>(sg->gridw) &&
                        rawZ >= 0 && rawZ < static_cast<int>(sg->gridh));

                if (!inGrid)
                {
                    ++outOfBoundsPts;
                    if (outOfBoundsPts <= kMaxReport)
                    {
                        file << "[!!] vertex " << vi << " maps outside grid before clamp: raw("
                            << rawX << "," << rawZ << ")  pos("
                            << p.x << "," << p.y << ")\n";
                    }
                }
            }

            // This one is *not necessarily an error* if some points are outside the min/max,
            // but given your builder uses min/max expanded by halo, we expect 0 in practice.
            ok(outOfBoundsPts == 0, "sampled vertices map inside grid without clamping");
            file << "sampled out-of-grid vertices: " << outOfBoundsPts << " / " << (std::min)(kSamples, vertCount) << "\n";

            // ---- Backing data pointer/span validation (must match ChunkRecord-owned products) ----
            file << "\n--- Backing Data & Span Wiring ---\n";

            // 1) Pointers exist (you already check these above, but keep it local/specific)
            ok(sg->tri != nullptr, "sg->tri (Triangulation*) non-null");
            ok(sg->pts != nullptr, "sg->pts (AllPoints*) non-null");
            ok(sg->hf != nullptr, "sg->hf  (HeightField*) non-null");

            // If any are missing, don't dereference further.
            if (!(sg->tri && sg->pts && sg->hf)) {
                file << "[!!] Skipping backing-data wiring checks due to missing pointers.\n";
            }
            else {
                // 2) Span sizes match their source vectors
                ok(sg->tris.size() == sg->tri->tris.size(), "sg->tris.size == tri->tris.size");
                ok(sg->vertexPos.size() == sg->pts->pts.size(), "sg->vertexPos.size == pts->pts.size");
                ok(sg->heights.size() == sg->hf->heights.size(), "sg->heights.size == hf->heights.size");

                // 3) Span data pointers match the source vector data pointers
                // Note: std::span::data() returns nullptr when size==0; so compare pointers
                // only if non-empty to avoid false negatives for empty products.
                if (!sg->tri->tris.empty() && !sg->tris.empty()) {
                    ok(sg->tris.data() == sg->tri->tris.data(), "sg->tris.data matches tri->tris.data");
                }
                else {
                    ok(sg->tri->tris.empty() && sg->tris.empty(), "tri->tris and sg->tris are both empty");
                }

                if (!sg->pts->pts.empty() && !sg->vertexPos.empty()) {
                    ok(sg->vertexPos.data() == sg->pts->pts.data(), "sg->vertexPos.data matches pts->pts.data");
                }
                else {
                    ok(sg->pts->pts.empty() && sg->vertexPos.empty(), "pts->pts and sg->vertexPos are both empty");
                }

                if (!sg->hf->heights.empty() && !sg->heights.empty()) {
                    ok(sg->heights.data() == sg->hf->heights.data(), "sg->heights.data matches hf->heights.data");
                }
                else {
                    ok(sg->hf->heights.empty() && sg->heights.empty(), "hf->heights and sg->heights are both empty");
                }

                // 4) Cross-field consistency expectations
                ok(sg->pts->pts.size() == sg->hf->heights.size(),
                    "pts->pts.size == hf->heights.size (positions align with heights)");

                // Triangulation accelerators typically have these invariants:
                // - triEdge0 size == tris size
                // - siteEdge size == vertex count (all points count)
                // (If you ever choose to not build them, you can relax these to warnings.)
                ok(sg->tri->triEdge0.size() == sg->tri->tris.size(),
                    "tri->triEdge0.size == tri->tris.size");

                ok(sg->tri->siteEdge.size() == sg->pts->pts.size(),
                    "tri->siteEdge.size == pts->pts.size (siteEdge per vertex)");

                // 5) Optional: spot-check that a few triangle vertex indices refer into the *same* vertexPos span.
                // This catches cases where spans were accidentally derived from a different buffer.
                file << "\n--- Spot Checks (triangle -> vertexPos) ---\n";
                if (!sg->tris.empty() && !sg->vertexPos.empty()) {
                    auto inRange = [&](uint32_t i) { return static_cast<size_t>(i) < sg->vertexPos.size(); };

                    constexpr size_t kSpot = 8;
                    size_t bad = 0;
                    for (size_t k = 0; k < kSpot && k < sg->tris.size(); ++k) {
                        const size_t ti = (k * 1315423911u) % sg->tris.size();
                        const Triangle& t = sg->tris[ti];

                        const bool ra = inRange(t.A);
                        const bool rb = inRange(t.B);
                        const bool rc = inRange(t.C);

                        if (!(ra && rb && rc)) {
                            ++bad;
                            file << "[!!] tri " << ti << " has out-of-range indices for vertexPos: ("
                                << t.A << "," << t.B << "," << t.C << ")\n";
                        }
                        else {
                            // Touch the data (forces debugger/ASAN-ish issues to show up)
                            const Vec2 a = sg->vertexPos[static_cast<size_t>(t.A)];
                            const Vec2 b = sg->vertexPos[static_cast<size_t>(t.B)];
                            const Vec2 c = sg->vertexPos[static_cast<size_t>(t.C)];
                            (void)a; (void)b; (void)c;
                        }
                    }
                    ok(bad == 0, "spot-checked triangles reference valid vertexPos indices");
                }
                else {
                    file << "[!!] Skipping spot checks (no tris or no vertexPos).\n";
                }
            }

            file << "\n--- Done ---\n";
        }

	private:
	};
}