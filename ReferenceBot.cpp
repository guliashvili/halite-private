/*****
 *
 *
 *  DO NOT EDIT - change MyBot.cpp instead
 *  This is reference bot which is performing well so any changes
 *  should be compared against it.
 *
 *  When improved - just replace this code with new reference bot
 *
 *
*****/

#include "hltref/constants.hpp"
#include "hltref/game.hpp"
#include "hltref/genes.hpp"
#include "hltref/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <optional>
#include <random>
#include <ratio>
#include <tuple>
#include <unordered_map>

using namespace std;
using namespace hlt;
using namespace std::chrono;

shared_ptr<Genes> genes;
Game game;
shared_ptr<Player> me;
high_resolution_clock::time_point t1;

int get_milisecond_left() {
  return 2000 - duration_cast<std::chrono::milliseconds>(
                    high_resolution_clock::now() - t1)
                    .count();
}

void navigate(const shared_ptr<Ship> ship, const Direction &direction,
              vector<tuple<shared_ptr<Ship>, Direction>> &direction_queue) {
  game.game_map->navigate(ship, direction);
  direction_queue.emplace_back(ship, direction);
}

Direction greedySquareMove(shared_ptr<Ship> ship, Position &target,
                           bool recall = false) {
  auto directions = game.game_map->get_safe_moves(ship, target, recall);
  if (directions.size() == 0) {
    return Direction::STILL;
  } else if (directions.size() == 1) {
    return directions[0];
  } else {
    auto a =
        game.game_map->at(ship->position.directional_offset(directions[0]));
    auto b =
        game.game_map->at(ship->position.directional_offset(directions[1]));
    if (a->halite > b->halite) {
      swap(a, b);
    }
    if (a->move_cost() >
        ship->halite - game.game_map->at(ship->position)->move_cost()) {
      return directions[1];
    } else if (b->move_cost() - a->move_cost() <
               genes->greedy_walk_randomisation_margin) {
      srand(ship->id * game.turn_number * target.x * target.y * genes->seed);
      return directions[rand() % directions.size()];
    } else {
      return directions[0];
    }
  }
}


bool faking_dropoff[64][64];

pair<int, shared_ptr<Entity>>
getMinDistanceToDropoff(Position &position,
                        const vector<shared_ptr<Entity>> &dropoffs) {
  pair<int, shared_ptr<Entity>> minDistance;
  minDistance.first = 999999999;
  for (const auto &dropoff : dropoffs) {
    if(faking_dropoff[dropoff->position.x][dropoff->position.y] && game.me->halite < 3000){
      continue;
    }
    int cur_distance =
        game.game_map->calculate_distance(position, dropoff->position);
    if (cur_distance < minDistance.first) {
      minDistance.first = cur_distance;
      minDistance.second = dropoff;
    }
  }
  return minDistance;
}

std::optional<Direction> isRecallTime(shared_ptr<Ship> ship) {
  auto minDstDropoff =
      getMinDistanceToDropoff(ship->position, me->all_dropoffs);
  if (constants::MAX_TURNS - game.turn_number - genes->extra_time_for_recall >
      (minDstDropoff.first +
       me->ships.size() / 10.0 / me->all_dropoffs.size())) {
    return {};
  }
  return greedySquareMove(ship, minDstDropoff.second->position,
                          true); // TODO might stand still when blocked
}

bool shouldGoHome(shared_ptr<Ship> ship) {
  // Heuristic: return home with halite ship if collected >= 900
  return ship->halite >= constants::MAX_HALITE * 9 / 10;
}

const int DP_MAX_TURNS = 100;
tuple<int, Direction, int> dp[64][64][DP_MAX_TURNS];

int DP_MARK = 1;

