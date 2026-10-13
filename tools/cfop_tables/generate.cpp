// cube_generate_cfop_tables: derives the last-layer tables the intermediate
// (CFOP) solver reads, and writes them as C++ to stdout.
//
//   cube_generate_cfop_tables > src/lib/src/solver/cfop_case_words.inc
//
// The tables are checked in, not built, so a normal build never runs this and
// the solver does no searching of its own for the last layer. Re-run it only
// if the alphabet or the half-length below changes; tests/unit/solver/
// cfop_test.cpp then proves the new file solves every case.
//
// What it computes, for the 216 OLL cases and 288 PLL cases, is the shortest
// move sequence over <U, R, F> that leaves the first two layers exactly as
// they were and finishes the step. It does so by meeting in the middle:
// every last-layer algorithm of up to 16 moves is `a` then `b` with each
// half at most 8, and `a` then `b` leaves the first two layers alone exactly
// when `b` puts the first-two-layers pieces where `a`'s inverse would. So
// all words of up to 8 moves are enumerated once, filed by what they do to
// the first two layers' positions, and each is paired with the files that
// complete it.
//
// <U, R, F> rather than all six sides because it is the set people actually
// learn last-layer algorithms in: every sequence in the output is something
// a learner can practise with a right hand and the occasional F.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>

#include "cfop_case_index.hpp"
#include "cubie.hpp"

namespace {

using cube::solver::Face;
using cube::solver::Move;
using cube::solver::detail::CubieCube;

constexpr std::size_t kHalfLength = 8;
constexpr std::array<Face, 3> kAlphabet = {Face::U, Face::R, Face::F};

// Faces are numbered U D L R F B; opposite faces differ in the low bit.
bool may_follow(std::size_t face, int last) noexcept {
    if (static_cast<int>(face) == last) {
        return false;
    }
    return !(last == static_cast<int>(face ^ 1) && static_cast<int>(face) < last);
}

CubieCube inverse_of(const CubieCube& a) noexcept {
    CubieCube r;
    for (std::size_t i = 0; i < 8; ++i) {
        r.cp[a.cp[i]] = static_cast<std::uint8_t>(i);
        r.co[a.cp[i]] = static_cast<std::uint8_t>((3 - a.co[i]) % 3);
    }
    for (std::size_t i = 0; i < 12; ++i) {
        r.ep[a.ep[i]] = static_cast<std::uint8_t>(i);
        r.eo[a.ep[i]] = a.eo[i];
    }
    return r;
}

// The first two layers' positions (corners 4..7, edges 4..11), packed: what
// a word leaves there. Two words compose into a last-layer algorithm exactly
// when the second's signature is the first's inverse's.
std::uint64_t f2l_signature(const CubieCube& c) noexcept {
    std::uint64_t s = 0;
    for (std::size_t i = 4; i < 8; ++i) {
        s = (s << 3) | c.cp[i];
        s = (s << 2) | c.co[i];
    }
    for (std::size_t i = 4; i < 12; ++i) {
        s = (s << 4) | c.ep[i];
        s = (s << 1) | c.eo[i];
    }
    return s;
}

// Full identity of a last-layer element (the four corners and edges in
// positions 0..3 - with the first two layers fixed, nothing else varies).
std::uint64_t element_key(const CubieCube& c) noexcept {
    std::uint64_t k = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        k = k * 4 + c.cp[i];
        k = k * 3 + c.co[i];
        k = k * 4 + c.ep[i];
        k = k * 2 + c.eo[i];
    }
    return k;
}

struct Word {
    CubieCube state;
    std::array<std::uint8_t, kHalfLength> moves{};
    std::uint8_t length = 0;
};

void enumerate(const CubieCube& state, std::size_t depth, int last, Word& scratch,
               std::vector<Word>& out) {
    scratch.state = state;
    scratch.length = static_cast<std::uint8_t>(depth);
    out.push_back(scratch);
    if (depth == kHalfLength) {
        return;
    }
    for (const Face face : kAlphabet) {
        const auto face_index = static_cast<std::size_t>(face);
        if (!may_follow(face_index, last)) {
            continue;
        }
        for (std::size_t turn = 0; turn < 3; ++turn) {
            const std::size_t move = face_index * 3 + turn;
            scratch.moves[depth] = static_cast<std::uint8_t>(move);
            enumerate(cube::solver::detail::multiply(state, cube::solver::detail::move_cubie(move)),
                      depth + 1, static_cast<int>(face_index), scratch, out);
        }
    }
}

