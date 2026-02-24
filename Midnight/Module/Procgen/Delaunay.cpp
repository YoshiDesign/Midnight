#include "Delaunay.h"
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"
#include "avpch.h"


/*
* Optimization, should this become stressful:
* - Don't compute circumcenters if you're not using a Voronoi layer.
*/

namespace { // Note - these could be specified `inline` only to hint to the compiler, but optimization will likely do this (?)

    // [IMPORTANT]
    // These functions inherit the same resource policy as their callers by using pmr. Cool design!
    // If we used plain ol' containers, we'd end up using the default allocator 
    // and lose the benefits of arena allocation, breaking our design/architecture.
    // Remember, while we can reasonably enforce it, heap-allocation should be considered leaky design.

    // ---------- Robust-ish predicates (double internally) ----------
    double orient2d(aveng::Vec2 a, aveng::Vec2 b, aveng::Vec2 c) {
        return double(b.x - a.x) * double(c.y - a.y) - double(b.y - a.y) * double(c.x - a.x);
    }

    bool inCircumcircle(aveng::Vec2 a, aveng::Vec2 b, aveng::Vec2 c, aveng::Vec2 p) {
        // Determinant test. Assumes a,b,c are CCW for "inside" meaning.
        const double ax = double(a.x) - double(p.x);
        const double ay = double(a.y) - double(p.y);
        const double bx = double(b.x) - double(p.x);
        const double by = double(b.y) - double(p.y);
        const double cx = double(c.x) - double(p.x);
        const double cy = double(c.y) - double(p.y);

        const double a2 = ax * ax + ay * ay;
        const double b2 = bx * bx + by * by;
        const double c2 = cx * cx + cy * cy;

        const double det =
            ax * (by * c2 - b2 * cy) -
            ay * (bx * c2 - b2 * cx) +
            a2 * (bx * cy - by * cx);

        // For CCW triangle, det > 0 => inside
        return det > 0.0;
    }

    aveng::Triangle ensureCCW(std::span<const aveng::Vec2> pts, aveng::Triangle t) {
        aveng::Vec2 a = pts[t.A], b = pts[t.B], c = pts[t.C];
        if (orient2d(a, b, c) < 0.0) {
            // swap b,c
            return aveng::Triangle{ t.A, t.C, t.B };
        }
        return t;
    }

    bool pointInTriCCW(aveng::Vec2 a, aveng::Vec2 b, aveng::Vec2 c, aveng::Vec2 p) {
        // Assumes (a,b,c) CCW. Allow on-edge.
        const double o0 = orient2d(a, b, p);
        const double o1 = orient2d(b, c, p);
        const double o2 = orient2d(c, a, p);
        return (o0 >= 0.0 && o1 >= 0.0 && o2 >= 0.0);
    }

    // ---------- Walking point location ----------
    aveng::TriIndex walkToPoint(
        std::pmr::vector<aveng::AdjTri>& tris,
        std::span<const aveng::Vec2> allPts,
        aveng::TriIndex startTri,
        aveng::Vec2 p
    ) {
        if (startTri < 0 || startTri >= (aveng::TriIndex)tris.size() || !tris[startTri].alive) {
            // fallback: find any alive tri
            for (aveng::TriIndex i = 0; i < (aveng::TriIndex)tris.size(); ++i) {
                if (tris[i].alive) { startTri = i; break; }
            }
            if (startTri < 0) return aveng::kInvalidTri;
        }

        aveng::TriIndex cur = startTri;

        // A conservative max-steps guard against weird degenerate cases:
        const int maxSteps = 256;

        for (int step = 0; step < maxSteps; ++step) {
            const aveng::AdjTri& t = tris[cur];
            if (!t.alive) break;

            // Ensure CCW for the containment test:
            aveng::Triangle tmp{ t.a, t.b, t.c };
            tmp = ensureCCW(allPts, tmp);
            aveng::Vec2 a = allPts[tmp.A], b = allPts[tmp.B], c = allPts[tmp.C];

            if (pointInTriCCW(a, b, c, p)) {
                return cur;
            }

            // If not inside, walk across a violated edge.
            // We test in CCW space: if point is "right" of an edge, go to neighbor across it.
            // Edges of CCW tri are (a->b), (b->c), (c->a).
            // We map those back to the adjacency neighbor slots:
            // slot2 is edge a-b, slot0 is edge b-c, slot1 is edge c-a.
            double oAB = orient2d(a, b, p);
            double oBC = orient2d(b, c, p);
            double oCA = orient2d(c, a, p);

            aveng::TriIndex nextTri = aveng::kInvalidTri;
            if (oAB < 0.0) nextTri = tris[cur].n2;
            else if (oBC < 0.0) nextTri = tris[cur].n0;
            else if (oCA < 0.0) nextTri = tris[cur].n1;

            if (nextTri < 0 || nextTri >= (aveng::TriIndex)tris.size() || !tris[nextTri].alive) {
                // can’t walk further: fall back later
                break;
            }
            cur = nextTri;
        }

        return aveng::kInvalidTri;
    }