Position _dp_walk_next_pos;
vector<tuple<int, Direction, int>>
compute_dp_walk(shared_ptr<Ship> ship, Position target, bool recall = false) {
  if (get_milisecond_left() < 500) {
    return {{0, greedySquareMove(ship, target, recall), 0}};
  }

  DP_MARK++;

  const auto moves_closer =
      game.game_map->get_unsafe_moves(ship->position, target);
  auto y_move = Direction::NORTH;
  auto x_move = Direction::EAST;
  if (find(moves_closer.begin(), moves_closer.end(), Direction::SOUTH) !=
      moves_closer.end()) {
    y_move = Direction::SOUTH;
  }
  if (find(moves_closer.begin(), moves_closer.end(), Direction::WEST) !=
      moves_closer.end()) {
    x_move = Direction::WEST;
  }

  dp[ship->position.x][ship->position.y][0] = {ship->halite, Direction::NONE,
                                               DP_MARK};

  const int MAX_CUR_TURN = (constants::WIDTH + constants::HEIGHT) * 1.5 / 2;

  Position cur_edge_position = ship->position;
  int edge_dist = 0;
  while (1) {

    Position cur_position = cur_edge_position;
    int cur_dist = edge_dist;
    while (1) {
      bool found_some = false;
      const vector<Direction> &moves =
          (cur_position == ship->position)
              ? game.game_map->get_safe_moves(ship, target, recall)
              : game.game_map->get_unsafe_moves(cur_position, target);
      vector<pair<Direction, Position>> move_pos;
      move_pos.reserve(moves.size());
      for (auto move : moves) {
        move_pos.emplace_back(move, cur_position.directional_offset(move));
      }
      for (int cur_turn = cur_dist; cur_turn < MAX_CUR_TURN; cur_turn++) {
        const auto &cur_dp_state = dp[cur_position.x][cur_position.y][cur_turn];

        if (get<2>(cur_dp_state) == DP_MARK) {
          found_some = true;
          // log::log(to_string(cur_turn) + " " + to_string(cur_position.x) +
          // ":" + to_string(cur_position.y) + "  -  " + " hal " +
          // to_string(get<0>(cur_dp_state)) + " move sizes " +
          // to_string(moves.size()));

          for (const auto &[move, _dp_walk_next_pos] : move_pos) {
            int halite_to_grab = game.game_map->at(cur_position)->halite;
            int cur_halite = get<0>(cur_dp_state);
            int stay_turns = 0;

            while (1) {
              const int halite_left =
                  cur_halite - halite_to_grab / constants::MOVE_COST_RATIO;
              if (halite_left >= 0) {
                const int next_turn = cur_turn + stay_turns + 1;
                if (next_turn < MAX_CUR_TURN) {
                  if (get<2>(dp[_dp_walk_next_pos.x][_dp_walk_next_pos.y]
                               [next_turn]) != DP_MARK ||
                      get<0>(dp[_dp_walk_next_pos.x][_dp_walk_next_pos.y]
                               [next_turn]) < halite_left) {
                    if (get<1>(cur_dp_state) == Direction::NONE) {
                      // log::log("upd " + to_string(next_pos.x) + " " +
                      // to_string(next_pos.y) + " " + to_string(next_turn));
                      dp[_dp_walk_next_pos.x][_dp_walk_next_pos.y][next_turn] =
                          {halite_left,
                           (stay_turns > 0) ? Direction::STILL : move, DP_MARK};
                    } else {
                      // log::log("upd2 " + to_string(next_pos.x) + " " +
                      // to_string(next_pos.y) + " " + to_string(next_turn));
                      dp[_dp_walk_next_pos.x][_dp_walk_next_pos.y][next_turn] =
                          {halite_left, get<1>(cur_dp_state), DP_MARK};
                    }
                  }
                }
              }

              if (cur_halite == constants::MAX_HALITE ||
                  halite_to_grab / constants::EXTRACT_RATIO == 0) {
                break;
              }
              stay_turns++;
              const int extraction =
                  (halite_to_grab + constants::EXTRACT_RATIO - 1) /
                  constants::EXTRACT_RATIO;
              cur_halite += extraction;
              cur_halite = min(cur_halite, constants::MAX_HALITE);
              halite_to_grab -= extraction;
            }
          }

        } else {
          if (found_some) {
            break;
          }
        }
      }

      if (cur_position.y == target.y) {
        break;
      }
      cur_position.directional_offset_self(y_move);
      cur_dist++;
    }

    if (cur_edge_position.x == target.x) {
      break;
    }
    cur_edge_position.directional_offset_self(x_move);
    edge_dist++;
  }

  vector<tuple<int, Direction, int>> efficient_possibilities;
  for (int turn = 0; turn < MAX_CUR_TURN; turn++) {
    if (get<2>(dp[target.x][target.y][turn]) == DP_MARK) {
      efficient_possibilities.push_back({turn,
                                         (get<1>(dp[target.x][target.y][turn])==Direction::NONE)?Direction::STILL:get<1>(dp[target.x][target.y][turn]),
                                         get<0>(dp[target.x][target.y][turn])});
    }
  }
  return efficient_possibilities;
}

