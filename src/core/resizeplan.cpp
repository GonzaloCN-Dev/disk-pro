// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "resizeplan.h"
#include <algorithm>

namespace diskpro {
namespace {
uint64_t roundNearest(uint64_t v) { return (v + kPlanAlign / 2) / kPlanAlign * kPlanAlign; }
}  // namespace

ResizeOutcome planResize(uint64_t diskSize, const std::vector<PlanPart>& parts, int index, DragEdge edge, uint64_t wanted,
                         uint64_t grab) {
    ResizeOutcome r;
    const PlanPart* me = nullptr;
    for (auto& p : parts) if (p.index == index) me = &p;
    if (!me || me->length == 0 || diskSize < 3 * kPlanAlign) return r;

    uint64_t low = kPlanAlign, high = diskSize - kPlanAlign;
    for (auto& p : parts) {
        if (&p == me || p.length == 0) continue;
        if (p.offset + p.length <= me->offset) low = std::max(low, p.offset + p.length);
        else if (p.offset >= me->offset + me->length) high = std::min(high, p.offset);
        else { r.warnings.push_back("Overlapping partitions: the plan is not reliable."); }
    }
    const uint64_t oldStart = me->offset, oldEnd = me->offset + me->length;
    low = std::min(low, oldStart);          // never claim less room than the partition already has
    high = std::max(high, oldEnd);
    r.minOffset = low; r.maxEnd = high;

    uint64_t start = oldStart, end = oldEnd;
    auto near = [](uint64_t a, uint64_t b) { return (a > b ? a - b : b - a) < kPlanAlign; };
    if (edge == DragEdge::Left) {
        uint64_t w = std::clamp(wanted, low, oldEnd - std::min<uint64_t>(kPlanAlign, oldEnd - low));
        start = near(w, oldStart) ? oldStart : roundNearest(w);
        start = std::clamp(start, low, oldEnd - kPlanAlign);
    } else if (edge == DragEdge::Right) {
        uint64_t w = std::clamp(wanted, oldStart + kPlanAlign, high);
        end = near(w, oldEnd) ? oldEnd : roundNearest(w);
        end = std::clamp(end, oldStart + kPlanAlign, high);
    } else {
        const uint64_t len = me->length;
        uint64_t s = wanted > grab ? wanted - grab : 0;
        s = std::clamp(s, low, high - len);
        start = near(s, oldStart) ? oldStart : roundNearest(s);
        start = std::clamp(start, low, high - len);
        end = start + len;
    }
    r.offset = start; r.length = end - start; r.valid = true;
    r.changed = (start != oldStart || end != oldEnd);
    if (r.changed) {
        if (r.length < me->length) r.warnings.push_back("Shrinking: data stored in the removed area would be lost unless the file system is shrunk first.");
        if (r.length > me->length) r.warnings.push_back("Growing: the file system must be extended afterwards to use the new space.");
        if (r.length == me->length) r.warnings.push_back("Moving: the contents have to be copied to the new position.");
        if (start != oldStart && r.length != me->length) r.warnings.push_back("Moving the start requires copying the contents.");
    }
    r.warnings.push_back("Preview only: this version does not apply partition changes.");
    return r;
}

HitResult hitTest(const std::vector<PlanPart>& parts, uint64_t pos, uint64_t tol) {
    HitResult best;
    uint64_t bestDist = ~0ull;
    auto dist = [](uint64_t a, uint64_t b) { return a > b ? a - b : b - a; };
    for (auto& p : parts) {
        if (p.length == 0) continue;
        uint64_t dl = dist(pos, p.offset), dr = dist(pos, p.offset + p.length);
        const bool inside = pos >= p.offset && pos < p.offset + p.length;   // on a tie the partition under the pointer wins
        if (dr <= tol && (dr < bestDist || (dr == bestDist && inside))) { bestDist = dr; best = {true, p.index, DragEdge::Right}; }
        if (dl <= tol && (dl < bestDist || (dl == bestDist && inside))) { bestDist = dl; best = {true, p.index, DragEdge::Left}; }
    }
    if (best.found) return best;
    for (auto& p : parts)
        if (p.length && pos >= p.offset && pos < p.offset + p.length) return {true, p.index, DragEdge::Move};
    return best;
}

}  // namespace diskpro