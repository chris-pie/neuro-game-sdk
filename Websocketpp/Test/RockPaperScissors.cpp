
#include "../NeuroGameSdkWebsocketpp.hpp"

#include <condition_variable> // Include for synchronization
#include <mutex>
#include <string>


class RPS {
public:
    int score1 = 0;
    int score2 = 0;
};

class NeuroRPS : public NeuroWebsocketpp::NeuroGameClient {
    NeuroRPS(const NeuroRPS &) = delete;

    NeuroRPS &operator=(const NeuroRPS &) = delete;

public:
    NeuroRPS(const std::string &uri, const std::string &game_name, RPS &rps, std::ostream &output_stream = std::cout,
             std::ostream &error_stream = std::cerr)
        : NeuroGameClient(uri, game_name, output_stream, error_stream), rps(rps) {
    }

protected:
    void handleMessage(const NeuroWebsocketpp::NeuroResponse &response) override {
        if (waitingForForcedAction) {
            if (response.getName() == "play_paper") {
                std::string resp = "You win!";
                sendUnregisterActions({"play_rock", "play_paper", "play_scissors"});
                sendActionResult(response, true, resp);
                rps.score2++;
                waitingForForcedAction = false;
            } else if (response.getName() == "play_scissors") {
                std::string resp = "You lose!";
                sendUnregisterActions({"play_rock", "play_paper", "play_scissors"});
                sendActionResult(response, true, resp);
                rps.score1++;
                waitingForForcedAction = false;
            } else if (response.getName() == "play_rock") {
                std::string resp = "It's a draw!";
                sendUnregisterActions({"play_rock", "play_paper", "play_scissors"});
                sendActionResult(response, true, resp);
                waitingForForcedAction = false;
            } else {
                std::string resp = "That's not a valid move!";
                sendActionResult(response, false, resp);
            }

        }
        else {
            if (response.getName() == "middle_finger") {
                std::string resp = "Mind your manners yong lady!";
                sendActionResult(response, false, resp);

            }
            else { //Should not happen if actions are properly unregistered
                std::string resp = "Please wait for your turn";
                sendActionResult(response, false, resp);
            }
        }

    }

    RPS &rps;
};

int main() {
    RPS rps{};

    NeuroRPS client("ws://localhost:8000", "Rock Paper Scissors", rps, std::cout, std::cerr);
    int rounds = 50;
    nlohmann::json schema;
    nlohmann::json empty_schema;
    schema["type"] = "object";
    schema["properties"]["hand"] = "string";
    NeuroWebsocketpp::Action rock = NeuroWebsocketpp::Action("play_rock", "Play Rock with right or left hand", schema);
    NeuroWebsocketpp::Action paper = NeuroWebsocketpp::Action("play_paper", "Play Paper with right or left hand",
                                                              schema);
    NeuroWebsocketpp::Action scissors = NeuroWebsocketpp::Action("play_scissors",
                                                                 "Play Scissors with right or left hand", schema);
    NeuroWebsocketpp::Action middleFinger = NeuroWebsocketpp::Action("middle_finger",
                                                                     "Show middle finger with right or left hand",
                                                                     schema);

    client.sendStartup();
    client.sendRegisterActions(std::vector<NeuroWebsocketpp::Action>{middleFinger});

    while (rps.score1 < rounds && rps.score2 < rounds) {
        client.sendContext("I played Rock", false);
        client.sendRegisterActions(std::vector<NeuroWebsocketpp::Action>{rock, paper, scissors});
        std::ostringstream state, query;
        state << "I have " << rps.score1 << " points, you have " << rps.score2 << " points. I played Rock.";
        query << "Please play either rock, paper or scissors. Indicate whether you play it with left or right hand.";
        client.forceAction(state.str(), query.str(), false,
                           {"play_rock", "play_paper", "play_scissors", "middle_finger"});
    }
    client.sendContext("Game over!", false);


    return 0;
}
