#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <deque>
#include <iostream>
#include <queue>
#include <algorithm>
#include <set>
namespace py = pybind11;
using namespace std;
using Board = vector<vector<map<string, string>>>;
using Cell = map<string, string>;
Cell empty_cell = {};

/*
=========================================================
 STUDENT AGENT FOR STONES & RIVERS GAME
---------------------------------------------------------
 The Python game engine passes the BOARD state into C++.
 Each board cell is represented as a dictionary in Python:

    {
        "owner": "circle" | "square",          // which player owns this piece
        "side": "stone" | "river",             // piece type
        "orientation": "horizontal" | "vertical"  // only relevant if side == "river"
    }

 In C++ with pybind11, this becomes:

    Board

 Meaning:
   - board[y][x] gives the cell at (x, y).
   - board[y][x].empty() → true if the cell is empty (no piece).
   - board[y][x].at("owner") → "circle" or "square".
   - board[y][x].at("side") → "stone" or "river".
   - board[y][x].at("orientation") → "horizontal" or "vertical".

=========================================================
*/

// ---- Move struct ----
struct Move {
    string action;
    vector<int> from;
    vector<int> to;
    vector<int> pushed_to;
    string orientation;
};

// Random 64-bit generator (fixed seed for reproducibility — change if you want)
static std::mt19937_64 zob_rng(1337);
static std::uniform_int_distribution<uint64_t> zob_dist(0, UINT64_MAX);

// Dynamic zobrist map: key -> random 64-bit value
static std::unordered_map<std::string, uint64_t> zobrist_map;

// Recent history (file-scope). Stores last HASH_HISTORY board hashes.
static std::deque<uint64_t> recent_hashes;
static const int HASH_HISTORY = 4;   // keep previous 4 boards

// Helper: produce / get a random 64-bit for a variant key
static uint64_t zob_get_or_create(const std::string &key) {
    auto it = zobrist_map.find(key);
    if (it != zobrist_map.end()) return it->second;
    uint64_t v = zob_dist(zob_rng);
    zobrist_map.emplace(key, v);
    return v;
}

static std::string zobrist_key_for_cell(int x, int y,
                                        const std::map<std::string,std::string> &cell)
{
    // Extract fields
    std::string type   = cell.count("side")        ? cell.at("side")        : "empty";
    std::string owner  = cell.count("owner")       ? cell.at("owner")       : "none";
    
    // IMPORTANT: ignore orientation for stones
    std::string orient;
    if (type == "river") {
        orient = cell.count("orientation") ? cell.at("orientation") : "";
    } else {
        orient = "";   // ignore orientation if stone
    }

    // Build positional zobrist key
    std::ostringstream ss;
    ss << x << ',' << y << '|' << type << '|' << owner << '|' << orient;
    return ss.str();
}


// Compute zobrist hash for a Board
static uint64_t computeZobristHash(const Board &board) {
    uint64_t h = 0ULL;
    for (int y = 0; y < (int)board.size(); ++y) {
        for (int x = 0; x < (int)board[y].size(); ++x) {
            if (board[y][x].empty()) continue;
            std::string key = zobrist_key_for_cell(x, y, board[y][x]);
            uint64_t v = zob_get_or_create(key);
            h ^= v;
        }
    }
    return h;
}

// Check / push functions for recent hashes
static bool seenRecently(uint64_t h) {
    return std::find(recent_hashes.begin(), recent_hashes.end(), h) != recent_hashes.end();
}
static void pushHash(uint64_t h) {
    recent_hashes.push_back(h);
    if ((int)recent_hashes.size() > HASH_HISTORY) recent_hashes.pop_front();
}

string opponent(const string& player) {
    return (player == "circle") ? "square" : "circle";
}

bool in_bounds(int x, int y, int rows, int cols) {
    return (x >= 0 && x < cols && y >= 0 && y < rows);
}

bool is_opponent_score_cell(int x, int y, const string& player, int rows, int cols, const vector<int>& score_cols) {
    if (player == "circle") {
        return (y == rows - 3) && (find(score_cols.begin(), score_cols.end(), x) != score_cols.end());
    } else {
        return (y == 2) && (find(score_cols.begin(), score_cols.end(), x) != score_cols.end());
    }
}

bool is_own_score_cell(int x, int y, const string& player, int rows, int cols, const vector<int>& score_cols) {
    return is_opponent_score_cell(x, y, opponent(player), rows, cols, score_cols);
}

int own_scoring_row(const string& player, int rows) {
    return (player == "circle") ? 2 : rows - 3;
}


vector<pair<int,int>> get_river_flow_destinations(
        const Board& board,
        int rx, int ry, int sx, int sy,
        const string& player, int rows, int cols,
        const vector<int>& score_cols,
        bool river_push=false) { // river push true when sx sy river else false

    vector<pair<int,int>> destinations;
    set<pair<int,int>> visited;
    deque <pair<int,int>> queue = {{rx,ry}};

    while (!queue.empty()) {
        auto [x,y] = queue.front();
        queue.pop_front();
        if (visited.count({x,y}) || !in_bounds(x,y,rows,cols)) continue;
        visited.insert({x,y});

        auto cell = board[y][x];
        if (river_push && x==rx && y==ry) {
            cell = board[sy][sx];
        }

        if (cell.empty()) {
            if (!is_opponent_score_cell(x,y,player,rows,cols,score_cols)) {
                destinations.push_back({x,y});
            }
            continue;
        }
        if (cell.at("side") != "river") continue;

        vector<pair<int,int>> dirs = 
            (cell.at("orientation")=="horizontal") ? vector<pair<int,int>>{{1,0},{-1,0}}
                                                   : vector<pair<int,int>>{{0,1},{0,-1}};

        for (auto [dx,dy] : dirs) {
            int nx = x+dx, ny = y+dy;
            while (in_bounds(nx,ny,rows,cols)) {
                if (is_opponent_score_cell(nx,ny,player,rows,cols,score_cols)) break;
                auto next_cell = board[ny][nx];
                if (next_cell.empty()) {
                    destinations.push_back({nx,ny});
                    nx += dx; ny += dy;
                    continue;
                }
                if (nx==sx && ny==sy) { nx += dx; ny += dy; continue; }
                if (next_cell.at("side") == "river") {
                    queue.push_back({nx,ny});
                    break;
                }
                break;
            }
        }
    }
    // deduplicate
    vector<pair<int,int>> out;
    set<pair<int,int>> seen;
    for (auto& d : destinations) {
        if (!seen.count(d)) {
            seen.insert(d);
            out.push_back(d);
        }
    }
    return out;
}