    // ---------- Flood-fill bad triangles ----------
    void floodFillBadTris(
        std::pmr::vector<aveng::AdjTri>& tris,
        std::span<const aveng::Vec2> allPts,
        aveng::TriIndex startTri,
        aveng::Vec2 p,
        std::pmr::vector<aveng::TriIndex>& outBad // scratch allocated
    ) {
        outBad.clear();
        if (startTri < 0) return;

        // Using our scratch allocator
        std::pmr::deque<aveng::TriIndex> q(outBad.get_allocator().resource());
        std::pmr::unordered_set<aveng::TriIndex> visited(outBad.get_allocator().resource());

        q.push_back(startTri);
        visited.insert(startTri);

        while (!q.empty()) {
            aveng::TriIndex ti = q.front();
            q.pop_front();

            if (ti < 0 || ti >= (aveng::TriIndex)tris.size()) continue;
            aveng::AdjTri& t = tris[ti];
            if (!t.alive) continue;

            // Ensure CCW before inCircumcircle meaning:
            aveng::Triangle tmp{ t.a, t.b, t.c };
            tmp = ensureCCW(allPts, tmp);

            aveng::Vec2 a = allPts[tmp.A], b = allPts[tmp.B], c = allPts[tmp.C];

            if (!inCircumcircle(a, b, c, p)) {
                continue;
            }

            outBad.push_back(ti);

            aveng::TriIndex neigh[3] = { t.n0, t.n1, t.n2 };
            for (aveng::TriIndex nj : neigh) {
                if (nj < 0) continue;
                if (visited.insert(nj).second) {
                    q.push_back(nj);
                }
            }
        }
    }

    // Undirected edge key: pack(min,max) into u64
    uint64_t makeEdgeKey(aveng::SiteIndex u, aveng::SiteIndex v) {
        aveng::SiteIndex lo = (u < v) ? u : v;
        aveng::SiteIndex hi = (u < v) ? v : u;
        return (uint64_t(hi) << 32) | uint64_t(lo);
    }

    // Directed edge key: pack(origin,dest)
    uint64_t makeDirKey(aveng::SiteIndex o, aveng::SiteIndex d) {
        return (uint64_t(o) << 32) | uint64_t(d);
    }

    // Edge slots:
    // slot 0 => edge b-c (opposite a), neighbor n0
    // slot 1 => edge c-a (opposite b), neighbor n1
    // slot 2 => edge a-b (opposite c), neighbor n2
    aveng::TriEdgeDesc triEdgeKey(const aveng::AdjTri& t, int slot) {
        switch (slot) {
        case 0: return { makeEdgeKey(t.b, t.c), 0 };
        case 1: return { makeEdgeKey(t.c, t.a), 1 };
        default:return { makeEdgeKey(t.a, t.b), 2 };
        }
    }

    aveng::TriIndex& neighborSlot(aveng::AdjTri& t, int slot) {
        if (slot == 0) return t.n0;
        if (slot == 1) return t.n1;
        return t.n2;
    }