int NUM_OF_MOVES_FROM_HOME[1000] = {0};

Direction goToPointEfficient(shared_ptr<Ship> ship, Position destination) {
  const auto dp_results = compute_dp_walk(ship, destination);
  if (dp_results.size() == 0) {
    return Direction::STILL;
  } else {
    const int num_of_turns_from_home = NUM_OF_MOVES_FROM_HOME[ship->id];
    pair<double, Direction> best = {-1, Direction::STILL};
    for (auto &[steps, direction, halite] : dp_results) {
      double cur_efficiency = double(halite) / (num_of_turns_from_home + steps);
      if (cur_efficiency > get<0>(best)) {
        best = {cur_efficiency, direction};
      }
    }
    return get<1>(best);
  }
}

Direction goToPointFast(shared_ptr<Ship> ship, Position destination,
                        bool recall = false) {
  if (ship->position == destination) {
    return Direction::STILL;
  }

  const auto &dp_results = compute_dp_walk(ship, destination, recall);
  if (dp_results.size() == 0) {
    return Direction::STILL;
  } else {
    return get<1>(dp_results[0]);
  }
}

Direction goHome(shared_ptr<Ship> ship) {
  auto minDstDropoff =
      getMinDistanceToDropoff(ship->position, me->all_dropoffs);

  return goToPointEfficient(
      ship,
      minDstDropoff.second->position); // TODO might stand still when blocked
}

vector<tuple<unsigned, Position, double>> _candidates;
int ship_pair_st[64][64] = {{0}};
int PAIR_MARK = 1;

void pair_ships(vector<shared_ptr<Ship>> &ships,
                vector<tuple<shared_ptr<Ship>, Direction>> &ship_directions) {
  PAIR_MARK++;
  auto &candidates = _candidates;
  candidates.resize(0);

  vector<tuple<shared_ptr<Ship>, Direction>> ret;

  {
    Position pos;
    for (int &y = pos.y = 0; y < constants::HEIGHT; y++) {
      for (int &x = pos.x = 0; x < constants::WIDTH; x++) {
        if (ship_pair_st[y][x] == PAIR_MARK) {
          continue;
        }

        auto target_cell = game.game_map->at(pos);
        auto target_halite_amount = target_cell->halite;
        if (target_cell->extract_halite() == 0) {
          continue;
        }
        // TODO SOMEONE ELSE SITS ON THAT

        for (unsigned i = 0; i < ships.size(); i++) {
          auto ship = ships[i];
          int distance = game.game_map->calculate_distance(ship->position, pos);
          candidates.emplace_back(i, pos,
                                  min(constants::MAX_HALITE - ship->halite,
                                      target_halite_amount - 3) /
                                      double(distance + 1));
        }
      }
    }
  }

  sort(candidates.begin(), candidates.end(),
       [](const auto &lhs, const auto &rhs) {
         return std::get<2>(lhs) > std::get<2>(rhs);
       });

  for (unsigned I = 0; I < candidates.size(); I++) {
    auto i = get<0>(candidates[I]);
    auto &pos = get<1>(candidates[I]);
    if (ships[i] == nullptr) {
      continue;
    }
    if (ship_pair_st[pos.y][pos.x] == PAIR_MARK) {
      continue;
    }
    auto &ship = ships[i];
    if (pos != ship->position &&
        game.game_map->get_safe_moves(ship, pos).size() == 0) {
      continue;
    }

    navigate(ship, goToPointFast(ship, pos), ship_directions);

    ship_pair_st[pos.y][pos.x] = PAIR_MARK;
    ship = nullptr;
  }

  for (auto &ship : ships) {
    if (ship != nullptr) {
      navigate(ship, Direction::STILL, ship_directions);
      // log::log("pairstill " + to_string(ship->id));
    }
  }
}