pair<
vector<pair<int,int>>, 
vector<pair<pair<int,int>, pair<int,int>>>>
compute_valid_targets(
    const Board& board,
    int sx, int sy, const string& player,
    int rows, int cols, const vector<int>& score_cols)
{
    set<pair<int,int>> moves;  
    vector<pair<pair<int,int>, pair<int,int>>> pushes;

    if (!in_bounds(sx, sy, rows, cols)) {
        return {{}, {}};
    }

    const auto& p = board[sy][sx];
    if (p.empty() || p.at("owner") != player) {
        return {{}, {}};
    }

    vector<pair<int,int>> dirs = {{1,0}, {-1,0}, {0,1}, {0,-1}};

    for (auto [dx,dy] : dirs) {
        int tx = sx + dx, ty = sy + dy;
        if (!in_bounds(tx,ty,rows,cols)) continue;

        // block entering opponent score
        if (is_opponent_score_cell(tx,ty,player,rows,cols,score_cols)) {
            continue;
        }

        const auto& target = board[ty][tx];

        if (target.empty()) {
            moves.insert({tx,ty});
        } 
        else if (target.at("side") == "river") {
            // for (auto& d : river_dests.at({tx,ty})) {
            //     moves.insert(d);
            // }
            auto flow = get_river_flow_destinations(board, tx, ty, sx, sy, player, rows, cols, score_cols);
            for (auto& d : flow) {
                moves.insert(d);
            }
        } 
        else {
            // Stone occupied
            if (p.at("side") == "stone") { // stone moved to stone
                int px = tx + dx, py = ty + dy;
                if (in_bounds(px,py,rows,cols) &&
                    board[py][px].empty() &&
                    !is_opponent_score_cell(px,py,target.at("owner"),rows,cols,score_cols)) {
                    pushes.push_back({{tx,ty}, {px,py}});
                }
            } else {
                string pushed_player = target.at("owner");
                auto flow = get_river_flow_destinations(board, tx, ty, sx, sy, pushed_player, rows, cols, score_cols, true);
                for (auto& d : flow) {
                    if (!is_opponent_score_cell(d.first, d.second, pushed_player, rows, cols, score_cols)) {
                        pushes.push_back({{tx,ty}, d});
                    }
                }
            }
        }
    }

    // Return moves as vector + pushes
    return {
        vector<pair<int,int>>(moves.begin(), moves.end()),
        pushes
    };
}

vector<Move> generate_all_moves(
    const Board& board,
    const string& player,
    int rows,
    int cols,
    const vector<int>& score_cols)
{
    vector<Move> moves;
    const vector<pair<int, int>> dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            // Use a reference to allow mutation, matching Python's behavior
            auto& p = board[y][x];
            if (p.empty() || p.at("owner") != player) {
                continue;
            }

            if (p.at("side") == "stone") {
                for (const auto& dir : dirs) {
                    int dx = dir.first;
                    int dy = dir.second;
                    int nx = x + dx;
                    int ny = y + dy;

                    if (!in_bounds(nx, ny, rows, cols) || is_opponent_score_cell(nx, ny, player, rows, cols, score_cols)) {
                        continue;
                    }

                    if (board[ny][nx].empty()) {
                        moves.push_back({"move", {x, y}, {nx, ny}});
                    } else {
                        const auto& target = board[ny][nx];
                        if (target.at("side") == "river") {
                            auto flow = get_river_flow_destinations(board, nx, ny, x, y, player, rows, cols, score_cols);
                            for (const auto& d : flow) {
                                moves.push_back({"move", {x, y}, {d.first, d.second}});
                            }
                        } else {
                            int px = nx + dx;
                            int py = ny + dy;
                            if (in_bounds(px, py, rows, cols) && board[py][px].empty() && !is_opponent_score_cell(px, py, target.at("owner"), rows, cols, score_cols)) {
                                moves.push_back({"push", {x, y}, {nx, ny}, {px, py}});
                            }
                        }
                    }
                }
                // flips
                for (const string& ori : {"horizontal", "vertical"}) {
                    // WARNING: This mutates the input board, preserving the original Python logic.
                    // p["side"] = "river";
                    // p["orientation"] = ori;
                    moves.push_back({"flip", {x, y}, {}, {}, ori});
                }
            } else { // Preserving the duplicated logic block from the Python code
                for (const auto& dir : dirs) {
                    int dx = dir.first;
                    int dy = dir.second;
                    int nx = x + dx;
                    int ny = y + dy;

                    if (!in_bounds(nx, ny, rows, cols) || is_opponent_score_cell(nx, ny, player, rows, cols, score_cols)) {
                        continue;
                    }

                    if (board[ny][nx].empty()) {
                        moves.push_back({"move", {x, y}, {nx, ny}});
                    } else {
                        const auto& target = board[ny][nx];
                        if (target.at("side") == "river") {
                            auto flow = get_river_flow_destinations(board, nx, ny, x, y, player, rows, cols, score_cols);
                            for (const auto& d : flow) {
                                moves.push_back({"move", {x, y}, {d.first, d.second}});
                            }
                        } else { // river on stone
                            string pushed_player = target.at("owner");
                            auto flow = get_river_flow_destinations(board, nx, ny, x, y, pushed_player, rows, cols, score_cols, true);
                            for (const auto& d : flow) {
                                if (!is_opponent_score_cell(d.first, d.second, pushed_player, rows, cols, score_cols)) {
                                    moves.push_back({"push", {x, y}, {nx, ny}, {d.first, d.second}});
                                }
                            }
                        }
                    }
                }
                // flip to stone side
                moves.push_back({"flip", {x, y}});
                // rotate
                // WARNING: This mutates the input board, preserving the original Python logic.
                // string new_ori = (p.at("orientation") == "horizontal") ? "vertical" : "horizontal";
                // p["orientation"] = new_ori;
                moves.push_back({"rotate", {x, y}});
            }
        }
    }
    return moves;
}