    aveng::Vec2 circumcenterOrCentroid(aveng::Vec2 a, aveng::Vec2 b, aveng::Vec2 c) {

        /*
            Lots of named locals going on here, but while I'm studying linear-alg I kinda like this.
            I've included the "less locals" version below for reference.
            The compiler will likely optimize this enough, right?
        */

        // robust-ish circumcenter in double, centroid fallback on degeneracy
        const double ax = a.x, ay = a.y;
        const double bx = b.x, by = b.y;
        const double cx = c.x, cy = c.y;

        const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (std::abs(d) <= 1e-20) {
            return aveng::Vec2{ (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f };
        }

        const double ax2ay2 = ax * ax + ay * ay;
        const double bx2by2 = bx * bx + by * by;
        const double cx2cy2 = cx * cx + cy * cy;

        const double ux = (ax2ay2 * (by - cy) + bx2by2 * (cy - ay) + cx2cy2 * (ay - by)) / d;
        const double uy = (ax2ay2 * (cx - bx) + bx2by2 * (ax - cx) + cx2cy2 * (bx - ax)) / d;

        return aveng::Vec2{ (float)ux, (float)uy };
    }

    /** Less locals
    *   static inline aveng::Vec2 circumcenterOrCentroid(aveng::Vec2 a, aveng::Vec2 b, aveng::Vec2 c)
    *   {
    *       const double ax = a.x, ay = a.y;
    *       const double bx = b.x, by = b.y;
    *       const double cx = c.x, cy = c.y;
    *
    *       const double d = 2.0 * (ax*(by - cy) + bx*(cy - ay) + cx*(ay - by));
    *       if (std::abs(d) <= 1e-20)
    *           return { (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f };
    *
    *       const double ax2 = ax*ax + ay*ay;
    *       const double bx2 = bx*bx + by*by;
    *       const double cx2 = cx*cx + cy*cy;
    *
    *       return {
    *           (float)((ax2*(by - cy) + bx2*(cy - ay) + cx2*(ay - by)) / d),
    *           (float)((ax2*(cx - bx) + bx2*(ax - cx) + cx2*(bx - ax)) / d)
    *       };
    *   }
    */

    /* Everything above this point assisted Triangulation and BuildHalfEdgeMesh */
    /* Everything below this point assists the utilization/mathematical helpers of the produced half-edge mesh */

    bool validTri(const aveng::Triangulation& tri, aveng::TriIndex t) {
        return t >= 0 && static_cast<size_t>(t) < tri.tris.size();
    }
    bool validSite(const aveng::AllPoints& pts, aveng::SiteIndex s) {
        return s >= 0 && static_cast<size_t>(s) < pts.pts.size();
    }

    const aveng::Triangle& getTri(const aveng::Triangulation& tri, aveng::TriIndex t) {
        return tri.tris[static_cast<size_t>(t)];
    }

    // Adjust these field names to match your Triangle struct.
    aveng::SiteIndex triA(const aveng::Triangle& t) { return static_cast<aveng::SiteIndex>(t.A); }
    aveng::SiteIndex triB(const aveng::Triangle& t) { return static_cast<aveng::SiteIndex>(t.B); }
    aveng::SiteIndex triC(const aveng::Triangle& t) { return static_cast<aveng::SiteIndex>(t.C); }

    // Adjust these field names to match your HalfEdge struct.
    aveng::SiteIndex heOrigin(const aveng::HalfEdge& e) { return static_cast<aveng::SiteIndex>(e.origin); }
    aveng::EdgeIndex heTwin(const aveng::HalfEdge& e) { return static_cast<aveng::EdgeIndex>(e.twin); }
    aveng::EdgeIndex heNext(const aveng::HalfEdge& e) { return static_cast<aveng::EdgeIndex>(e.next); }
    aveng::TriIndex  heTri(const aveng::HalfEdge& e) { return static_cast<aveng::TriIndex>(e.tri); }


}

namespace aveng {