// bool GOING_HOME[1000] = {false};
int AVERAGE_TIME_TO_HOME = 0;

bool should_ship_new_ship() {
  int total_me = game.me->ships.size();
  if (total_me == 0) {
    return true;
  }

  int total_halite = 0;
  {
    Position position;
    for (int &i = position.y = 0; i < constants::HEIGHT; i++) {
      for (int &j = position.x = 0; j < constants::WIDTH; j++) {
        total_halite += max(0, game.game_map->at(position)->halite -
                                   genes->total_halite_margin_substr);
      }
    }
  }

  int total_ships_count = 0;
  for (const auto &player : game.players) {
    total_ships_count += player->ships.size();
  }

  int current_halite_prediction = total_halite * total_me / total_ships_count;
  int next_halite_prediction =
      total_halite * (total_me + 1) / (total_ships_count + 1);

  return next_halite_prediction - current_halite_prediction >
             genes->margin_to_create_new_ship &&
         constants::MAX_TURNS - AVERAGE_TIME_TO_HOME >
             genes->ship_spawn_step_margin;
}


// int map_halite_clone[64][64];

vector<Position> faking_dropoffs;
bool isTimeToDropoff(){
  return game.turn_number > 50 && game.me->ships.size() > 15 && (faking_dropoffs.size() + game.me->dropoffs.size()) == 0;
}

Position find_dropoff_place(){

  pair<int, Position> best_dropoff;
  best_dropoff.first = -1;
  Position pos;
  for(int &x = pos.x = 0; x < constants::WIDTH; x++){
    for(int &y = pos.y = 0; y < constants::HEIGHT; y++){
      if(game.game_map->at(pos)->has_structure()){
        continue;
      }
      const int effect_distance = constants::WIDTH / game.players.size() / 4;

      int total_halite_in_range = 0;
      for(int delta_x = -effect_distance; delta_x < effect_distance; delta_x++){
        for(int delta_y = -(effect_distance-abs(delta_x)); delta_y < (effect_distance-abs(delta_x)); delta_y++){
          Position cur_cell( (((x+delta_x)%constants::WIDTH)+constants::WIDTH)%constants::WIDTH,  (((y+delta_y)%constants::HEIGHT)+constants::HEIGHT)%constants::HEIGHT);
          total_halite_in_range += game.game_map->at(cur_cell)->halite;
        }
      }

      if(total_halite_in_range > get<0>(best_dropoff)){
        best_dropoff = {total_halite_in_range, pos};
      }

      // map_halite_clone[x][y] = game.game_map->at(pos)->halite;
    }
  }

  return get<1>(best_dropoff);

  // for(auto player : game.players){
  //   for(auto dropoff : player->all_dropoffs){
  //     for(int delta_x = -)
  //     min_dst = min(min_dst, game.game_map->calculate_distance(candidate_pos, dropoff->position))
  //   }
  // }
}

// boolean flag telling is ship currently going home (to shipyard)
bool GO_HOME_EFFICIENT[1000] = {false};

