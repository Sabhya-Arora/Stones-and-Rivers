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

pair<double, double> get_total_manhattan_dist (const Board& board, const set<pair<int,int>> &own_pieces, const vector<int>& score_cols, const string &side){
    double total_dist = 0;
    int rows = board.size();
    int cols = board[0].size();
    double min_manhattan = rows + cols;
    int score_row = (side=="circle") ? 2 : rows - 3; 
    for(auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;
        double min_dist = cols + rows;
        for (int sc : score_cols) {
            min_dist = min(min_dist, (double)abs(sc - x) + abs(score_row - y));
        }
        total_dist += 1 / (min_dist + 1);
        min_manhattan = min(min_manhattan, min_dist);
    }
    return {40 * total_dist, min_manhattan};
}


int countScoringPieces(const Board& board, const string& player, int rows, int cols, const vector<int>& score_cols) {
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
            if (countstones == 4) return 40;
            else if (countstones == 3 && countriver == 1) return 15;
            else if (countstones == 2 && countriver == 2) return 10;
            else if (countstones == 1 && countriver == 3) return 7;
            else return 4;
        } else if (score_cols.size() == 5) { // victory eval > 30
            if (countstones == 5) return 60;
            else if (countstones == 4 && countriver == 1) return 20;
            else if (countstones == 3 && countriver == 2) return 15;
            else if (countstones == 2 && countriver == 3) return 10;
            else if (countstones == 1 && countriver == 4) return 7;
            else return 5;
        } else if (score_cols.size() == 6) { // victory eval > 40
            if (countstones == 6) return 70;
            else if (countstones == 5 && countriver == 1) return 25;
            else if (countstones == 4 && countriver == 2) return 20;
            else if (countstones == 3 && countriver == 3) return 15;
            else if (countstones == 2 && countriver == 4) return 10;
            else if (countstones == 1 && countriver == 5) return 8;
            else return 6;
        }
    }
    return countstones + countriver;
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

