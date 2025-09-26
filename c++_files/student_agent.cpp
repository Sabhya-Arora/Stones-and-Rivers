#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <deque>
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

// Cell& find_cell (int x, int y, const map <pair<int,int>,Cell> &own_pieces, const map <pair<int,int>,Cell> &opponent_pieces) {
//     auto it = own_pieces.find({x,y});
//     if (it != own_pieces.end()) return it->second;
//     it = opponent_pieces.find({x,y});
//     if (it != opponent_pieces.end()) return it->second;
//     return empty_cell;
// }


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
            auto flow = get_river_flow_destinations(board, tx, ty, sx, sy, player, rows, cols, score_cols);
            for (auto& d : flow) {
                moves.insert(d);
            }
        } 
        else {
            // Stone occupied
            if (p.at("side") == "stone") {
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

        if (!in_bounds(fx,fy,rows,cols) || !in_bounds(tx,ty,rows,cols))
            return {false, "oob"};

        if (is_opponent_score_cell(tx,ty,player,rows,cols,score_cols))
            return {false, "can't go into opponent score"};

        auto& piece = board[fy][fx];
        if (piece.empty() || piece.at("owner") != player)
            return {false, "invalid piece"};

        if (board[ty][tx].empty()) {
            board[ty][tx] = piece;
            board[fy][fx].clear();
            return {true, "moved"};
        }

        if (move.pushed_to.size() < 2)
            return {false, "destination occupied; pushed_to required"};

        int ptx = move.pushed_to[0], pty = move.pushed_to[1];
        board[pty][ptx] = board[ty][tx];
        board[ty][tx]  = piece;
        board[fy][fx].clear();
        return {true, "move+push applied"};
    }

    // ------------------ PUSH ------------------
    else if (action == "push") {
        if (move.from.size() < 2 || move.to.size() < 2 || move.pushed_to.size() < 2)
            return {false, "push needs from,to,pushed_to"};

        int fx = move.from[0], fy = move.from[1];
        int tx = move.to[0], ty = move.to[1];
        int px = move.pushed_to[0], py = move.pushed_to[1];

        if (!(in_bounds(fx,fy,rows,cols) && in_bounds(tx,ty,rows,cols) && in_bounds(px,py,rows,cols)))
            return {false, "oob"};

        string pushed_player = board[ty][tx].empty() ? "" : board[ty][tx].at("owner");
        if (is_opponent_score_cell(tx,ty,player,rows,cols,score_cols) ||
            (!pushed_player.empty() && is_opponent_score_cell(px,py,pushed_player,rows,cols,score_cols)))
            return {false, "push would enter opponent score cell"};

        auto& piece = board[fy][fx];
        if (piece.empty() || piece.at("owner") != player)
            return {false, "invalid piece"};

        if (board[ty][tx].empty())
            return {false, "to must be occupied"};
        if (!board[py][px].empty())
            return {false, "pushed_to not empty"};

        if (piece.at("side") == "river" && board[ty][tx].at("side") == "river")
            return {false, "rivers cannot push rivers"};

        auto [moves, pushes] = compute_valid_targets(board, fx, fy, player, rows, cols, score_cols);
        bool valid_pair = false;
        for (auto& push_pair : pushes) {
            auto [of, pf] = push_pair;
            if (of.first == tx && of.second == ty &&
                pf.first == px && pf.second == py) {
                valid_pair = true;
                break;
            }
        }
        if (!valid_pair) {
            return {false, "push pair invalid"};
        }

        // Apply push
        board[py][px] = board[ty][tx];   // enemy goes to pushed_to
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
        if (!in_bounds(fx,fy,rows,cols)) return {false, "oob"};

        auto& piece = board[fy][fx];
        if (piece.empty() || piece.at("owner")!=player)
            return {false, "invalid piece"};

        if (piece.at("side")=="stone") {
            string ori = move.orientation;
            if (ori!="horizontal" && ori!="vertical")
                return {false, "stone->river needs orientation"};

            // temporarily flip to test safety
            piece["side"]="river";
            piece["orientation"]=ori;
            auto flow = get_river_flow_destinations(board, fx, fy, fx, fy, player, rows, cols, score_cols);
            piece["side"]="stone"; piece["orientation"]="";

            for (auto [dx,dy]:flow) {
                if (is_opponent_score_cell(dx,dy,player,rows,cols,score_cols))
                    return {false, "flip would allow flow into opponent score"};
            }

            // commit flip
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
        if (!in_bounds(fx,fy,rows,cols)) return {false,"oob"};

        auto& piece = board[fy][fx];
        if (piece.empty() || piece.at("owner")!=player)
            return {false, "invalid"};
        if (piece.at("side")!="river")
            return {false, "rotate only on river"};

        // flip orientation
        piece["orientation"] = (piece["orientation"]=="vertical") ? "horizontal" : "vertical";

        auto flow = get_river_flow_destinations(board, fx, fy, fx, fy, player, rows, cols, score_cols);
        for (auto [dx,dy]:flow) {
            if (is_opponent_score_cell(dx,dy,player,rows,cols,score_cols)) {
                // revert orientation
                piece["orientation"] = (piece["orientation"]=="vertical") ? "horizontal" : "vertical";
                return {false, "rotation allows flow into opponent score"};
            }
        }
        return {true, "rotated"};
    }

    return {false,"unknown action"};
}

void reverse_move(Board& board, const Move& move, const string& player, int rows, int cols, const vector<int>& score_cols, int from_river_orient) {
    const string& action = move.action;

    // ------------------ MOVE ------------------
    if (action == "move") {
        int fx = move.from[0], fy = move.from[1];
        int tx = move.to[0], ty = move.to[1];
        auto& piece = board[ty][tx];

        board[fy][fx] = piece;
        board[ty][tx].clear();
    }

    // ------------------ PUSH ------------------
    else if (action == "push") {
        int fx = move.from[0], fy = move.from[1]; // can be stone or river
        int tx = move.to[0], ty = move.to[1];
        int px = move.pushed_to[0], py = move.pushed_to[1];
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

int get_total_manhattan_dist (const Board& board, const vector<int>& score_cols, const string &side){
    int total_dist = 0;
    int min_manhattan = rows + cols;
    int rows = board.size();
    int cols = board[0].size();
    int score_row = (side=="circle") ? 2 : rows - 3; 
    for(auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
        const auto& cell = piece.second;
        int min_dist = cols + rows;
        for (int sc : score_cols) {
            min_dist = min(min_dist, abs(sc - x) + abs(score_row - y));
        }
        total_dist += min_dist;
        min_manhattan = min(min_manhattan, min_dist);
    }
    return {total_dist, min_manhattan};
}



pair<int,int> count_reachable_in_one(
const Board& board,
const set <pair<int,int>> &own_pieces,
const string& player,
int rows, int cols,
const vector<int>& score_cols)
{
    int m = 0;
    int count = 0;
    for(auto piece : own_pieces){
        int x = piece.first;
        int y = piece.second;
        const auto& p = board[y][x];
        // already in own scoring area → skip
        if (is_own_score_cell(x, y, player, rows, cols, score_cols)) {
            continue;
        }
        // compute valid targets
        auto [moves, pushes] = compute_valid_targets(board, x, y, player, rows, cols, score_cols);
        if(p.at("side")=="stone") count+=moves.size();
        // check moves
        bool found = false;
        for (auto& [tx, ty] : moves) {
            if (is_own_score_cell(tx, ty, player, rows, cols, score_cols)) {
                m++;
                found = true;
                break;
            }
        }
        // if no direct moves, check pushes
        if (p.at("side")=="river" &&  (!found)) {
            for (auto& push : pushes) {
                auto [ptx, pty] = push.second;
                if (is_own_score_cell(ptx, pty, player, rows, cols, score_cols)) {
                    m++;
                    break;
                }
            }
        }
    }

    return {m, count};
}


int riversToGoal(const Board& board,const set <pair<int,int>> &own_pieces, const string& player, int rows, int cols, const vector<int>& score_cols) {
    int count = 0;
    for(auto piece : own_pieces) {
        int x = piece.first.first;
        int y = piece.first.second;
        if (board[y][x].empty()) continue;
        if (board[y][x].at("side") == "river") {
            auto flow = get_river_flow_destinations(board, x, y, -1, -1, player, rows, cols, score_cols);
            for (auto [dx,dy]:flow) {
                if (is_own_score_cell(dx,dy,player,rows,cols,score_cols)) {
                    count++;
                    break;
                }
            }
        }
    }
    return count;
}


int countScoringPieces(const Board& board, const string& player, int rows, int cols, const vector<int>& score_cols) {
    int scorerow = (player == "circle" ? 2 : rows - 3);
    int count = 0;
    for (int col:score_cols) {
        if (!board[scorerow][col].empty() && board[scorerow][col].at("owner") == player && board[scorerow][col].at("side") == "stone") {
            count++;
        }
    } 
    return count;
}


// ---- Student Agent ----
class StudentAgent {
public:
    
    explicit StudentAgent(string side) : side(move(side)), gen(rd()) {} // side is own side

    vector<double> weights = {1000, -1000, 100, -100, -1, 1, 30, -30, 2, -2, -1, 1};
    double evaluateBoard(const Board& board, int rows, int cols, const vector<int>& score_cols) {
        vector<double> weights = {100, -0, 0, 0.0, 0.0};
        double score = 0;
        auto [m1, total1] = count_reachable_in_one(board, side, rows, cols, score_cols);
        auto [m2, total2] = count_reachable_in_one(board, opponent(side), rows, cols, score_cols);
        features.push_back(countScoringPieces(board, side, rows, cols, score_cols));
        features.push_back(countScoringPieces(board, opponent(side), rows, cols, score_cols));
        features.push_back(m1);
        features.push_back(m2);
        features.push_back(get_total_manhattan_dist(board, score_cols, side));
        features.push_back(get_total_manhattan_dist(board, score_cols, opponent(side)));
        features.push_back(riversToGoal(board, side, rows, cols, score_cols));
        features.push_back(riversToGoal(board, opponent(side), rows, cols, score_cols));
        features.push_back(total1);
        features.push_back(total2);
        // features.push_back(dist_to_closest_river(board, side, score_cols));
        // features.push_back(dist_to_closest_river(board, opponent(side), score_cols));
        for (size_t i = 0; i < features.size(); i++) {
            score += weights[i] * features[i];
        }
        return score;
    }

    Move choose(const Board& board, int row, int col, const vector<int>& score_cols, float current_player_time, float opponent_time) {
        Board curboard = board;
        return maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9).second;
    }

    pair<double, Move> maxValue(Board& board, int row, int col, const vector<int>& score_cols, int depth, double alpha, double beta) {
        if (depth == cutoffdepth) {
            return {evaluateBoard(board, row, col, score_cols), Move()}; // return dummy move
        }
        double max_val = -1e9;
        Move best_move;
        vector<pair<double, Move>> visit_order;
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
            validate_and_apply_move(board, move, side, row, col, score_cols);
            visit_order.push_back({evaluateBoard(board, row, col, score_cols), move});
            reverse_move(board, move, side, row, col, score_cols, from_river_orient);
        }
        if (depth == 0) {
            sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
                return a.first > b.first;
            });
        }
        for (auto [_, move]:visit_order) {
            moves++;
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty())
            if (board[move.from[1]][move.from[0]].at("side") == "river") {
                if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
                    from_river_orient = 1; // vertical orientation
                } else {
                    from_river_orient = 2;
                }
            }
            validate_and_apply_move(board, move, side, row, col, score_cols);
            double val = minValue(board, row, col, score_cols, depth + 1, alpha, beta).first;
            reverse_move(board, move, side, row, col, score_cols, from_river_orient);
            moves--;
            alpha = max(alpha, val);
            if (val > max_val) {
                max_val = val;
                best_move = move;
            }
            if (beta <= alpha) break;
        }
        return {max_val, best_move};
    }

    pair<double, Move> minValue(Board& board, int row, int col, const vector<int>& score_cols, int depth, double alpha, double beta) {
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
            validate_and_apply_move(board, move, opponent(side), row, col, score_cols);
            visit_order.push_back({evaluateBoard(board, row, col, score_cols), move});
            reverse_move(board, move, opponent(side), row, col, score_cols, from_river_orient);
        }
        // sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
        //     return evaluateBoard(a.first, row, col, score_cols) < evaluateBoard(b.first, row, col, score_cols);
        // });
        for (auto [_, move] : visit_order) {
            moves++;
            int from_river_orient = 0;
            if (!board[move.from[1]][move.from[0]].empty())
            if (board[move.from[1]][move.from[0]].at("side") == "river") {
                if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
                    from_river_orient = 1; // vertical orientation
                } else {
                    from_river_orient = 2;
                }
            }
            validate_and_apply_move(board, move, side, row, col, score_cols);
            double val = maxValue(board, row, col, score_cols, depth + 1, alpha, beta).first;
            reverse_move(board, move, opponent(side), row, col, score_cols, from_river_orient);
            moves--;
            beta = min(beta, val);
            if (val < min_val) {
                min_val = val;
                worst_move = move;
            }
            if (beta <= alpha) break;
        }
        return {min_val, worst_move};
    }

   

private:
    string side;
    int cutoffdepth = 2;
    random_device rd;
    mt19937 gen;
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