vector<shared_ptr<Ship>> oplock_doStepNoStill(
    vector<shared_ptr<Ship>> ships,
    vector<tuple<shared_ptr<Ship>, Direction>> &direction_queue_original) {
  vector<tuple<shared_ptr<Ship>, Direction>> direction_queue_temporary;
  direction_queue_temporary.reserve(ships.size());

  vector<shared_ptr<Ship>> ready_to_pair;
  vector<shared_ptr<Ship>> going_home;
  vector<shared_ptr<Ship>> going_home_now;
  sort(ships.begin(), ships.end(), [](const auto &lhs, const auto &rhs) {
    return get<0>(
               getMinDistanceToDropoff(lhs->position, game.me->all_dropoffs)) <
           get<0>(
               getMinDistanceToDropoff(rhs->position, game.me->all_dropoffs));
  });
  for (const auto &ship : ships) {

    // returned to home shipyard
    if (game.game_map->has_my_structure(ship->position)) {
      GO_HOME_EFFICIENT[ship->id] = false;
    }

    // time to return home at the end of the game
    if (auto recall = isRecallTime(ship)) {
      navigate(ship, recall.value(), direction_queue_temporary);
    } else if (GO_HOME_EFFICIENT[ship->id]) {
      going_home.push_back(ship);
    } else if (shouldGoHome(ship)) {
      going_home.push_back(ship);
      going_home_now.push_back(ship);
    } else {
      ready_to_pair.push_back(ship);
    }
  }

  bool was_blocker =
      std::find_if(direction_queue_temporary.begin(),
                   direction_queue_temporary.end(), [](const auto &element) {
                     return get<1>(element) == Direction::STILL;
                   }) != direction_queue_temporary.end();

  vector<shared_ptr<Ship>> ships_no_still;
  if (!was_blocker) {
    pair_ships(ready_to_pair, direction_queue_temporary);

    for (auto &ship : going_home) {
      const auto &goHomeDir = goHome(ship);
      navigate(ship, goHomeDir, direction_queue_temporary);
      GO_HOME_EFFICIENT[ship->id] = true;
    }

  } else {
    for (auto &ship : ready_to_pair) {
      ships_no_still.push_back(ship);
    }
    for (auto &ship : going_home) {
      ships_no_still.push_back(ship);
    }
  }

  was_blocker =
      was_blocker ||
      std::find_if(direction_queue_temporary.begin(),
                   direction_queue_temporary.end(), [](const auto &element) {
                     return get<1>(element) == Direction::STILL;
                   }) != direction_queue_temporary.end();

  if (was_blocker) {
    ships_no_still.reserve(ships.size());
    for (auto &[ship, direction] : direction_queue_temporary) {
      if (direction != Direction::STILL) {
        auto target_pos = ship->position.directional_offset(direction);
        if (game.game_map->at(target_pos)->ship == ship) {
          game.game_map->at(target_pos)->mark_safe();
        }
        ships_no_still.push_back(ship);
      } else {
        game.game_map->at(ship->position)->mark_unsafe(ship);
        direction_queue_original.emplace_back(ship, direction);
      }
    }

    for (auto ship : going_home_now) {
      GO_HOME_EFFICIENT[ship->id] = false;
    }

    return ships_no_still;
  } else {
    // for(auto& [ship, direction] : direction_queue_temporary){
    //   log::log("the end: " + to_string(ship->id));
    // }
    direction_queue_original.insert(direction_queue_original.end(),
                                    direction_queue_temporary.begin(),
                                    direction_queue_temporary.end());
    return {};
  }
}

