#ifndef INCLUDE_NEURO_WEBSOCKETPP_LIBRARY_HPP
#define INCLUDE_NEURO_WEBSOCKETPP_LIBRARY_HPP

#include <utility>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include "websocketpp/client.hpp"
#include <iostream>
#include <string>
#include "json.hpp"

namespace NeuroWebsocketpp {
    class Action {
    public:
        // Constructor
        Action(std::string  name, std::string  description, nlohmann::json& schema)
            : name(std::move(name)), description(std::move(description)), schema(schema) {}

        std::string getName() const {
            return name;
        }

        std::string getDescription() const {
            return description;
        }

        nlohmann::json getSchema() const {
            return schema;
        }

    private:
        std::string name;
        std::string description;
        nlohmann::json schema;
    };


    class NeuroResponse {
    public:
        explicit NeuroResponse(const std::string& jsonStr);
        std::string getCommand() const {
            return command;
        }
        std::string getId() const {
            return id;
        }
        std::string getName() const {
            return name;
        }
        std::string getData() const {
            return data;
        }
    private:
        std::string command;
        std::string id;
        std::string name;
        std::string data;
    };

    NeuroResponse::NeuroResponse(const std::string& jsonStr) {
        try {
            if (jsonStr.empty()) {
                id = "";
                name = "";
                data = "";
                command = "";
                return;
            }
            // Parse the input JSON string into a JSON object
            nlohmann::json parsedJson = nlohmann::json::parse(jsonStr);

            // Extract the mandatory "command" field
            if (parsedJson.contains("command") && parsedJson["command"].is_string()) {
                command = parsedJson["command"];
            } else {
                throw std::invalid_argument("JSON is missing the 'command' field or it is not a string.");
            }


            id = parsedJson["data"]["id"];
            name = parsedJson["data"]["name"];
            data = parsedJson["data"]["data"];

        } catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON string: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw std::invalid_argument("Error processing JSON: " + std::string(e.what()));
        }
    }




    // Type definitions
    using websocketpp::connection_hdl;
    using client = websocketpp::client<websocketpp::config::asio_client>;
    typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
    class NeuroGameClient {
    public:
        virtual ~NeuroGameClient() {
            if (connection_thread.joinable()) {
                connection_thread.join();
            }
        }


        NeuroGameClient(const std::string& uri, std::string  game_name, std::ostream& output_stream = std::cout, std::ostream& error_stream = std::cerr)
            : output(output_stream), error(error_stream), game_name(std::move(game_name)), lastResponse("") {
            // Initialize WebSocket++ client
            ws_client.init_asio();

            // Set message and open handlers
            ws_client.set_open_handler( [this](connection_hdl && PH1) { on_open(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_message_handler([this](connection_hdl && PH1, message_ptr && PH2) { on_message(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); });
            ws_client.set_close_handler([this](connection_hdl && PH1) { on_close(std::forward<decltype(PH1)>(PH1)); });
            std::unique_lock<std::mutex> lock(mutex); // Acquire lock
            connection_thread = std::thread(&NeuroGameClient::connect, this, uri);
            condition.wait(lock, [this]() { return connected; }); // Wait until connected

        }

        // Sends a "Startup" message to notify that the game has started
        void sendStartup() {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "startup";
            send(payload.dump());
        }

        // Sends a "Context" message to notify Neuro about in-game events
        void sendContext(const std::string& context_message, bool silent) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "context";
            payload["data"]["message"] = context_message;
            payload["data"]["silent"] = silent;

            send(payload.dump());
        }

        // Sends a "Register Actions" message to register actions with Neuro
        void sendRegisterActions(const std::vector<Action>& actions) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "actions/register";
            payload["data"]["actions"] = nlohmann::json::array();
            for (const auto& action : actions) {
                nlohmann::json action_json;
                action_json["name"] = action.getName();
                action_json["description"] = action.getDescription();
                action_json["schema"] = action.getSchema();
                payload["data"]["actions"].push_back(action_json);
            }
            send(payload.dump());

        }

        // Sends an "Unregister Actions" message to remove actions
        void sendUnregisterActions(const std::vector<std::string>& action_names) {
            nlohmann::json payload;
            for (const auto& action : action_names) {
                payload.push_back(action);
            }
            send(payload.dump());
        }

        // Sends a "Force Actions" message to force Neuro to execute certain actions
        void sendForceActions(const std::string& state, const std::string& query, bool ephemeral, const std::vector<std::string>& actions) {
            nlohmann::json payload;
            payload["command"] = "actions/force";
            payload["game"] = game_name;
            payload["data"]["state"] = state;
            payload["data"]["query"] = query;
            payload["data"]["ephemeral_context"] = ephemeral;
            payload["data"]["action_names"] = actions;
            send(payload.dump());
        }

        // Sends an "Action Result" message to report the result of an action execution
        void sendActionResult(const NeuroResponse& neuroAction, bool success, std::string& message) {
            nlohmann::json payload;
            payload["command"] = "action/result";
            payload["game"] = game_name;
            payload["data"]["id"] = neuroAction.getId();
            payload["data"]["success"] = success;
            payload["data"]["message"] = message;
            send(payload.dump());
        }

        void forceAction(const std::string& state, const std::string& query, bool ephemeral, const std::vector<std::string>& actions) {
            std::unique_lock<std::mutex> lock(mutex); // Acquire lock
            forcedActions = actions;
            waitingForForcedAction = true;
            sendForceActions(state, query, ephemeral, actions);
            condition.wait(lock, [this]() { return !waitingForForcedAction; });
            forcedActions.clear();
        }



    protected:
        void on_open(connection_hdl hdl) {
            output << "Connection established!" << std::endl;
            ws_hdl = std::move(hdl);
            connected = true;
            condition.notify_one();

        }


        //Override this method to handle actions.
        //To prevent race conditions, only accept disposable actions when waitingForForcedAction is true
        //When valid action arrives, set waitingForForcedAction to false to continue execution
        //Remember to unregister actions and send action result to Neuro ASAP
        virtual void handleMessage(NeuroResponse const& response) = 0;

        virtual void on_message(const connection_hdl&, const client::message_ptr& msg) {
            {
                std::lock_guard<std::mutex> lock(mutex); // Ensure thread safety
                auto message = msg->get_payload();
                auto JsonMessage = nlohmann::json::parse(message);
                if (JsonMessage["command"] == "actions/reregister_all")
                    return;
                NeuroResponse const response = NeuroResponse(message);
                handleMessage(response);
                lastResponse = response;
            }

            condition.notify_one();
        }
        void on_close(const connection_hdl&) {
            output << "Connection closed." << std::endl;
        }

        void send(const std::string& message) {
            websocketpp::lib::error_code ec;
            ws_client.send(ws_hdl, message, websocketpp::frame::opcode::text, ec);

            if (ec) {
                error << "Error sending message: " << ec.message() << std::endl;
            }
        }

        void connect(const std::string& uri) {
            websocketpp::lib::error_code ec;
            auto con = ws_client.get_connection(uri, ec);

            if (ec) {
                error << "Error creating connection: " << ec.message() << std::endl;
                return;
            }

            ws_client.connect(con);
            ws_client.run();
        }

        client ws_client;
        connection_hdl ws_hdl;
        std::ostream& output;
        std::ostream& error;
        std::string game_name;
        std::thread connection_thread;
        std::mutex mutex;
        std::condition_variable condition; // Condition variable for waiting and notifying
        NeuroResponse lastResponse; // Response from Neuro
        bool waitingForForcedAction = false;
        bool connected = false;
        std::vector<std::string> forcedActions;
    };

}

#endif