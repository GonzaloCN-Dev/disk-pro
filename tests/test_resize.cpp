#include <cstdio>
#include "resizeplan.h"
using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
constexpr uint64_t M = 1ull << 20;

int main() {
    const uint64_t disk = 1000 * M;
    std::vector<PlanPart> ps = {{1, 1 * M, 100 * M}, {2, 200 * M, 300 * M}, {3, 700 * M, 100 * M}};

    // Right edge: growing into the gap, then clamped by the next partition
    auto r = planResize(disk, ps, 2, DragEdge::Right, 600 * M);
    CHECK(r.valid && r.changed && r.offset == 200 * M && r.length == 400 * M);
    r = planResize(disk, ps, 2, DragEdge::Right, 900 * M);
    CHECK(r.length == 500 * M && r.offset + r.length == 700 * M);               // stops at the neighbour
    // Right edge: shrinking, keeps at least 1 MiB
    r = planResize(disk, ps, 2, DragEdge::Right, 10 * M);
    CHECK(r.length == 1 * M && r.offset == 200 * M);
    // Left edge
    r = planResize(disk, ps, 2, DragEdge::Left, 150 * M);
    CHECK(r.offset == 150 * M && r.offset + r.length == 500 * M);
    r = planResize(disk, ps, 2, DragEdge::Left, 5 * M);
    CHECK(r.offset == 101 * M);                                                   // stops at the previous partition
    r = planResize(disk, ps, 2, DragEdge::Left, 999 * M);
    CHECK(r.length == 1 * M && r.offset + r.length == 500 * M);
    // Move keeps the size and never crosses neighbours
    r = planResize(disk, ps, 2, DragEdge::Move, 300 * M, 0);
    CHECK(r.length == 300 * M && r.offset == 300 * M);
    r = planResize(disk, ps, 2, DragEdge::Move, 350 * M, 50 * M);
    CHECK(r.length == 300 * M && r.offset == 300 * M && r.changed);
    r = planResize(disk, ps, 2, DragEdge::Move, 900 * M, 0);
    CHECK(r.offset + r.length == 700 * M && r.length == 300 * M);
    r = planResize(disk, ps, 2, DragEdge::Move, 0, 0);
    CHECK(r.offset == 101 * M && r.length == 300 * M);
    // Sub-MiB jitter does not change anything
    r = planResize(disk, ps, 2, DragEdge::Right, 500 * M + 1000);
    CHECK(r.valid && !r.changed && r.length == 300 * M);
    // Alignment: always a multiple of 1 MiB when it changes
    r = planResize(disk, ps, 2, DragEdge::Right, 600 * M + 300000);
    CHECK(r.changed && r.length % M == 0 && r.offset % M == 0);
    // First partition cannot start before 1 MiB; last cannot end past disk - 1 MiB
    r = planResize(disk, ps, 1, DragEdge::Left, 0);
    CHECK(r.offset == 1 * M);
    r = planResize(disk, ps, 3, DragEdge::Right, 5000 * M);
    CHECK(r.offset + r.length == disk - M);
    // Warnings
    r = planResize(disk, ps, 2, DragEdge::Right, 400 * M);
    bool shrink = false; for (auto& w : r.warnings) shrink |= w.find("Shrinking") != std::string::npos;
    CHECK(shrink);
    r = planResize(disk, ps, 2, DragEdge::Right, 600 * M);
    bool grow = false, prev = false; for (auto& w : r.warnings) { grow |= w.find("Growing") != std::string::npos; prev |= w.find("Preview only") != std::string::npos; }
    CHECK(grow && prev);
    // Invalid
    CHECK(!planResize(disk, ps, 9, DragEdge::Right, 5 * M).valid);
    CHECK(!planResize(2 * M, ps, 2, DragEdge::Right, 5 * M).valid);
    // A partition that already extends outside the usual room is not made smaller by the clamp alone
    std::vector<PlanPart> odd = {{1, 1 * M, 100 * M}, {2, 101 * M, disk - 101 * M}};
    r = planResize(disk, odd, 2, DragEdge::Right, disk);
    CHECK(r.valid && !r.changed && r.length == disk - 101 * M);
    // Hit test
    { auto h = hitTest(ps, 500 * M, 2 * M); CHECK(h.found && h.index == 2 && h.edge == DragEdge::Right);
      h = hitTest(ps, 201 * M, 2 * M); CHECK(h.found && h.index == 2 && h.edge == DragEdge::Left);
      h = hitTest(ps, 350 * M, 2 * M); CHECK(h.found && h.index == 2 && h.edge == DragEdge::Move);
      h = hitTest(ps, 600 * M, 2 * M); CHECK(!h.found);                      // free space
      h = hitTest(ps, 0, 2 * M); CHECK(h.found && h.index == 1 && h.edge == DragEdge::Left);  // 1 MiB away, within tol
      std::vector<PlanPart> adj = {{1, 1 * M, 100 * M}, {2, 101 * M, 100 * M}};
      h = hitTest(adj, 101 * M + M / 4, 2 * M); CHECK(h.found && h.index == 2 && h.edge == DragEdge::Left);  // nearest edge wins
      h = hitTest(adj, 101 * M - M / 4, 2 * M); CHECK(h.found && h.index == 1 && h.edge == DragEdge::Right); }
    std::printf(failures ? "resize: %d failures\n" : "resize: all passed\n", failures);
    return failures ? 1 : 0;
}