int savings = 0;
vector<Command> doStep(vector<tuple<shared_ptr<Ship>, Direction>> &direction_queue) {

  t1 = high_resolution_clock::now();
  vector<Command> constructions;
  me = game.me;
  unique_ptr<GameMap> &game_map = game.game_map;
  game_map->init(me->id, genes);

  if(isTimeToDropoff()){
    savings += 4000;
    Position pos = find_dropoff_place();
    faking_dropoff[pos.x][pos.y] = true;
    faking_dropoffs.push_back(pos);
  }

  for(auto &position : faking_dropoffs){
    if(game.game_map->at(position)->has_structure() && game.game_map->at(position)->structure->owner != me->id){
      faking_dropoff[position.x][position.y] = false;
      position = find_dropoff_place();
      faking_dropoff[position.x][position.y] = true;
    }
    game.me->dropoffs.push_back(make_shared<Dropoff>(game.me->id, -1, position.x, position.y));
    game.me->all_dropoffs.push_back(make_shared<Entity>(game.me->id, -1, position.x, position.y));
    game.game_map->at(position)->structure = game.me->dropoffs.back();
  }

  vector<shared_ptr<Ship>> ships;

  for (const auto &ship : me->ships) {
    // ship returned to shipyard (which is used as dropoff point)
    // currently we do not have any other dropoffs except shipyard
    if (game_map->has_my_structure(ship->position)) {
      // GOING_HOME[ship->id] = false;
      AVERAGE_TIME_TO_HOME =
          AVERAGE_TIME_TO_HOME * genes->average_time_home_decay +
          NUM_OF_MOVES_FROM_HOME[ship->id] *
              (1 - genes->average_time_home_decay);
      NUM_OF_MOVES_FROM_HOME[ship->id] = 0;  // at home (shipyard)
    }
    if (game_map->has_my_structure(ship->position) && faking_dropoff[ship->position.x][ship->position.y]){
      if(me->halite >= 4000){
        me->halite -= 4000;
        savings -= 4000;
        faking_dropoff[ship->position.x][ship->position.y] = false;
        faking_dropoffs.erase(std::remove(faking_dropoffs.begin(), faking_dropoffs.end(), ship->position), faking_dropoffs.end());
        constructions.push_back(ship->make_dropoff());
      }else{
        direction_queue.emplace_back(ship, Direction::STILL);
      }
    }else if (!game_map->can_move(ship)) {
      navigate(ship, Direction::STILL, direction_queue);
    } else {
      game.game_map->at(ship->position)->mark_safe();
      ships.push_back(ship);
    }
  }

  while ((ships = oplock_doStepNoStill(ships, direction_queue)).size() != 0) {
  }

  for (const auto &ship : me->ships) {
    // ship is always going away from home
    // when going to target point to collect halite
    NUM_OF_MOVES_FROM_HOME[ship->id]++;
  }

  log::log("doStep duration: " +
           to_string(duration_cast<duration<double>>(
                         high_resolution_clock::now() - t1)
                         .count()) +
           " ships: " + to_string(me->ships.size()));
  auto spawn_ship = me->halite - savings >= constants::SHIP_COST &&
             !(game_map->at(me->shipyard->position)->is_occupied()) &&
             should_ship_new_ship();

  if(spawn_ship){
    constructions.push_back(command::spawn_ship());
  }
  return constructions;
}

int main(int argc, const char *argv[]) {

  _candidates.reserve(constants::WIDTH * constants::HEIGHT * 100);
  // At this point "game" variable is populated with initial map data.
  // This is a good place to do computationally expensive start-up
  // pre-processing. As soon as you call "ready" function below, the 2 second
  // per turn timer will start.
  game.ready("MyCppBot");
  genes = make_shared<Genes>(argc, argv);
  srand(genes->seed);
  // log::log("Successfully created bot! My Player ID is " +
  // to_string(game.my_id) + ". Bot rng seed is " + to_string(genes->seed) +
  // ".");

  ;
  vector<tuple<shared_ptr<Ship>, Direction>> direction_queue;
  while (1) {
    game.update_frame();

    direction_queue.resize(0);
    direction_queue.reserve(game.me->ships.size());

    vector<Command> command_queue = doStep(direction_queue);

    for (auto &[ship, direction] : direction_queue) {
      command_queue.push_back(command::move(ship->id, direction));
    }

    if (!game.end_turn(command_queue)) {
      break;
    }
  }

  return 0;
}
