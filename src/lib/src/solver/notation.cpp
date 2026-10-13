#include <cube/solver/notation.hpp>
#include <ranges>

#include <fmt/format.h>

namespace cube::solver {

namespace {

constexpr char face_letter(Face face) noexcept {
    switch (face) {
        case Face::U:
            return 'U';
        case Face::D:
            return 'D';
        case Face::L:
            return 'L';
        case Face::R:
            return 'R';
        case Face::F:
            return 'F';
        case Face::B:
            return 'B';
    }
    return '?';
}

}  // namespace

Result<Move> parse_move(std::string_view token) {
    if (token.empty() || token.size() > 2) {
        return std::unexpected(CubeError::invalid_move_sequence);
    }

    Face face{};
    switch (token[0]) {
        case 'U':
            face = Face::U;
            break;
        case 'D':
            face = Face::D;
            break;
        case 'L':
            face = Face::L;
            break;
        case 'R':
            face = Face::R;
            break;
        case 'F':
            face = Face::F;
            break;
        case 'B':
            face = Face::B;
            break;
        default:
            return std::unexpected(CubeError::invalid_move_sequence);
    }

    int turns = 1;
    if (token.size() == 2) {
        if (token[1] == '\'') {
            turns = 3;
        } else if (token[1] == '2') {
            turns = 2;
        } else {
            return std::unexpected(CubeError::invalid_move_sequence);
        }
    }

    return Move{.face = face, .turns = turns};
}

Result<std::vector<Move>> parse_algorithm(std::string_view text) {
    std::vector<Move> moves;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != ' ') {
            ++pos;
        }
        if (pos == start) {
            break;
        }
        Result<Move> move = parse_move(text.substr(start, pos - start));
        if (!move.has_value()) {
            return std::unexpected(move.error());
        }
        moves.push_back(*move);
    }
    return moves;
}

std::string to_string(Move move) {
    switch (move.turns) {
        case 2:
            return fmt::format("{}2", face_letter(move.face));
        case 3:
            return fmt::format("{}'", face_letter(move.face));
        default:
            return fmt::format("{}", face_letter(move.face));
    }
}

std::string to_string(const std::vector<Move>& moves) {
    std::string result;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (i > 0) {
            result += ' ';
        }
        result += to_string(moves[i]);
    }
    return result;
}

std::vector<Move> inverse(const std::vector<Move>& moves) {
    std::vector<Move> result;
    result.reserve(moves.size());
    for (auto move : std::views::reverse(moves)) {
        result.push_back(move.inverse());
    }
    return result;
}

}  // namespace cube::solver
