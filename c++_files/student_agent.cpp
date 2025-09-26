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

pair<int, int> get_total_manhattan_dist (const Board& board, const set<pair<int,int>> &own_pieces, const vector<int>& score_cols, const string &side){
    int total_dist = 0;
    int rows = board.size();
    int cols = board[0].size();
    int min_manhattan = rows + cols;
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


int riversToGoal(const Board& board,const set <pair<int,int>> &own_pieces, const set<pair<int,int>> &opp_pieces, const string& player, int rows, int cols, const vector<int>& score_cols) {
    int count = 0;
    for(auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
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
    for (auto piece : opp_pieces) {
        int x = piece.first;
        int y = piece.second;
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

int dist_to_closest_river (const Board& board, const string &player, const vector<int>& score_cols) {
    int sum_dist = 0;
    int rows = board.size();
    int cols = board[0].size();
    // multi-source BFS from all rivers
    queue <pair<int,int>> q;
    vector<vector<int>> dist(rows, vector<int>(cols, 1e9));
    vector<vector<bool>> visited(rows, vector<bool>(cols, false));
    for (int y=0; y<rows; y++) {
        for (int x=0; x<cols; x++) {
            if (!board[y][x].empty() && board[y][x].at("side") == "river"){
                q.push({x,y});
                dist[y][x] = 0;
                visited[y][x] = true;
            } 
        }
    }
    vector<pair<int,int>> directions = {{1,0},{-1,0},{0,1},{0,-1}};
    while (!q.empty()) {
        auto [x,y] = q.front(); q.pop();
        for (auto [dx,dy]:directions) {
            int nx = x+dx, ny = y+dy;
            if (in_bounds(nx, ny, rows, cols) && !visited[ny][nx]) {
                visited[ny][nx] = true;
                dist[ny][nx] = dist[y][x] + 1;
                q.push({nx,ny}); 
            }
        }
    }

    for(int y=0; y<rows; y++) {
        for(int x=0; x<cols; x++) {
            if (!board[y][x].empty() && board[y][x].at("owner") == player && board[y][x].at("side") == "stone" && !is_own_score_cell(x,y,player,rows,cols,score_cols)) {
                sum_dist += dist[y][x];
            }
        }
    }
    if(sum_dist >= 1e9) return 12*(rows + cols); 
    return sum_dist;
}


int countScoringPieces(const Board& board, const string& player, int rows, int cols, const vector<int>& score_cols) {
    int scorerow = (player == "circle" ? 2 : rows - 3);
    int count = 0;
    for (int col:score_cols) {
        if (!board[scorerow][col].empty() && board[scorerow][col].at("owner") == player && board[scorerow][col].at("side") == "stone") {
            count++;
        }
    } 
    if (count == 4) return 10;
    return count;
}


// ---- Student Agent ----
class StudentAgent {
public:
    
    explicit StudentAgent(string side) : side(move(side)), gen(rd()) {} // side is own side

    vector<double> weights = {1000, -1000, 100, -100, -1, 1, 15, -15, 1, -1, -2, 2};
    double evaluateBoard(const Board& board,const set<pair<int,int>> &own_pieces, const set<pair<int,int>> &opp_pieces, int rows, int cols, const vector<int>& score_cols) {
        vector<double> features;
        double score = 0;
        auto [m1, total1] = count_reachable_in_one(board, own_pieces, side, rows, cols, score_cols);
        auto [m2, total2] = count_reachable_in_one(board, opp_pieces, opponent(side), rows, cols, score_cols);
        auto [totalman1, minman1] = get_total_manhattan_dist(board, own_pieces, score_cols, side);
        auto [totalman2, minman2] = get_total_manhattan_dist(board, opp_pieces, score_cols, opponent(side));
        features.push_back(countScoringPieces(board, side, rows, cols, score_cols));
        features.push_back(countScoringPieces(board, opponent(side), rows, cols, score_cols));
        features.push_back(m1);
        features.push_back(m2);
        features.push_back(totalman1);
        features.push_back(totalman2);
        features.push_back(riversToGoal(board, own_pieces, opp_pieces, opponent(side), rows, cols, score_cols));
        features.push_back(riversToGoal(board, own_pieces, opp_pieces, side, rows, cols, score_cols));
        // features.push_back(total1);
        // features.push_back(total2);
        // features.push_back(minman1);
        // features.push_back(minman2);
        // features.push_back(dist_to_closest_river(board, side, score_cols));
        // features.push_back(dist_to_closest_river(board, opponent(side), score_cols));
        for (size_t i = 0; i < features.size(); i++) {
            score += weights[i] * features[i];
        }
        return score;
    }

    Move choose(const Board& board, int row, int col, const vector<int>& score_cols, float current_player_time, float opponent_time) {
        Board curboard = board;
        own_pieces.clear();
        opp_pieces.clear();
        for (int y=0; y<row; y++) {
            for (int x=0; x<col; x++) { 
                if (!curboard[y][x].empty()) {
                    if (curboard[y][x].at("owner") == side) {
                        own_pieces.insert({x,y});
                    } else {
                        opp_pieces.insert({x,y});
                    }
                }
            }
        }
        moves += 1;
        return maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9).second;
    }

    pair<double, Move> maxValue(Board& board, int row, int col, const vector<int>& score_cols, int depth, double alpha, double beta) {
        if (depth == cutoffdepth) {
            return {evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), Move()}; // return dummy move
        }
        double max_val = -1e9;
        Move best_move;
        vector<pair<double, Move>> visit_order;
        for (const auto& move : generate_all_moves(board, side, row, col, score_cols)) {
        //     int from_river_orient = 0;
        //     if (!board[move.from[1]][move.from[0]].empty())
        //     if (board[move.from[1]][move.from[0]].at("side") == "river") {
        //         if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
        //             from_river_orient = 1; // vertical orientation
        //         } else {
        //             from_river_orient = 2;
        //         }
        //     }
        //     validate_and_apply_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols);
        //     visit_order.push_back({evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), move});
        //     reverse_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols, from_river_orient);
        // }
        // if (depth != cutoffdepth - 1)
        // sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
        //     return a.first > b.first;
        // });
        // for (auto [_, move]: visit_order) {
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
            validate_and_apply_move(board,own_pieces, opp_pieces, move, side, row, col, score_cols);
            double val = minValue(board, row, col, score_cols, depth + 1, alpha, beta).first;
            reverse_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols, from_river_orient);
            moves--;
            alpha = max(alpha, val);
            if (val > max_val) {
                max_val = val;
                best_move = move;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return {max_val, best_move};
    }

    pair<double, Move> minValue(Board& board, int row, int col, const vector<int>& score_cols, int depth, double alpha, double beta) {
        
        if (depth == cutoffdepth) {
            return {evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), Move()}; // return dummy move
        }
        double min_val = 1e9;
        Move worst_move;
        vector<pair<double, Move>> visit_order;
        for (const auto& move : generate_all_moves(board, opponent(side), row, col, score_cols)) {
            
        //     int from_river_orient = 0;
        //     if (!board[move.from[1]][move.from[0]].empty())
        //     if (board[move.from[1]][move.from[0]].at("side") == "river") {
        //         if (board[move.from[1]][move.from[0]].at("orientation") == "vertical") {
        //             from_river_orient = 1; // vertical orientation
        //         } else {
        //             from_river_orient = 2;
        //         }
        //     }
        //     validate_and_apply_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols);
        //     visit_order.push_back({evaluateBoard(board, own_pieces, opp_pieces, row, col, score_cols), move});
        //     reverse_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols, from_river_orient);
        // }
        // if (depth != cutoffdepth - 1)
        // sort(visit_order.begin(), visit_order.end(), [&](const auto& a, const auto& b) {
        //     return a.first < b.first;
        // });
        // for (auto [_, move]: visit_order) {
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
            validate_and_apply_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols);
            double val = maxValue(board, row, col, score_cols, depth + 1, alpha, beta).first;
            reverse_move(board, opp_pieces, own_pieces, move, opponent(side), row, col, score_cols, from_river_orient);
            moves--;
            beta = min(beta, val);
            if (val < min_val) {
                min_val = val;
                worst_move = move;
            }
            if (beta <= alpha) {
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
    int cutoffdepth = 2;
    set <pair<int,int>> own_pieces, opp_pieces;
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