// Shorter is better; among equals, fewer F turns (the awkward one), then the
// lexicographically smaller move list - so the output does not depend on
// hash-map iteration order.
struct Candidate {
    std::vector<std::uint8_t> moves;
    CubieCube effect;

    [[nodiscard]] std::array<std::size_t, 2> cost() const {
        std::size_t f_turns = 0;
        for (const std::uint8_t m : moves) {
            f_turns += (m / 3 == static_cast<std::size_t>(Face::F)) ? 1 : 0;
        }
        return {moves.size(), f_turns};
    }

    [[nodiscard]] bool better_than(const Candidate& other) const {
        const auto a = cost();
        const auto b = other.cost();
        if (a != b) {
            return a < b;
        }
        return moves < other.moves;
    }
};

// Merges adjacent same-face turns and drops cancelled ones. The pairing of
// two halves can leave "R" then "R'" at the seam; the shortest word for the
// element never has that (it would have been found as a shorter pair), so
// this is only a guard, but a table entry must not carry a redundancy.
std::vector<std::uint8_t> normalised(const std::vector<std::uint8_t>& moves) {
    std::vector<Move> kept;
    for (const std::uint8_t m : moves) {
        const Move move = cube::solver::detail::move_from_index(m);
        if (!kept.empty() && kept.back().face == move.face) {
            const int turns = (kept.back().turns + move.turns) % 4;
            kept.pop_back();
            if (turns != 0) {
                kept.push_back(Move{move.face, turns});
            }
        } else {
            kept.push_back(move);
        }
    }
    std::vector<std::uint8_t> out;
    for (const Move& m : kept) {
        out.push_back(static_cast<std::uint8_t>(cube::solver::detail::move_index(m)));
    }
    return out;
}

std::string as_text(const std::vector<std::uint8_t>& moves) {
    std::vector<Move> list;
    for (const std::uint8_t m : moves) {
        list.push_back(cube::solver::detail::move_from_index(m));
    }
    return cube::solver::to_string(list);
}

}  // namespace