    // This is not a general purpose function - `out` must have a valid `tris` member.
    // Builds connectivity + caches from triangles.
    // - tris must be CCW for "inside circumcircle" / Voronoi orientation sanity.
    // - vertexPos is only needed for cache + circumcenters (can be omitted if you don’t want them).
    void BuildHalfEdgeMesh(
        std::span<const Vec2> vertexPos,
        Triangulation& out,
        SiteIndex vertexCount,
        std::pmr::memory_resource* scratchMr // for temporary edge map
    ) {
#ifdef M_DEBUG
        assert(out.tris.size() > 0 && "[BuildHalfEdgeMesh] Missing triangulated input");
#endif
        const size_t T = out.tris.size();

        // Allocate outputs
        out.halfEdges.clear();
        out.halfEdges.resize(T * 3);

        out.triEdge0.clear();
        out.triEdge0.resize(T);

        out.siteEdge.clear();
        out.siteEdge.resize(vertexCount, kInvalidEdge);

        out.cache.clear();
        out.cache.resize(T);

        out.circumcenters.clear();
        out.circumcenters.resize(T);

        // Directed edge map for twin linking
        std::pmr::unordered_map<uint64_t, EdgeIndex> dirEdge(scratchMr);
        dirEdge.reserve(T * 3);

        auto originAt = [&](size_t ti, int corner) -> SiteIndex {
            const Triangle& t = out.tris[ti];
            return (corner == 0) ? t.A : (corner == 1) ? t.B : t.C;
        };

        for (size_t ti = 0; ti < T; ++ti) {
            const EdgeIndex e0 = (EdgeIndex)(3 * ti + 0);
            const EdgeIndex e1 = (EdgeIndex)(3 * ti + 1);
            const EdgeIndex e2 = (EdgeIndex)(3 * ti + 2);

            out.triEdge0[ti] = e0;

            const SiteIndex a = originAt(ti, 0);
            const SiteIndex b = originAt(ti, 1);
            const SiteIndex c = originAt(ti, 2);

            // Build the 3-cycle: a->b, b->c, c->a
            out.halfEdges[e0] = HalfEdge{ a, (TriIndex)ti, e1, kInvalidEdge };
            out.halfEdges[e1] = HalfEdge{ b, (TriIndex)ti, e2, kInvalidEdge };
            out.halfEdges[e2] = HalfEdge{ c, (TriIndex)ti, e0, kInvalidEdge };

            // Remember one outgoing edge per site
            if (a < vertexCount && out.siteEdge[a] == kInvalidEdge) out.siteEdge[a] = e0;
            if (b < vertexCount && out.siteEdge[b] == kInvalidEdge) out.siteEdge[b] = e1;
            if (c < vertexCount && out.siteEdge[c] == kInvalidEdge) out.siteEdge[c] = e2;

            // Register directed edges for twin linking
            dirEdge.emplace(makeDirKey(a, b), e0);
            dirEdge.emplace(makeDirKey(b, c), e1);
            dirEdge.emplace(makeDirKey(c, a), e2);

            // Cache invariants + circumcenter
            if (!vertexPos.empty()) {
                Vec2 A = vertexPos[a];
                Vec2 B = vertexPos[b];
                Vec2 C = vertexPos[c];

                Vec2 ab = B - A;
                Vec2 ac = C - A;
                const double denom = (double)ab.cross(ac); // 
                const float invDen = (std::abs(denom) > 1e-20) ? (float)(1.0 / denom) : 0.0f;

                out.cache[ti] = TriangleCache{ ab, ac, invDen };
                out.circumcenters[ti] = circumcenterOrCentroid(A, B, C); // This can be removed if we're not using Voronoi cells
            }
            else {
                // keep arrays aligned if desired; or skip entirely
                out.cache[ti] = TriangleCache{ Vec2{0,0}, Vec2{0,0}, 0.0f };
                out.circumcenters[ti] = Vec2{ 0,0 };
            }
        }

        // Link twins by looking up the opposite direction.
        for (size_t ti = 0; ti < T; ++ti) {
            const EdgeIndex e0 = (EdgeIndex)(3 * ti + 0);
            const EdgeIndex e1 = (EdgeIndex)(3 * ti + 1);
            const EdgeIndex e2 = (EdgeIndex)(3 * ti + 2);

            auto linkTwin = [&](EdgeIndex e) {
                // dest = next.origin
                const SiteIndex o = out.halfEdges[e].origin;
                const SiteIndex d = out.halfEdges[out.halfEdges[e].next].origin;
                auto it = dirEdge.find(makeDirKey(d, o));
                if (it != dirEdge.end()) {
                    out.halfEdges[e].twin = it->second;
                }
            };

            linkTwin(e0);
            linkTwin(e1);
            linkTwin(e2);
        }
    }

    /* Shameless AI guidance
    * 
    * Because TriangulateBowyerWatson allocates a new `struct Triangulation` every time it runs, 
    * if you ever allow recomputation (i.e invalidate / rebuild triangulation for the same chunk), 
    * you'll be "leaking" inside the chunk's monotonic arena.
    * 
    * That might be totally fine(common in arena designs) if chunk records aren't recomputed often 
    * and the arena is reset when the chunk is evicted / destroyed.
    * 
    * If you do want to support rebuild - without - growth later, you'll eventually want to refactor BW to fill into an existing Triangulation& out rather than allocate.
    * But for your current "compute once per chunk lifetime" strategy : this is exactly right.
    */

    /* Delaunay triangulation implementation using the Bowyer-Watson algorithm */
    Triangulation* TriangulateBowyerWatson(
        std::span<const Vec2> points,
        std::pmr::memory_resource* scratchMr,
        std::pmr::memory_resource* finalMr
    ) {
        // Doubtful this will every be true but let's handle it anyway.
        if (points.size() < 3) {
            auto* out = static_cast<Triangulation*>(finalMr->allocate(sizeof(Triangulation), alignof(Triangulation)));
            return new (out) Triangulation(finalMr);
        }

        // Scratch containers
        std::pmr::vector<Vec2> allPts(scratchMr);
        allPts.reserve(points.size() + 3);
        allPts.insert(allPts.end(), points.begin(), points.end());

        // Bounding box
        float minX = points[0].x, maxX = points[0].x;
        float minY = points[0].y, maxY = points[0].y;
        for (auto& p : points) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }

        // Super triangle
        float dx = maxX - minX;
        float dy = maxY - minY;
        float deltaMax = (dy > dx) ? dy : dx;
        float midX = (minX + maxX) * 0.5f;
        float midY = (minY + maxY) * 0.5f;