pair<bool,string> validate_and_apply_move(
    Board& board,
    set <pair<int,int>> &own_pieces,
    set<pair<int,int>> &opp_pieces,
    const Move& move,
    const string& player,
    int rows, int cols,
    const vector<int>& score_cols)
{
    const string& action = move.action;
    // ------------------ MOVE ------------------
    if (action == "move") {
        if (move.from.size() < 2 || move.to.size() < 2)
            return {false, "move needs from & to"};

        int fx = move.from[0], fy = move.from[1];
        int tx = move.to[0], ty = move.to[1];
        auto& piece = board[fy][fx];

        if (board[ty][tx].empty()) {
            board[ty][tx] = piece;
            board[fy][fx].clear();
            own_pieces.erase({fx,fy});
            own_pieces.insert({tx,ty});
            return {true, "moved"};
        }

        // int ptx = move.pushed_to[0], pty = move.pushed_to[1];
        // board[pty][ptx] = board[ty][tx];
        // if (board[ty][tx].at("owner") == player) {
        //     own_pieces.erase({fx,fy});
        //     own_pieces.insert({ptx,pty});
        // } else {
        //     opp_pieces.erase({tx,ty});
        //     opp_pieces.insert({ptx,pty});
        //     own_pieces.erase({fx,fy});
        //     own_pieces.insert({tx,ty});
        // }
        // board[ty][tx]  = piece;
        // board[fy][fx].clear();
        // return {true, "move+push applied"};
    }

    // ------------------ PUSH ------------------
    else if (action == "push") {
        if (move.from.size() < 2 || move.to.size() < 2 || move.pushed_to.size() < 2)
            return {false, "push needs from,to,pushed_to"};

        int fx = move.from[0], fy = move.from[1];
        int tx = move.to[0], ty = move.to[1];
        int px = move.pushed_to[0], py = move.pushed_to[1];

        string pushed_player = board[ty][tx].empty() ? "" : board[ty][tx].at("owner");

        auto& piece = board[fy][fx];

        // Apply push
        board[py][px] = board[ty][tx];   // enemy goes to pushed_to
        if (board[ty][tx].at("owner") == player) {
            own_pieces.erase({fx,fy});
            own_pieces.insert({px,py});
        } else {
            opp_pieces.erase({tx,ty});
            opp_pieces.insert({px,py});
            own_pieces.erase({fx,fy});
            own_pieces.insert({tx,ty});
        }
        board[ty][tx] = piece;           // mover goes into enemy's cell
        board[fy][fx].clear();           // origin cleared
        

        auto& mover = board[ty][tx];
        if (mover.at("side") == "river") {
            mover["side"] = "stone";
            mover["orientation"] = "";
        }

        return {true, "push applied"};
    }


    // ------------------ FLIP ------------------
    else if (action == "flip") {
        if (move.from.size()<2) return {false, "flip needs from"};
        int fx=move.from[0], fy=move.from[1];

        auto& piece = board[fy][fx];

        if (piece.at("side")=="stone") {
            string ori = move.orientation;

            piece["side"]="river";
            piece["orientation"]=ori;
            return {true, "flipped to river"};
        } else {
            piece["side"]="stone";
            piece["orientation"]="";
            return {true, "flipped to stone"};
        }
    }

    // ------------------ ROTATE ------------------
    else if (action == "rotate") {
        if (move.from.size()<2) return {false,"rotate needs from"};
        int fx=move.from[0], fy=move.from[1];

        auto& piece = board[fy][fx];

        // flip orientation
        piece["orientation"] = (piece["orientation"]=="vertical") ? "horizontal" : "vertical";
        return {true, "rotated"};
    }

    return {false,"unknown action"};
}

void reverse_move(Board& board, set <pair<int,int>> &own_pieces, set <pair<int,int>> &opp_pieces, const Move& move, const string& player, int rows, int cols, const vector<int>& score_cols, int from_river_orient) {
    const string& action = move.action;

    // ------------------ MOVE ------------------
    if (action == "move") {
        int fx = move.from[0], fy = move.from[1];
        int tx = move.to[0], ty = move.to[1];
        auto& piece = board[ty][tx];

        board[fy][fx] = piece;
        board[ty][tx].clear();
        own_pieces.erase({tx,ty});
        own_pieces.insert({fx,fy});
    }

    // ------------------ PUSH ------------------
    else if (action == "push") {
        int fx = move.from[0], fy = move.from[1]; // can be stone or river
        int tx = move.to[0], ty = move.to[1];
        int px = move.pushed_to[0], py = move.pushed_to[1];
        if (board[py][px].at("owner") == player) {
            own_pieces.erase({px,py});
            own_pieces.insert({fx,fy});
        } else {
            opp_pieces.erase({px,py});
            opp_pieces.insert({tx,ty});
            own_pieces.erase({tx,ty});
            own_pieces.insert({fx,fy});
        }
        if (from_river_orient == 1) {
            board[fy][fx] = board[ty][tx];
            board[fy][fx]["side"] = "river";
            board[fy][fx]["orientation"] = "vertical";
            board[ty][tx] = board[py][px];
            board[py][px].clear();
        } else if (from_river_orient == 2) {
            board[fy][fx] = board[ty][tx];
            board[fy][fx]["side"] = "river";
            board[fy][fx]["orientation"] = "horizontal";
            board[ty][tx] = board[py][px];
            board[py][px].clear();
        } else {
            board[fy][fx] = board[ty][tx];
            board[ty][tx] = board[py][px];
            board[py][px].clear();
        }
    }

    // ------------------ FLIP ------------------
    else if (action == "flip") {
        int fx=move.from[0], fy=move.from[1];
        auto& piece = board[fy][fx];

        if (from_river_orient == 1) {
            piece["side"] = "river";
            piece["orientation"] = "vertical";
        } else if (from_river_orient == 2) {
            piece["side"] = "river";
            piece["orientation"] = "horizontal";
        } else {
            piece["side"] = "stone";
            piece["orientation"] = "";
        }
    } 

    // ------------------ ROTATE ------------------
    else if (action == "rotate") {
        int fx=move.from[0], fy=move.from[1];
        auto& piece = board[fy][fx];
        piece["orientation"] = (piece["orientation"]=="vertical") ? "horizontal" : "vertical";
    }
}

pair<double, double> get_total_manhattan_dist (const Board& board, const set<pair<int,int>> &own_pieces, const vector<int>& score_cols, const string &side, int scoring_own){
    double total_dist = 0;
    int rows = board.size();
    int cols = board[0].size();
    double min_manhattan = rows + cols;
    int score_row = (side=="circle") ? 2 : rows - 3; 
    int cap = (score_cols.size() >= 5 ? 2 : 1);
    for(auto piece : own_pieces) {
        // if in scoring area then continue
        if (is_own_score_cell(piece.first, piece.second, side, rows, cols, score_cols)) { // NEW
            continue;
        }
        int x = piece.first;
        int y = piece.second;
        double min_dist = cols + rows;
        for (int sc : score_cols) {
            if (scoring_own >= score_cols.size() - cap)
            {if (!board[score_row][sc].empty()) {
                continue; // skip occupied scoring cells
            }}
            min_dist = min(min_dist, (double)abs(sc - x) + abs(score_row - y));
        }
        if (min_dist <= 2 && !board[y][x].empty() && board[y][x].at("side") == "river" && board[y][x].at("orientation") == "horizontal") { // NEW
            total_dist += 10; // prioritize stones that can score next turn
        }
        total_dist += 1 / (min_dist + 1);
        min_manhattan = min(min_manhattan, min_dist);
    }
    return {40 * total_dist, min_manhattan};
}