int main() {
    using cube::solver::detail::multiply;

    // All words of up to kHalfLength moves, with their effect.
    std::vector<Word> words;
    {
        Word scratch;
        enumerate(CubieCube{}, 0, -1, scratch, words);
    }
    std::fprintf(stderr, "%zu words of up to %zu moves\n", words.size(), kHalfLength);

    // File them by what they do to the first two layers.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> by_signature;
    by_signature.reserve(words.size());
    for (std::uint32_t i = 0; i < words.size(); ++i) {
        by_signature.emplace_back(f2l_signature(words[i].state), i);
    }
    std::ranges::sort(by_signature);

    // Every pairing that leaves the first two layers alone is a last-layer
    // algorithm; keep the best one per resulting arrangement.
    std::unordered_map<std::uint64_t, Candidate> best;
    best.reserve(70'000);
    for (const Word& a : words) {
        const std::uint64_t wanted = f2l_signature(inverse_of(a.state));
        auto [lo, hi] = std::ranges::equal_range(by_signature, wanted, {},
                                                 &std::pair<std::uint64_t, std::uint32_t>::first);
        for (auto it = lo; it != hi; ++it) {
            const Word& b = words[it->second];
            Candidate candidate;
            candidate.moves.assign(a.moves.begin(), a.moves.begin() + a.length);
            candidate.moves.insert(candidate.moves.end(), b.moves.begin(),
                                   b.moves.begin() + b.length);
            candidate.moves = normalised(candidate.moves);
            candidate.effect = multiply(a.state, b.state);

            const std::uint64_t key = element_key(candidate.effect);
            auto found = best.find(key);
            if (found == best.end()) {
                best.emplace(key, std::move(candidate));
            } else if (candidate.better_than(found->second)) {
                found->second = std::move(candidate);
            }
        }
    }
    std::fprintf(stderr, "%zu last-layer arrangements reached\n", best.size());

    // OLL: for each orientation case, the best algorithm that leaves every
    // U-layer corner and edge oriented wherever it started. An algorithm g
    // does that exactly when, for every position i, the orientation the case
    // has at the position g draws from, plus g's own turn at i, is zero.
    std::vector<const Candidate*> oll(cube::solver::detail::kOllCaseCount, nullptr);
    for (std::size_t index = 0; index < oll.size(); ++index) {
        std::array<int, 4> co{};
        std::array<int, 4> eo{};
        std::size_t v = index;
        eo[2] = static_cast<int>(v % 2);
        v /= 2;
        eo[1] = static_cast<int>(v % 2);
        v /= 2;
        eo[0] = static_cast<int>(v % 2);
        v /= 2;
        co[2] = static_cast<int>(v % 3);
        v /= 3;
        co[1] = static_cast<int>(v % 3);
        v /= 3;
        co[0] = static_cast<int>(v % 3);
        co[3] = (3 - (co[0] + co[1] + co[2]) % 3) % 3;
        eo[3] = (eo[0] + eo[1] + eo[2]) % 2;

        for (const auto& [key, candidate] : best) {
            const CubieCube& g = candidate.effect;
            bool resolves = true;
            for (std::size_t i = 0; i < 4 && resolves; ++i) {
                resolves = (co[g.cp[i]] + g.co[i]) % 3 == 0 && (eo[g.ep[i]] + g.eo[i]) % 2 == 0;
            }
            if (resolves && (oll[index] == nullptr || candidate.better_than(*oll[index]))) {
                oll[index] = &candidate;
            }
        }
        if (oll[index] == nullptr) {
            std::fprintf(stderr, "no algorithm for OLL case %zu\n", index);
            return 1;
        }
    }

    // PLL: a fully oriented arrangement S is finished by exactly the
    // algorithm equal to S's inverse.
    std::vector<const Candidate*> pll(cube::solver::detail::kPllSlotCount, nullptr);
    std::array<std::array<std::uint8_t, 4>, 24> perms{};
    {
        std::array<std::uint8_t, 4> p = {0, 1, 2, 3};
        std::size_t n = 0;
        do {
            perms[n++] = p;
        } while (std::ranges::next_permutation(p).found);
    }
    std::size_t pll_cases = 0;
    for (const auto& corners : perms) {
        for (const auto& edges : perms) {
            using cube::solver::detail::parity_of_four;
            if (parity_of_four(corners.data()) != parity_of_four(edges.data())) {
                continue;
            }
            CubieCube s;
            CubieCube g;
            for (std::size_t i = 0; i < 4; ++i) {
                s.cp[i] = corners[i];
                s.ep[i] = edges[i];
                g.cp[corners[i]] = static_cast<std::uint8_t>(i);
                g.ep[edges[i]] = static_cast<std::uint8_t>(i);
            }
            const auto found = best.find(element_key(g));
            if (found == best.end()) {
                std::fprintf(stderr, "no algorithm for a PLL case\n");
                return 1;
            }
            pll[cube::solver::detail::pll_slot(s)] = &found->second;
            ++pll_cases;
        }
    }

    // Statistics, for the header comment and the log.
    auto summarise = [](const std::vector<const Candidate*>& table) {
        std::size_t count = 0;
        std::size_t total = 0;
        std::size_t longest = 0;
        for (const Candidate* c : table) {
            if (c != nullptr) {
                ++count;
                total += c->moves.size();
                longest = std::max(longest, c->moves.size());
            }
        }
        return std::array<double, 3>{static_cast<double>(count),
                                     static_cast<double>(total) / static_cast<double>(count),
                                     static_cast<double>(longest)};
    };
    const auto oll_stats = summarise(oll);
    const auto pll_stats = summarise(pll);

    std::printf(
        "// GENERATED by tools/cfop_tables/generate.cpp - do not edit by hand.\n"
        "//\n"
        "// The shortest sequence over <U, R, F>, per case, that finishes a last-layer\n"
        "// step without disturbing the first two layers.\n"
        "//   OLL: %.0f cases, %.2f moves on average, longest %.0f\n"
        "//   PLL: %.0f cases, %.2f moves on average, longest %.0f\n"
        "// An empty string is a case that needs no moves (or, for PLL, a slot a real\n"
        "// cube never reaches: corner and edge parity must match).\n"
        "\n",
        oll_stats[0], oll_stats[1], oll_stats[2], pll_stats[0], pll_stats[1], pll_stats[2]);

    std::printf("inline constexpr std::array<std::string_view, %zu> kOllWords = {{\n", oll.size());
    for (const Candidate* c : oll) {
        std::printf("    \"%s\",\n", as_text(c->moves).c_str());
    }
    std::printf("}};\n\n");

    std::printf("inline constexpr std::array<std::string_view, %zu> kPllWords = {{\n", pll.size());
    for (const Candidate* c : pll) {
        std::printf("    \"%s\",\n", c == nullptr ? "" : as_text(c->moves).c_str());
    }
    std::printf("}};\n");

    std::fprintf(stderr, "OLL %.0f cases mean %.2f max %.0f; PLL %zu cases mean %.2f max %.0f\n",
                 oll_stats[0], oll_stats[1], oll_stats[2], pll_cases, pll_stats[1], pll_stats[2]);
    return 0;
}