        Vec2 superA{ midX - 20.0f * deltaMax, midY - 1.0f * deltaMax };
        Vec2 superB{ midX,                 midY + 20.0f * deltaMax };
        Vec2 superC{ midX + 20.0f * deltaMax, midY - 1.0f * deltaMax };

        const SiteIndex super0 = (SiteIndex)allPts.size(); allPts.push_back(superA);
        const SiteIndex super1 = (SiteIndex)allPts.size(); allPts.push_back(superB);
        const SiteIndex super2 = (SiteIndex)allPts.size(); allPts.push_back(superC);

        const SiteIndex superI[3] = { super0, super1, super2 };

        // Working triangle list (with adjacency)
        std::pmr::vector<AdjTri> tris(scratchMr);
        tris.reserve(points.size() * 2 + 8);
        tris.push_back(AdjTri{ super0, super1, super2, kInvalidTri, kInvalidTri, kInvalidTri, true });

        // Edge-to-triangle adjacency map (undirected edges)
        std::pmr::unordered_map<uint64_t, TriEdgeRef> edgeToTri(scratchMr);
        edgeToTri.reserve(points.size() * 6);

        // Register initial triangle edges
        for (int slot = 0; slot < 3; ++slot) {
            auto e = triEdgeKey(tris[0], slot);
            edgeToTri[e.key] = TriEdgeRef{ 0, e.slot };
        }

        TriIndex lastInsertedTri = 0;

        // scratch helpers
        std::pmr::vector<TriIndex> badTris(scratchMr);
        badTris.reserve(64);

        // TODO - move this
        struct PolyEdge { SiteIndex a, b; TriIndex neighbor; };
        std::pmr::unordered_map<uint64_t, int> edgeCount(scratchMr);
        std::pmr::unordered_map<uint64_t, PolyEdge> edgeInfo(scratchMr);
        std::pmr::vector<PolyEdge> polygon(scratchMr);
        std::pmr::vector<TriIndex> newTriIndices(scratchMr);