// double min_man_from_empty(const Board& board, const set<pair<int,int>> &own_pieces, const string &player, const vector<int>& score_cols) {
//     double min_manhattan = board.size() + board[0].size();
//     int rows = board.size();
//     int cols = board[0].size();
//     int score_row = own_scoring_row(player, rows);
//     for (auto &pc: own_pieces) {
//         int x = pc.first;
//         int y = pc.second;
//         if (is_own_score_cell(x, y, player, rows, cols, score_cols)) {
//             continue;
//         }
//         for (int sc : score_cols) {
//             if (board[score_row][sc].empty()) {
//                 min_manhattan = min(min_manhattan, (double)(abs(sc - x) + abs(score_row - y)));
//             }
//         }
//     }
//     return min_manhattan;
// }

pair<double, pair<int, int>> countScoringPieces(const Board& board, const string& player, int rows, int cols, const vector<int>& score_cols) {
    int scorerow = own_scoring_row(player, rows);
    int countstones = 0, countriver = 0;
    for (int col:score_cols) {
        if (!board[scorerow][col].empty() && board[scorerow][col].at("owner") == player) {
            if (board[scorerow][col].at("side") == "stone") countstones++;
            else countriver++;
        }
    } 
    if (countstones + countriver == score_cols.size()) {
        if (score_cols.size() == 4) { // victory eval > 20
            if (countstones == 4) return {40, {countstones, countriver}};
            else if (countstones == 3 && countriver == 1) return {15, {countstones, countriver}};
            else if (countstones == 2 && countriver == 2) return {10, {countstones, countriver}};
            else if (countstones == 1 && countriver == 3) return {7, {countstones, countriver}};
            else return {4, {countstones, countriver}};
        } else if (score_cols.size() == 5) { // victory eval > 30
            if (countstones == 5) return {60, {countstones, countriver}};
            else if (countstones == 4 && countriver == 1) return {20, {countstones, countriver}};
            else if (countstones == 3 && countriver == 2) return {15, {countstones, countriver}};
            else if (countstones == 2 && countriver == 3) return {10, {countstones, countriver}};
            else if (countstones == 1 && countriver == 4) return {7, {countstones, countriver}};
            else return {5, {countstones, countriver}};
        } else if (score_cols.size() == 6) { // victory eval > 40
            if (countstones == 6) return {70, {countstones, countriver}};
            else if (countstones == 5 && countriver == 1) return {25, {countstones, countriver}};
            else if (countstones == 4 && countriver == 2) return {20, {countstones, countriver}};
            else if (countstones == 3 && countriver == 3) return {15, {countstones, countriver}};
            else if (countstones == 2 && countriver == 4) return {10, {countstones, countriver}};
            else if (countstones == 1 && countriver == 5) return {8, {countstones, countriver}};
            else return {6, {countstones, countriver}};
        }
    }
    return {countstones + countriver, {countstones, countriver}};
}

int countBorderPieces(const Board& board, const string& player, int rows, int cols, const set<pair<int,int>> &own_pieces) {
    int count = 0;
    for (auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
        if (x == 0 || x == cols - 1 || (player == "circle" ? y == rows - 1 : y == 0)) {
            count++;
        }
    }
    return count;
}

int riversToGoalLite (const Board& board, const set<pair<int,int>> &own_pieces, const set<pair<int,int>> &opponent_pieces,
                     const string& player, int rows, int cols,
                     const vector<int>& score_cols){
    unordered_map <int, set<int>> x_coord_to_y;
    for (const auto& piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
        x_coord_to_y[x].insert(y);
    }
    for (const auto& piece : opponent_pieces) {
        int x = piece.first;
        int y = piece.second;
        x_coord_to_y[x].insert(y);
    }

    int score = 0;
    for (auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;

        if (board[y][x].empty()) continue;
        if (board[y][x].at("side") != "river") continue;
        if (is_own_score_cell(x, y, player, rows, cols, score_cols)) continue;

        int blocker_count = 0;
        if(board[y][x].at("orientation") == "horizontal") {
            if(y==own_scoring_row(player, rows)) {
                // check left
                bool can_reach = false;
                for(int i = x - 1; i >= score_cols.front(); i--) {
                    if(board[y][i].empty() && is_own_score_cell(i,y,player,rows,cols,score_cols)) {
                        can_reach = true;
                        break;      
                    }
                    if(!board[y][i].empty()) {
                        if(board[y][i].at("side") == "stone") {
                            blocker_count++;
                        } else if(board[y][i].at("side") == "river" && board[y][i].at("orientation") == "vertical") {
                            blocker_count++;
                        }
                    }
                }
                if (!can_reach) {
                    blocker_count = 0;
                    for(int i = x + 1; i <= score_cols.back(); i++) {
                        if(board[y][i].empty() && is_own_score_cell(i,y,player,rows,cols,score_cols)) {
                            can_reach = true;
                            break;
                        }
                        if(!board[y][i].empty()) {
                            if(board[y][i].at("side") == "stone") {
                                blocker_count++;
                            } else if(board[y][i].at("side") == "river" && board[y][i].at("orientation") == "vertical") {
                                blocker_count++;
                            }
                        }
                    }
                }
                if(!can_reach){
                    blocker_count = 3; // treat as fully blocked
                }
            }
        }
        else{
            if(x<= score_cols.back() + 1 && x >= score_cols.front() - 1){
                if(player=="circle" && y<own_scoring_row(player,rows)) {
                    
                    for(int i = y + 1; i < own_scoring_row(player,rows); i++) {
                        if(!board[i][x].empty()) {
                            if(board[i][x].at("side") == "stone") {
                                blocker_count++;
                            } else if(board[i][x].at("side") == "river" && board[i][x].at("orientation") == "horizontal") {
                                blocker_count++;
                            }
                        }
                    }
                }
                else if(player=="square" && y>own_scoring_row(player,rows)) {
                    // check up
                    bool can_reach = false;
                    for(int i = y - 1; i > own_scoring_row(player,rows); i--) {
                        if(!board[i][x].empty()) {
                            if(board[i][x].at("side") == "stone") {
                                blocker_count++;
                            } else if(board[i][x].at("side") == "river" && board[i][x].at("orientation") == "horizontal") {
                                blocker_count++;
                            }
                        }
                    }
                }
                else if (player=="circle" && y>own_scoring_row(player,rows)){
                    // iterate over all y between own scoring row and y using unordered map
                    auto it = x_coord_to_y[x].upper_bound(own_scoring_row(player,rows));
                    while(it != x_coord_to_y[x].end() && *it < y) {
                        int iy = *it;
                        if(!board[iy][x].empty()) {
                            if(board[iy][x].at("side") == "stone") {
                                blocker_count++;
                            } else if(board[iy][x].at("side") == "river" && board[iy][x].at("orientation") == "horizontal") {
                                blocker_count++;
                            }
                        }
                        ++it;
                    }
                }
                else if (player=="square" && y<own_scoring_row(player,rows)){
                    auto it = x_coord_to_y[x].lower_bound(own_scoring_row(player,rows));
                    while(it != x_coord_to_y[x].begin()) {
                        --it;
                        int iy = *it;
                        if(iy <= y) break;
                        if(!board[iy][x].empty()) {
                            if(board[iy][x].at("side") == "stone") {
                                blocker_count++;
                            } else if(board[iy][x].at("side") == "river" && board[iy][x].at("orientation") == "horizontal") {
                                blocker_count++;
                            }
                        }
                    }
                }
            }
            else{
                blocker_count = 3; // treat as fully blocked
            }
        }
        if(blocker_count==0) score +=4;
        else if(blocker_count==1) score +=2;
        else if(blocker_count==2) score +=1;
    }
    return score;
}


