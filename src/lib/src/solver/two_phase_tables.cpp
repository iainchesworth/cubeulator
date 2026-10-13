#include "two_phase_tables.hpp"

// The one translation unit that asks the compiler to evaluate the two-phase
// solver's compile-time tables (see two_phase_tables.hpp). It is built with a
// raised constant-evaluation budget (src/lib/CMakeLists.txt); keeping the
// evaluation here means no other file needs that budget and a change to the
// generators recompiles one small file.

namespace cube::solver::detail {

namespace {

constexpr SliceNumbering kSliceNumbering = make_slice_numbering();
constexpr Phase1MoveTables kPhase1MoveTables{kSliceNumbering};

}  // namespace

const SliceNumbering& slice_numbering() noexcept {
    return kSliceNumbering;
}

const Phase1MoveTables& phase1_move_tables() noexcept {
    return kPhase1MoveTables;
}

}  // namespace cube::solver::detail