        for (SiteIndex pi = 0; pi < (SiteIndex)points.size(); ++pi) {
            Vec2 p = allPts[pi];

            // Walk to containing triangle
            TriIndex startTri = walkToPoint(tris, allPts, lastInsertedTri, p);

            // Flood-fill bad triangles
            floodFillBadTris(tris, allPts, startTri, p, badTris);

            // Fallback: linear scan if walking failed or no bad triangles found
            if (badTris.empty()) {
                for (TriIndex ti = 0; ti < (TriIndex)tris.size(); ++ti) {
                    AdjTri& t = tris[ti];
                    if (!t.alive) continue;

                    Triangle tmp{ t.a, t.b, t.c };
                    tmp = ensureCCW(allPts, tmp);
                    if (inCircumcircle(allPts[tmp.A], allPts[tmp.B], allPts[tmp.C], p)) {
                        badTris.push_back(ti);
                    }
                }
            }

            if (badTris.empty()) {
                // Degenerate or duplicate point scenario; skip insertion.
                continue;
            }

            // Build boundary polygon: edges appearing exactly once among bad triangles
            edgeCount.clear();
            edgeInfo.clear();

            for (TriIndex ti : badTris) {
                AdjTri& t = tris[ti];

                // edges in the same sense as your Go:
                // edge 0: b-c, neighbor n0
                // edge 1: c-a, neighbor n1
                // edge 2: a-b, neighbor n2
                struct E { SiteIndex a, b; TriIndex n; };
                E edges[3] = {
                    { t.b, t.c, t.n0 },
                    { t.c, t.a, t.n1 },
                    { t.a, t.b, t.n2 },
                };

                for (auto& e : edges) {
                    uint64_t key = makeEdgeKey(e.a, e.b);
                    edgeCount[key] += 1;
                    edgeInfo[key] = PolyEdge{ e.a, e.b, e.n };
                }
            }

            // Remove bad triangles: delete edges from edgeToTri and mark dead
            for (TriIndex ti : badTris) {
                AdjTri& t = tris[ti];
                for (int slot = 0; slot < 3; ++slot) {
                    auto ek = triEdgeKey(t, slot).key;
                    edgeToTri.erase(ek);
                }
                t.alive = false;
            }

            // Collect boundary edges (count==1)
            polygon.clear();
            polygon.reserve(edgeCount.size());
            for (auto& kv : edgeCount) {
                if (kv.second == 1) {
                    polygon.push_back(edgeInfo[kv.first]);
                }
            }

            // Create new triangles from polygon edges to new point pi
            newTriIndices.clear();
            newTriIndices.reserve(polygon.size());

            for (auto& e : polygon) {
                AdjTri newTri{};
                newTri.a = e.a;
                newTri.b = e.b;
                newTri.c = pi;

                // n2 is across edge a-b (opposite c)
                newTri.n0 = kInvalidTri;
                newTri.n1 = kInvalidTri;
                newTri.n2 = e.neighbor;
                newTri.alive = true;

                TriIndex triIdx = (TriIndex)tris.size();
                tris.push_back(newTri);
                newTriIndices.push_back(triIdx);

                // Update neighbor to point back to us (if alive)
                if (e.neighbor >= 0 && e.neighbor < (TriIndex)tris.size() && tris[e.neighbor].alive) {
                    AdjTri& nt = tris[e.neighbor];
                    uint64_t edgeAB = makeEdgeKey(e.a, e.b);
                    for (int slot = 0; slot < 3; ++slot) {
                        if (triEdgeKey(nt, slot).key == edgeAB) {
                            neighborSlot(nt, slot) = triIdx;
                            break;
                        }
                    }
                }

                // Register edges in edgeToTri
                for (int slot = 0; slot < 3; ++slot) {
                    auto ed = triEdgeKey(tris[triIdx], slot);
                    edgeToTri[ed.key] = TriEdgeRef{ triIdx, ed.slot };
                }
            }

            // Link new triangles together via shared edges
            for (size_t i = 0; i < newTriIndices.size(); ++i) {
                TriIndex ti = newTriIndices[i];
                AdjTri& t = tris[ti];

                // edges to find siblings: (b, c=pi) and (c=pi, a)
                uint64_t key0 = makeEdgeKey(t.b, t.c);
                uint64_t key1 = makeEdgeKey(t.c, t.a);

                // find tri sharing those edges
                if (t.n0 == kInvalidTri) {
                    auto it = edgeToTri.find(key0);
                    if (it != edgeToTri.end()) {
                        TriIndex other = it->second.triIdx;
                        if (other != ti && tris[other].alive) {
                            t.n0 = other;
                            // set reciprocal neighbor slot on other
                            AdjTri& ot = tris[other];
                            for (int slot = 0; slot < 3; ++slot) {
                                if (triEdgeKey(ot, slot).key == key0) {
                                    neighborSlot(ot, slot) = ti;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (t.n1 == kInvalidTri) {
                    auto it = edgeToTri.find(key1);
                    if (it != edgeToTri.end()) {
                        TriIndex other = it->second.triIdx;
                        if (other != ti && tris[other].alive) {
                            t.n1 = other;
                            AdjTri& ot = tris[other];
                            for (int slot = 0; slot < 3; ++slot) {
                                if (triEdgeKey(ot, slot).key == key1) {
                                    neighborSlot(ot, slot) = ti;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (!newTriIndices.empty()) {
                lastInsertedTri = newTriIndices[0];
            }
        }

        // ---------- Publish result into final arena ----------
        auto* out = static_cast<Triangulation*>(finalMr->allocate(sizeof(Triangulation), alignof(Triangulation)));
        Triangulation* result = new (out) Triangulation(finalMr);

        result->tris.reserve(tris.size());
		//result->circumcenters.reserve(tris.size());   MOVED TO BuildHalfEdgeMesh
        //result->cache.reserve(tris.size());           MOVED TO BuildHalfEdgeMesh

        // Collect final triangles excluding super vertices
        for (auto& t : tris) {
            if (!t.alive) continue;

            bool usesSuper = (t.a == super0 || t.a == super1 || t.a == super2 ||
                t.b == super0 || t.b == super1 || t.b == super2 ||
                t.c == super0 || t.c == super1 || t.c == super2);

            if (usesSuper) continue;

            Triangle tri{ t.a, t.b, t.c };
            tri = ensureCCW(allPts, tri);
            result->tris.push_back(tri);
        }

        return result;
    }

    /* Math Warning */
    /* You are now entering mathematical territory */
    bool Barycentric(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const Vec2& p,
        BaryWeights& outW,
        float eps
    ) {
        if (!validTri(tri, triID)) return false;

        const Triangle& t = getTri(tri, triID);
        const SiteIndex A = triA(t), B = triB(t), C = triC(t);
        if (!validSite(pts, A) || !validSite(pts, B) || !validSite(pts, C)) return false;

        const Vec2 a = pts.pts[static_cast<size_t>(A)];
        const Vec2 b = pts.pts[static_cast<size_t>(B)];
        const Vec2 c = pts.pts[static_cast<size_t>(C)];

        const Vec2 v0 = b - a;
        const Vec2 v1 = c - a;
        const Vec2 v2 = p - a;

        const float denom = cross2(v0, v1); // signed 2*area
        if (std::abs(denom) < eps) return false;

        const float wb = cross2(v2, v1) / denom;
        const float wc = cross2(v0, v2) / denom;
        const float wa = 1.f - wb - wc;

        outW = BaryWeights{ wa, wb, wc };
        return true;
    }

    bool SampleScalar(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const Vec2& p,
        const float* valuesAtSites, size_t valuesCount,
        float& outValue
    ) {
        (void)pts; // pts used via validSite checks / indexing
        if (!valuesAtSites) return false;
        if (!validTri(tri, triID)) return false;
        if (valuesCount < tri.siteEdge.size() && valuesCount < valuesCount) {
            // No-op; left intentionally empty (we can’t assume siteEdge == vertexCount always)
        }
        // We actually need valuesCount >= pts.pts.size()
        if (valuesCount < pts.pts.size()) return false;

        BaryWeights w{};
        if (!Barycentric(pts, tri, triID, p, w)) return false;

        const Triangle& t = getTri(tri, triID);
        const SiteIndex A = triA(t), B = triB(t), C = triC(t);
        if (!validSite(pts, A) || !validSite(pts, B) || !validSite(pts, C)) return false;

        const float va = valuesAtSites[static_cast<size_t>(A)];
        const float vb = valuesAtSites[static_cast<size_t>(B)];
        const float vc = valuesAtSites[static_cast<size_t>(C)];

        outValue = w.wa * va + w.wb * vb + w.wc * vc;
        return true;
    }

    bool TriangleGradient(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const float* valuesAtSites, size_t valuesCount,
        float& outDhdx,
        float& outDhdz
    ) {
        if (!valuesAtSites) return false;
        if (!validTri(tri, triID)) return false;
        if (valuesCount < pts.pts.size()) return false;

        const Triangle& t = getTri(tri, triID);
        const SiteIndex A = triA(t), B = triB(t), C = triC(t);
        if (!validSite(pts, A) || !validSite(pts, B) || !validSite(pts, C)) return false;

        const Vec2 a = pts.pts[static_cast<size_t>(A)];
        const Vec2 b = pts.pts[static_cast<size_t>(B)];
        const Vec2 c = pts.pts[static_cast<size_t>(C)];

        const float ha = valuesAtSites[static_cast<size_t>(A)];
        const float hb = valuesAtSites[static_cast<size_t>(B)];
        const float hc = valuesAtSites[static_cast<size_t>(C)];

        const Vec2 ab = b - a;
        const Vec2 ac = c - a;

        const float det = cross2(ab, ac); // == signed 2*area
        const float scale = (ab.len() * ac.len());
        if (std::abs(det) < 1e-12f * std::max(scale, 1e-20f)) return false;

        const float rhs1 = hb - ha;
        const float rhs2 = hc - ha;

        // Inverse of [[ab.x ab.y],[ac.x ac.y]] is (1/det) * [[ ac.y, -ab.y],[-ac.x, ab.x]]
        outDhdx = (rhs1 * ac.y - rhs2 * ab.y) / det;
        outDhdz = (-rhs1 * ac.x + rhs2 * ab.x) / det;
        return true;
    }

    bool TriangleNormal(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const float* heights, size_t heightsCount,
        Vec3& outN
    ) {
        if (!heights) return false;
        if (!validTri(tri, triID)) return false;
        if (heightsCount < pts.pts.size()) return false;

        const Triangle& t = getTri(tri, triID);
        const SiteIndex A = triA(t), B = triB(t), C = triC(t);
        if (!validSite(pts, A) || !validSite(pts, B) || !validSite(pts, C)) return false;

        const Vec2 a2 = pts.pts[static_cast<size_t>(A)];
        const Vec2 b2 = pts.pts[static_cast<size_t>(B)];
        const Vec2 c2 = pts.pts[static_cast<size_t>(C)];

        const Vec3 a{ a2.x, heights[static_cast<size_t>(A)], a2.y };
        const Vec3 b{ b2.x, heights[static_cast<size_t>(B)], b2.y };
        const Vec3 c{ c2.x, heights[static_cast<size_t>(C)], c2.y };

        const Vec3 ab = b - a;
        const Vec3 ac = c - a;

        Vec3 n = Vec3::cross(ab, ac);
        const float L = n.len();
        if (L < 1e-12f) {
            outN = Vec3{ 0.f, 1.f, 0.f };
            return false;
        }

        outN = n * (1.f / L);
        return true;
    }

    bool SlopeAngleRadians(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const float* heights, size_t heightsCount,
        float& outAngleRad
    ) {
        Vec3 n{};
        if (!TriangleNormal(pts, tri, triID, heights, heightsCount, n)) return false;

        float cosAngle = n.y; // dot(n, up) since up=(0,1,0)
        cosAngle = std::clamp(cosAngle, -1.f, 1.f);

        outAngleRad = std::acos(cosAngle);
        return true;
    }

    bool SlopePercent(
        const AllPoints& pts,
        const Triangulation& tri,
        TriIndex triID,
        const float* heights, size_t heightsCount,
        float& outPercent
    ) {
        float dhdx = 0.f, dhdz = 0.f;
        if (!TriangleGradient(pts, tri, triID, heights, heightsCount, dhdx, dhdz)) return false;

        const float slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
        outPercent = slope * 100.f;
        return true;
    }

    static EdgeIndex findAnyOutgoing(const Triangulation& tri, SiteIndex site) {
        // This is O(E). You already have tri.siteEdge, so this is only a defensive fallback.
        for (size_t i = 0; i < tri.halfEdges.size(); ++i) {
            if (heOrigin(tri.halfEdges[i]) == site) return static_cast<EdgeIndex>(i);
        }
        return -1;
    }

    static void angleSortAround(
        const Vec2& center,
        std::pmr::vector<TriIndex>& tris,
        std::pmr::vector<Vec2>& verts,
        std::pmr::memory_resource* mr
    ) {
        struct Item { TriIndex tri; Vec2 v; float a; };

        std::pmr::vector<Item> items(mr);
        items.reserve(verts.size());

        for (size_t i = 0; i < verts.size(); ++i) {
            const Vec2 d = verts[i] - center;
            items.push_back(Item{
                tris[i],
                verts[i],
                std::atan2(d.y, d.x)
                });
        }

        std::sort(items.begin(), items.end(), [](const Item& lhs, const Item& rhs) {
            return lhs.a < rhs.a;
        });

        for (size_t i = 0; i < items.size(); ++i) {
            tris[i] = items[i].tri;
            verts[i] = items[i].v;
        }
    }

    // Includes optional angle-sorting
    VoronoiCell VoronoiCellForSite(
        const AllPoints& pts,
        const Triangulation& tri,
        SiteIndex site,
        std::pmr::memory_resource* mr,
        bool doAngleSort
    ) {
        VoronoiCell cell(mr);
        cell.site = site;
        cell.closed = false;

        if (!mr) return cell;
        if (!validSite(pts, site)) return cell;

        EdgeIndex start = -1;
        if (site >= 0 && static_cast<size_t>(site) < tri.siteEdge.size()) {
            start = tri.siteEdge[static_cast<size_t>(site)];
        }
        if (start == -1) return cell;

        // Ensure we have an outgoing edge whose origin == site (defensive)
        if (start < 0 || static_cast<size_t>(start) >= tri.halfEdges.size() ||
            heOrigin(tri.halfEdges[static_cast<size_t>(start)]) != site)
        {
            start = findAnyOutgoing(tri, site);
            if (start == -1) return cell;
        }

        cell.tris.reserve(8);
        cell.vertices.reserve(8);

        // Track visited edges to prevent infinite loops on malformed adjacency.
        std::pmr::unordered_set<EdgeIndex> visited(mr);
        visited.reserve(16);

        EdgeIndex e = start;
        bool closed = true;

        for (;;) {
            if (visited.find(e) != visited.end()) break;
            visited.insert(e);

            const HalfEdge& he = tri.halfEdges[static_cast<size_t>(e)];
            const TriIndex ti = heTri(he);

            if (!validTri(tri, ti) || static_cast<size_t>(ti) >= tri.circumcenters.size()) {
                closed = false;
                break;
            }

            cell.tris.push_back(ti);
            cell.vertices.push_back(tri.circumcenters[static_cast<size_t>(ti)]);

            const EdgeIndex tw = heTwin(he);
            if (tw == -1) { closed = false; break; }
            if (tw < 0 || static_cast<size_t>(tw) >= tri.halfEdges.size()) { closed = false; break; }

            // Move to next edge around the same site: twin.next
            const EdgeIndex next = heNext(tri.halfEdges[static_cast<size_t>(tw)]);
            if (next < 0 || static_cast<size_t>(next) >= tri.halfEdges.size()) { closed = false; break; }

            e = next;

            // Safety: ensure we're still around the same site
            if (heOrigin(tri.halfEdges[static_cast<size_t>(e)]) != site) {
                closed = false;
                break;
            }
        }

        cell.closed = closed;

        if (doAngleSort) {
            angleSortAround(
                pts.pts[static_cast<size_t>(site)],
                cell.tris,
                cell.vertices,
                mr
            );
        }

        return cell;
    }
}