int count_reachable_in_one(
    const Board& board,
    const set<pair<int,int>> &own_pieces,
    const string& player,
    int rows, int cols,
    const vector<int>& score_cols) {
    int count = 0;
    for (auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
        if(is_own_score_cell(x,y,player,rows,cols,score_cols)) continue;
        vector<pair<int,int>> dirs = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        bool reachable = false;
        for (auto [dx,dy] : dirs) {
            int tx = x + dx, ty = y + dy;
            if(!in_bounds(tx,ty,rows,cols)) continue;
            if (board[ty][tx].empty() && is_own_score_cell(tx,ty,player,rows,cols,score_cols)) {
                reachable = true;
                break;
            } 
        }
        if(reachable){
            count++;
            continue;
        }
        if (board[y][x].at("side")=="stone") {
            for(auto[dx,dy] : dirs){
                int tx = x + dx, ty = y + dy;
                if(!in_bounds(tx,ty,rows,cols)) continue;
                if(is_own_score_cell(tx,ty, opponent(player),rows,cols,score_cols)) continue;
                if(!board[ty][tx].empty() && board[ty][tx].at("side")=="stone"){
                    int px = tx + dx, py = ty + dy;
                    if(in_bounds(px,py,rows,cols) && board[py][px].empty() && is_own_score_cell(px,py,player,rows,cols,score_cols) && is_own_score_cell(tx,ty,player,rows,cols,score_cols)){
                        reachable = true;
                        break;
                    }
                }
                if(!board[ty][tx].empty() && board[ty][tx].at("side")=="river"){

                    // check along direction of river if there is a path to score cell
                    string orientation = board[ty][tx].at("orientation");
                    if(orientation=="horizontal"){
                        if(ty==own_scoring_row(player,rows)){
                            for(int i = tx+1; i<=score_cols.back(); i++){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                            if(reachable) break;
                            for(int i = tx-1; i>=score_cols.front(); i--){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                        }
                    }
                    else{
                        if(tx<= score_cols.back() && tx >= score_cols.front() && board[own_scoring_row(player,rows)][tx].empty()){
                            for(int i = ty+1; i<=own_scoring_row(player,rows); i++){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(tx==x && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                            if(reachable) break;
                            for(int i = ty-1; i>=own_scoring_row(player,rows); i--){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(tx==x && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                        }   
                    }
                }

            }
        }
        if(reachable){
            count++;
            continue;
        }
        if(board[y][x].at("side")=="river"){
            for(auto[dx,dy] : dirs){
                int tx = x + dx, ty = y + dy;
                if(!in_bounds(tx,ty,rows,cols)) continue;
                if(is_own_score_cell(tx,ty,opponent(player),rows,cols,score_cols)) continue;
                if(!board[ty][tx].empty() && board[ty][tx].at("side")=="stone"){
                    if(board[y][x].at("orientation")=="horizontal"){
                        if(ty==own_scoring_row(player,rows)){
                            for(int i = tx+1; i<=score_cols.back(); i++){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                            if(reachable) break;
                            for(int i = tx-1; i>=score_cols.front(); i--){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                        }
                    }
                    else{
                        if(tx<= score_cols.back() && tx >= score_cols.front() && board[own_scoring_row(player,rows)][tx].empty()){
                            for(int i = ty+1; i<=own_scoring_row(player,rows); i++){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(x==tx && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                            if(reachable) break;
                            for(int i = ty-1; i>=own_scoring_row(player,rows); i--){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(x==tx && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                        }
                    }
                }
                if(reachable){
                    break;
                }
                if(!board[ty][tx].empty() && board[ty][tx].at("side")=="river"){
                    string orientation = board[ty][tx].at("orientation");
                    if(orientation=="horizontal"){
                        if(ty==own_scoring_row(player,rows)){
                            for(int i = tx+1; i<=score_cols.back(); i++){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                            if(reachable) break;
                            for(int i = tx-1; i>=score_cols.front(); i--){
                                if(board[ty][i].empty() && is_own_score_cell(i,ty,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(board[ty][i].empty()) continue;
                                if(ty==y && i==x) continue;
                                if(board[ty][i].at("side")=="stone") break;
                                if(board[ty][i].at("side")=="river" && board[ty][i].at("orientation")=="vertical") break;
                            }
                        }
                    }
                    else{
                        if(tx<= score_cols.back() && tx >= score_cols.front() && board[own_scoring_row(player,rows)][tx].empty()){
                            for(int i = ty+1; i<=own_scoring_row(player,rows); i++){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(x==tx && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                            if(reachable) break;
                            for(int i = ty-1; i>=own_scoring_row(player,rows); i--){
                                if(board[i][tx].empty() && is_own_score_cell(tx,i,player,rows,cols,score_cols)){
                                    reachable = true;
                                    break;
                                }
                                if(i==own_scoring_row(opponent(player),rows)) break;
                                if(board[i][tx].empty()) continue;
                                if(x==tx && i==y) continue;
                                if(board[i][tx].at("side")=="stone") break;
                                if(board[i][tx].at("side")=="river" && board[i][tx].at("orientation")=="horizontal") break;
                            }
                        }
                    }
                }
                if(reachable){
                    break;
                }
            }
        }
        if(reachable){
            count++;
            continue;
        }
    }
    return count;
}

// ---- Student Agent ----
class StudentAgent {
public:
    
    explicit StudentAgent(string side) : side(move(side)), gen(rd()) {} // side is own side

    vector<double> weights = {15, -15, 1000, -1000, 1,-1, -60, 60, -40, 40};
    double evaluateBoard(const Board& board,const set<pair<int,int>> &own_pieces, const set<pair<int,int>> &opp_pieces, int rows, int cols, const vector<int>& score_cols) {
        vector<double> features;
        double score = 0;
        if (score_cols.size() == 4 || score_cols.size() == 6) {
            features.push_back(0);
            features.push_back(0);
        } else {
            features.push_back(riversToGoalLite(board, own_pieces, opp_pieces, side, rows, cols, score_cols));
            features.push_back(riversToGoalLite(board, own_pieces, opp_pieces, opponent(side), rows, cols, score_cols));
        }

        auto [score1, cnt1] = countScoringPieces(board, side, rows, cols, score_cols);
        auto [score2, cnt2] = countScoringPieces(board, opponent(side), rows, cols, score_cols);
        auto [scoringstones1, scoringrivers1] = cnt1;
        auto [scoringstones2, scoringrivers2] = cnt2;
        double multiplier = 1.0;
        if (scoringrivers1 + scoringstones1 <= scoringstones2 + scoringrivers2 - 2) {
            multiplier = 1.2;
            features.push_back(scoringstones1 * multiplier + scoringrivers1);
        }
        else {  
            features.push_back(score1);
        }
        features.push_back(score2);
        auto [totalman1, minman1] = get_total_manhattan_dist(board, own_pieces, score_cols, side, scoringstones1 + scoringrivers1);
        auto [totalman2, minman2] = get_total_manhattan_dist(board, opp_pieces, score_cols, opponent(side), scoringstones2 + scoringrivers2);
        // features.push_back(m1);
        // features.push_back(m2);
        features.push_back(totalman1);
        features.push_back(totalman2);
        // features.push_back(total1);
        // features.push_back(total2);

        features.push_back(minman1);
        features.push_back(minman2);
        int borderpiecesown = countBorderPieces(board, side, rows, cols, own_pieces);
        int borderpiecesopp = countBorderPieces(board, opponent(side), rows, cols, opp_pieces);
        features.push_back(borderpiecesown);
        features.push_back(borderpiecesopp);
        // features.push_back(min_man_from_empty(board, own_pieces, side, score_cols));
        // features.push_back(min_man_from_empty(board, opp_pieces, opponent(side), score_cols));
        // features.push_back(0);
        // features.push_back(0);
        // features.push_back(dist_to_closest_river(board, side, score_cols));
        // features.push_back(dist_to_closest_river(board, opponent(side), score_cols));
        for (size_t i = 0; i < features.size(); i++) {
            score += weights[i] * features[i];
        }
        return score;
    }

    bool should_draw (vector<Move> & plies, Board& board, set<pair<int,int>> &own_pieces, set<pair<int,int>> &opp_pieces, const string& player, int rows, int cols, const vector<int>& score_cols) {
        if (cutoffdepth >= 3) {
            int from_river_orient0 = 0;
            if (!board[plies[0].from[1]][plies[0].from[0]].empty())
            if (board[plies[0].from[1]][plies[0].from[0]].at("side") == "river") {
                if (board[plies[0].from[1]][plies[0].from[0]].at("orientation") == "vertical") {
                    from_river_orient0 = 1; // vertical orientation
                } else {
                    from_river_orient0 = 2;
                }
            }
            int from_river_orient1 = 0;
            if (!board[plies[1].from[1]][plies[1].from[0]].empty())
            if (board[plies[1].from[1]][plies[1].from[0]].at("side") == "river") {
                if (board[plies[1].from[1]][plies[1].from[0]].at("orientation") == "vertical") {
                    from_river_orient1 = 1; // vertical orientation
                } else {
                    from_river_orient1 = 2;
                }
            }
            
            validate_and_apply_move(board, own_pieces, opp_pieces, plies[0], player, rows, cols, score_cols);
            validate_and_apply_move(board, opp_pieces, own_pieces, plies[1], opponent(player), rows, cols, score_cols);
            auto last_info = maxValue(board, rows, cols, score_cols, 2, -1e9, 1e9);
            auto last_move = last_info.first.second;
            int from_river_orient2 = 0;
            if (!board[last_move.from[1]][last_move.from[0]].empty())
            if (board[last_move.from[1]][last_move.from[0]].at("side") == "river") {
                if (board[last_move.from[1]][last_move.from[0]].at("orientation") == "vertical") {
                    from_river_orient2 = 1; // vertical orientation
                } else {
                    from_river_orient2 = 2;
                }
            }
            validate_and_apply_move(board, own_pieces, opp_pieces, last_move, player, rows, cols, score_cols);
            int my_scoring_pieces  = 0;
            int my_river_pieces = 0;
            int opp_scoring_pieces = 0;
            int opp_river_pieces = 0;
            for (int col : score_cols) {
                if (!board[own_scoring_row(player, rows)][col].empty()) {
                    if (board[own_scoring_row(player, rows)][col].at("side") == "stone") my_scoring_pieces++;
                    else my_river_pieces++;
                }
                if (!board[own_scoring_row(opponent(player), rows)][col].empty()) {
                    if (board[own_scoring_row(opponent(player), rows)][col].at("side") == "stone") opp_scoring_pieces++;
                    else opp_river_pieces++;
                }
            }
            my_scoring_pieces += my_river_pieces;
            opp_scoring_pieces += opp_river_pieces;
            int delta_pieces = my_scoring_pieces - opp_scoring_pieces;
            delta_pieces = -delta_pieces;
            int delta_rivers = my_river_pieces - opp_river_pieces;
            double curr_eval = evaluateBoard(board, own_pieces, opp_pieces, rows, cols, score_cols);
            reverse_move(board, own_pieces, opp_pieces, last_move, player, rows, cols, score_cols, from_river_orient2);
            reverse_move(board, opp_pieces, own_pieces, plies[1], opponent(player), rows, cols, score_cols, from_river_orient1);
            reverse_move(board, own_pieces, opp_pieces, plies[0], player, rows, cols, score_cols, from_river_orient0);
            if(delta_pieces > 2) return true;
            else if (delta_pieces == 2) {
                return (delta_rivers >=0);
            } else if (delta_pieces == 1) {
                if (opp_scoring_pieces==score_cols.size()-1) {
                    return true;
                } else {
                    return (delta_rivers >=2);
                }
            } else {
                if (opp_scoring_pieces <= score_cols.size()-2) {
                    return false;
                } else {
                    if(delta_rivers >=1) return true;
                    return curr_eval < 0;
                }
            }
        } else {
            int my_scoring_pieces  = 0;
            int my_river_pieces = 0;
            int opp_scoring_pieces = 0;
            int opp_river_pieces = 0;
            for (int col : score_cols) {
                if (!board[own_scoring_row(player, rows)][col].empty()) {
                    if (board[own_scoring_row(player, rows)][col].at("side") == "stone") my_scoring_pieces++;
                    else my_river_pieces++;
                }
                if (!board[own_scoring_row(opponent(player), rows)][col].empty()) {
                    if (board[own_scoring_row(opponent(player), rows)][col].at("side") == "stone") opp_scoring_pieces++;
                    else opp_river_pieces++;
                }
            }
            my_scoring_pieces += my_river_pieces;
            opp_scoring_pieces += opp_river_pieces;
            int delta_pieces = my_scoring_pieces - opp_scoring_pieces;
            delta_pieces = -delta_pieces;
            int delta_rivers = my_river_pieces - opp_river_pieces;
            double curr_eval = evaluateBoard(board, own_pieces, opp_pieces, rows, cols, score_cols);
            if(delta_pieces > 2) return true;
            else if (delta_pieces == 2) {
                return (delta_rivers >=0);
            } else if (delta_pieces == 1) {
                if (opp_scoring_pieces==score_cols.size()-1) {
                    return true;
                } else {
                    return (delta_rivers >=2);
                }
            } else {
                if (opp_scoring_pieces <= score_cols.size()-2) {
                    return false;
                } else {
                    if(delta_rivers >=1) return true;
                    return curr_eval < 0;
                }
            }
        }
    }

    // simulate on a copy and return the zobrist hash of the resulting board
    uint64_t simulate_move_hash(
        const Board &board,
        const set<pair<int,int>> own,
        const set<pair<int,int>> opp,
        const Move &mv,
        const string &side,
        int rows, int cols,
        const vector<int> &score_cols)
    {
        Board tmp = board;               // deep copy of board
        auto tmp_own = own;              // copy sets
        auto tmp_opp = opp;

        // apply move to tmp (validate_and_apply_move mutates the board)
        validate_and_apply_move(tmp, tmp_own, tmp_opp, mv, side, rows, cols, score_cols);
        return computeZobristHash(tmp);
    }

    // void compare_boards(const Board& a, const Board& b) {
    //     bool same = true;
    //     for (int y = 0; y < a.size(); y++) {
    //         for (int x = 0; x < a[y].size(); x++) {
    //             if (a[y][x].empty() != b[y][x].empty()
    //                 || (!a[y][x].empty() &&
    //                     (a[y][x].at("owner") != b[y][x].at("owner") ||
    //                     a[y][x].at("side") != b[y][x].at("side") ||
    //                     a[y][x].at("orientation") != b[y][x].at("orientation")))) {
    //                 cout << "DIFF at ("<<x<<","<<y<<")\n";
    //                 cout<< a[y][x].at("owner") << " " << b[y][x].at("owner") << endl;
    //                 cout<< a[y][x].at("side") << " " << b[y][x].at("side") << endl;
    //                 cout<< a[y][x].at("orientation") << " " << b[y][x].at("orientation") << endl;
    //                 same = false;
    //             }
    //         }
    //     }
    //     if (same) cout << "BOARDS IDENTICAL\n";
    // }

    // void print_board(const Board& board) {
    //     for (int y = 0; y < board.size(); y++) {
    //         for (int x = 0; x < board[y].size(); x++) {
    //             if (board[y][x].empty()) {
    //                 cout << ". ";
    //             } else {
    //                 if (board[y][x].at("owner") == side) {
    //                     if (board[y][x].at("side") == "stone") {
    //                         cout << "S ";
    //                     } else {
    //                         if (board[y][x].at("orientation") == "horizontal") {
    //                             cout << "RH ";
    //                         } else if (board[y][x].at("orientation") == "vertical") {
    //                             cout << "RV ";
    //                         } else {
    //                             cout << "?u";
    //                         }
    //                     }
    //                 } else {
    //                     if (board[y][x].at("side") == "stone") {
    //                         cout << "s ";
    //                     } else {
    //                         if (board[y][x].at("orientation") == "horizontal") {
    //                             cout << "rh ";
    //                         } else if (board[y][x].at("orientation") == "vertical") {
    //                             cout << "rv ";
    //                         } else {
    //                             cout << "?o";
    //                         }
    //                     }
    //                 }
    //             }
    //         }
    //         cout << endl;
    //     }
    // }

    Move choose(const Board& board, int row, int col, const vector<int>& score_cols, float current_player_time, float opponent_time) {
    // adjust depth as before
        if (row == 13) {
            if (current_player_time < 13) {
                cutoffdepth = 2;
                if (current_player_time < 8) {
                    cutoffdepth = 1;
                }
            }
        } else if (row == 15) {
            if (current_player_time < 25) {
                cutoffdepth = 2;
                if (current_player_time < 13) {
                    cutoffdepth = 1;
                }
            } 
        } else if (row == 17) {
            if (current_player_time < 45) {
                cutoffdepth = 2;
                if (current_player_time < 15) cutoffdepth = 1;
            }
        }
        Board curboard = board;
        own_pieces.clear();
        opp_pieces.clear();
        for (int y = 0; y < row; y++) {
            for (int x = 0; x < col; x++) {
                if (!curboard[y][x].empty()) {
                    if (curboard[y][x].at("owner") == side) {
                        own_pieces.insert({x, y});
                    } else {
                        opp_pieces.insert({x, y});
                    }
                }
            }
        }

        moves += 1;

        bool same_as_last = (curboard == last_board);
        Move chosen_move;
        if(same_as_last) {
            chosen_move = last_move;
            if(zob_history.size() >= 4) {
                int from_river_orient = 0;
                if (!curboard[chosen_move.from[1]][chosen_move.from[0]].empty())
                if (curboard[chosen_move.from[1]][chosen_move.from[0]].at("side") == "river") {
                    if (curboard[chosen_move.from[1]][chosen_move.from[0]].at("orientation") == "vertical") {
                        from_river_orient = 1; // vertical orientation
                    } else {
                        from_river_orient = 2;
                    }
                }
                auto res_hash = simulate_move_hash(board, own_pieces, opp_pieces, chosen_move, side, row, col, score_cols);
                if(res_hash==zob_history[zob_history.size()-2] && res_hash==zob_history[zob_history.size()-4]){
                    Board board_copy = curboard;
                    if(!should_draw(plies_for_second_best, board_copy, own_pieces, opp_pieces, side, row, col, score_cols)) {
                        auto minimax = maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9);
                        auto second_best_move = minimax.second.second;
                        chosen_move = second_best_move;
                    }
                }
            }
        }
        else{
            auto minimax = maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9);
            auto best_move = minimax.first.second;
            auto second_best_move = minimax.second.second;
            chosen_move = best_move;
            if(zob_history.size() >= 4) {
                auto res_hash_on_best = simulate_move_hash(board, own_pieces, opp_pieces, best_move, side, row, col, score_cols);
                auto res_hash_on_second_best = simulate_move_hash(board, own_pieces, opp_pieces, second_best_move, side, row, col, score_cols);
                if(res_hash_on_best==zob_history[zob_history.size()-2] && res_hash_on_best==zob_history[zob_history.size()-4]){
                    Board board_copy = curboard;
                    if(!should_draw(plies_for_second_best, board_copy, own_pieces, opp_pieces, side, row, col, score_cols)) {
                        chosen_move = second_best_move;
                    }
                }
            }
        }

        last_board = curboard;
        auto res_hash = simulate_move_hash(curboard, own_pieces, opp_pieces, chosen_move, side, row, col, score_cols);
        zob_history.push_back(res_hash);
        // reverse_move(curboard, own_pieces, opp_pieces, chosen_move, side, row, col, score_cols, 0);

        return last_move = chosen_move;
    }

    pair<pair<double, Move>, pair<double, Move>> maxValue(
        Board& board,
        int row, int col,
        const vector<int>& score_cols,
        int depth, double alpha, double beta
    ) {
        if (depth == cutoffdepth) {
            // At leaf node → return evaluation only, dummy moves
            double eval = evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols);
            return {{eval, Move()}, {eval, Move()}};
        }
        double max_val = -1e9;
        double second_max_val = -1e9;
        auto start_time = chrono::high_resolution_clock::now();
        Move best_move;
        Move second_best_move;
        vector<pair<double, Move>> visit_order;
        // Generate all moves for this player
        int count = 0;
        for (const auto& move : generate_all_moves(board, side, row, col, score_cols)) {
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty())
            if (board[move.from[1]][move.from[0]].at("side") == "river") {
                if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
                    from_river_orient = 1; // vertical orientation
                } else {
                    from_river_orient = 2;
                }
            }
            validate_and_apply_move(board, own_pieces, opp_pieces, move, (side), row, col, score_cols);
            visit_order.push_back({evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), move});
            reverse_move(board, own_pieces, opp_pieces, move, (side), row, col, score_cols, from_river_orient);
        }
        if (depth != cutoffdepth - 1)
        sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        for (auto [value, move]: visit_order) {
            if (depth == 0) {
                auto end_time = chrono::high_resolution_clock::now();
                chrono::duration<double> elapsed = end_time - start_time;
                if (elapsed.count() > 8) {
                    break; // Time cutoff at root
                }
            }
            // if (count > 200) break;
            count++;
            moves++;
            int victory_eval = 0;
            if (score_cols.size() == 4) victory_eval = 20000;
            else if (score_cols.size() == 5) victory_eval = 30000;
            else if (score_cols.size() == 6) victory_eval = 40000;
            if (value > victory_eval) { // Winning move found
                return {{value, move}, {value, move}};
            }
            // Remember the orientation of river if needed (for reversal)
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty()) {
                if (board[move.from[1]][move.from[0]].at("side") == "river") {
                    from_river_orient = (board[move.from[1]][move.from[0]].at("orientation") == "vertical") ? 1 : 2;
                }
            }

            // Apply move
            validate_and_apply_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols);

            // Recursive evaluation
            auto [val, next_move] = minValue(board, row, col, score_cols, depth + 1, alpha, beta);

            // Undo move
            reverse_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols, from_river_orient);
            moves--;

            // Alpha-beta update
            if (val > max_val) {
                // Shift current best to second best
                second_max_val = max_val;
                second_best_move = best_move;
                if(depth==0){
                    plies_for_second_best = plies_for_best;
                }

                // Update best
                max_val = val;
                best_move = move;
                if(depth==0){
                    plies_for_best.clear();
                    plies_for_best.push_back(move);
                    plies_for_best.push_back(next_move);
                }
            } else if (val > second_max_val) {
                // Update second best only
                second_max_val = val;
                second_best_move = move;
                if(depth==0){
                    plies_for_second_best.clear();
                    plies_for_second_best.push_back(move);
                    plies_for_second_best.push_back(next_move);
                }
            }

            alpha = max(alpha, max_val);
            if (beta <= alpha) {
                pruned++;
                break;  // prune
            }
        }
        return {{max_val, best_move}, {second_max_val, second_best_move}};
    }


    pair<double, Move> minValue(Board& board, int row, int col, const vector<int>& score_cols, int depth, double alpha, double beta) {
        
        if (depth == cutoffdepth) {
            return {evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), Move()}; // return dummy move
        }
        double min_val = 1e9;
        Move worst_move;
        vector<pair<double, Move>> visit_order;
        for (const auto& move : generate_all_moves(board, opponent(side), row, col, score_cols)) {
            
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty())
            if (board[move.from[1]][move.from[0]].at("side") == "river") {
                if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
                    from_river_orient = 1; // vertical orientation
                } else {
                    from_river_orient = 2;
                }
            }
            validate_and_apply_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols);
            visit_order.push_back({evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), move});
            reverse_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols, from_river_orient);
        }
        if (depth != cutoffdepth - 1)
        sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        for (auto [value, move]: visit_order) {
            moves++;
            int victory_eval = 0;
            if (score_cols.size() == 4) victory_eval = -20000;
            else if (score_cols.size() == 5) victory_eval = -30000;
            else if (score_cols.size() == 6) victory_eval = -40000;
            if (value < victory_eval) { // Losing move found
                return {value, move};
            }
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty())
            if (board[move.from[1]][move.from[0]].at("side") == "river") {
                if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
                    from_river_orient = 1; // vertical orientation
                } else {
                    from_river_orient = 2;
                }
            }
            validate_and_apply_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols);
            double val = maxValue(board, row, col, score_cols, depth + 1, alpha, beta).first.first;
            reverse_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols, from_river_orient);
            moves--;
            beta = min(beta, val);
            if (val < min_val) {
                min_val = val;
                worst_move = move;
            }
            if (beta <= alpha) {
                pruned++;
                break;
            }
        }
        return {min_val, worst_move};
    }

private:
    string side;
    float m_current_player_time;
    float m_opponent_time;
    int moves = 0;
    int cutoffdepth = 3;
    set <pair<int,int>> own_pieces, opp_pieces;
    random_device rd;
    Board last_board;
    Move last_move;
    mt19937 gen;
    int pruned = 0;
    vector<uint64_t> zob_history;
    vector<Move> plies_for_second_best;
    vector<Move> plies_for_best;
};

// ---- PyBind11 bindings ----
PYBIND11_MODULE(student_agent_module, m) {
    py::class_<Move>(m, "Move")
        .def_readonly("action", &Move::action)
        .def_readonly("from_pos", &Move::from)
        .def_readonly("to_pos", &Move::to)
        .def_readonly("pushed_to", &Move::pushed_to)
        .def_readonly("orientation", &Move::orientation);

    py::class_<StudentAgent>(m, "StudentAgent")
        .def(py::init<string>())
        .def("choose", &StudentAgent::choose);
}