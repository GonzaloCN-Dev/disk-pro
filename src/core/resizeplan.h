// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Portable model for the interactive partition bar: given a drag on an edge (or the body) of a partition it returns the
// new geometry, clamped to the neighbours and aligned. PURE PLANNING: nothing here touches a disk, and this version
// never applies it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace diskpro {

constexpr uint64_t kPlanAlign = 1ull << 20;   // 1 MiB alignment, also the minimum size and the reserved start/end

struct PlanPart { int index = 0; uint64_t offset = 0, length = 0; };

enum class DragEdge { Left, Right, Move };

struct ResizeOutcome {
    bool valid = false;        // false if the partition was not found or the disk is too small
    bool changed = false;
    uint64_t offset = 0, length = 0;
    uint64_t minOffset = 0, maxEnd = 0;   // the room available around the partition
    std::vector<std::string> warnings;
};

// `wanted` is the byte position under the mouse: the new start (Left, Move) or the new end (Right).
// Neighbours are never crossed and no partition is reordered.
ResizeOutcome planResize(uint64_t diskSize, const std::vector<PlanPart>& parts, int index, DragEdge edge, uint64_t wanted,
                         uint64_t grabOffset = 0);

struct HitResult { bool found = false; int index = 0; DragEdge edge = DragEdge::Move; };
// What is under byte position `pos`? An edge if within `tol` bytes of a partition boundary (the nearest one wins),
// else the partition body (Move), else nothing (free space).
HitResult hitTest(const std::vector<PlanPart>& parts, uint64_t pos, uint64_t tol);

}  // namespace diskpro