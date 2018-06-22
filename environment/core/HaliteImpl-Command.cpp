#include <future>
#include "HaliteImpl.hpp"

namespace hlt {

/** Communicate with bots to obtain commands for next step. */
void HaliteImpl::retrieve_commands() {
    std::unordered_map<Player::id_type, std::future<Command>> results;
    for (auto &[player_id, player] : game->players) {
        results[player_id] = std::async(std::launch::async,
                                        [game = this->game, &player = player] {
                                            return game->networking.handle_frame(player);
                                        });
    }
    for (auto &[player_id, result] : results) {
        commands[player_id] = result.get();
    }
}

/** Process the effects of commands. */
void HaliteImpl::process_commands() {
    for (const auto &[player_id, command] : commands) {
        command->act_on_map(game->game_map, game->players[player_id]);
    }
}

}