int riversToGoalLite(const Board& board, const set<pair<int,int>> &own_pieces,
                     const string& player, int rows, int cols,
                     const vector<int>& score_cols) {
    int score = 0;

    for (auto piece : own_pieces) {
        int x = piece.first;
        int y = piece.second;

        if (board[y][x].empty()) continue;
        if (board[y][x].at("side") != "river") continue;
        if (is_own_score_cell(x, y, player, rows, cols, score_cols)) continue;

        // Determine flow directions based on orientation
        vector<pair<int,int>> dirs = 
            (board[y][x].at("orientation") == "horizontal") ? 
                vector<pair<int,int>>{{1,0}, {-1,0}} :
                vector<pair<int,int>>{{0,1}, {0,-1}};

        int blocker_count = 0;
        int temp_count = 0;
        // Look in both directions along the river
        for (auto [dx, dy] : dirs) {
            bool goal_direction = false;
            int nx = x + dx, ny = y + dy;
            while (in_bounds(nx, ny, rows, cols)) {
                if (is_opponent_score_cell(nx, ny, player, rows, cols, score_cols)) {
                    goal_direction = true;
                    break;
                }

                if (!board[ny][nx].empty()) {
                    const auto& cell = board[ny][nx];

                    // If it’s an opponent stone or a river flowing the wrong way → blocker
                    if (cell.at("owner") != player ||
                        (cell.at("side") == "river" && 
                         cell.at("orientation") != board[y][x].at("orientation"))) {
                        temp_count++;
                        break;
                    }
                }
                nx += dx;
                ny += dy;
            }
            if (goal_direction) {
                blocker_count += temp_count;
            } else {
                blocker_count = 3; // No path to goal in this direction
            }
        }

        // Assign weights depending on how open the flow line is
        if (blocker_count == 0) score += 4;   // completely open lane
        else if (blocker_count == 1) score += 2; // slightly blocked
        else if (blocker_count == 2) score += 1; // quite obstructed

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

    vector<double> weights = {20, -20, 1000, -1000, 100, -100, 1,-1, 10, -10, -50, 50, -40, 40};
    double evaluateBoard(const Board& board,const set<pair<int,int>> &own_pieces, const set<pair<int,int>> &opp_pieces, int rows, int cols, const vector<int>& score_cols) {
        vector<double> features;
        double score = 0;
        auto [totalman1, minman1] = get_total_manhattan_dist(board, own_pieces, score_cols, side);
        auto [totalman2, minman2] = get_total_manhattan_dist(board, opp_pieces, score_cols, opponent(side));
        features.push_back(riversToGoalLite(board, own_pieces, side, rows, cols, score_cols));
        features.push_back(riversToGoalLite(board, opp_pieces, opponent(side), rows, cols, score_cols));
        // features.push_back(countScoringPieces(board, side, rows, cols, score_cols));
        // features.push_back(countScoringPieces(board, opponent(side), rows, cols, score_cols));
        features.push_back(0);
        features.push_back(0);
        features.push_back(count_reachable_in_one(board, own_pieces, side, rows, cols, score_cols));
        features.push_back(count_reachable_in_one(board, opp_pieces, opponent(side), rows, cols, score_cols));
        features.push_back(totalman1);
        features.push_back(totalman2);
        // features.push_back(total1);
        // features.push_back(total2);
        features.push_back(0);
        features.push_back(0);

        features.push_back(minman1);
        features.push_back(minman2);
        int borderpiecesown = countBorderPieces(board, side, rows, cols, own_pieces);
        int borderpiecesopp = countBorderPieces(board, opponent(side), rows, cols, opp_pieces);
        features.push_back(borderpiecesown);
        features.push_back(borderpiecesopp);
        // features.push_back(dist_to_closest_river(board, side, score_cols));
        // features.push_back(dist_to_closest_river(board, opponent(side), score_cols));
        for (size_t i = 0; i < features.size(); i++) {
            score += weights[i] * features[i];
        }
        return score;
    }

    Move choose(const Board& board, int row, int col, const vector<int>& score_cols, float current_player_time, float opponent_time) {
        // if (moves < 10) {
        //     cutoffdepth = 2;
        // } else if (current_player_time > 10.0) {
        //     cutoffdepth = 3;
        // }
        if (current_player_time < 10.0 && cutoffdepth == 3) {
            cutoffdepth = 2;
        }
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
        if (curboard == last_board) {
            moves_since_change += 1;
            if (moves_since_change == repeat_limit) {
                pair<pair<double, Move>, pair<double, Move>> minimax = maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9);
                auto [besteval, bestmove] = minimax.first;
                auto [second_eval, second_move] = minimax.second;
                if (besteval - second_eval < 200) {
                    return last_move = second_move;
                } else {
                    return last_move = bestmove;
                }
            } else {
                return last_move;
            }
        }
        moves_since_change = 0;
        last_board = curboard;
        cout<<"Move played by depth 3"<<endl;
        auto [eval, played_move] = maxValue(curboard, row, col, score_cols, 0, -1e9, 1e9).first;
        cout << "Evaluation Score: " << eval << endl;
        return last_move = played_move;
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
        Move best_move;
        Move second_best_move;
        vector<pair<double, Move>> visit_order;
        // Generate all moves for this player
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
            double val = minValue(board, row, col, score_cols, depth + 1, alpha, beta).first;

            // Undo move
            reverse_move(board, own_pieces, opp_pieces, move, side, row, col, score_cols, from_river_orient);
            moves--;

            // Alpha-beta update
            if (val > max_val) {
                // Shift current best to second best
                second_max_val = max_val;
                second_best_move = best_move;

                // Update best
                max_val = val;
                best_move = move;
            } else if (val > second_max_val) {
                // Update second best only
                second_max_val = val;
                second_best_move = move;
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
    int repeat_limit = 100;
    int moves_since_change = 0;
    mt19937 gen;
    int pruned = 0;
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