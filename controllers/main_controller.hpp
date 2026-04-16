#pragma once
#include "crow_all.h" 
#include "models.hpp"
#include "repo.hpp" 
#include <optional>

class MainController{
public:
    static crow::response index(const std::string& conn_str, std::optional<Users> user = std::nullopt, std::optional<History> current_lecture = std::nullopt) {
        crow::mustache::context ctx;
        if (user.has_value()) {
            ctx["is_auth"] = true;
            ctx["user_name"] = user->name;
            ctx["user_role"] = user->role;

            HistoryRepository historyRepo(conn_str);

            auto historyUser = historyRepo.getUserHistoryWithLimit(user->id, 15);

            for (size_t i = 0; i < historyUser.size(); i++) {
                ctx["user_history"][i]["title"] = historyUser[i].title;
                ctx["user_history"][i]["id_history"] = historyUser[i].id_history;
            }

            if (current_lecture.has_value()) {
                ctx["has_content"] = true;
                ctx["current_title"] = current_lecture->title;
                ctx["current_content"] = current_lecture->content;
                ctx["current_id"] = current_lecture->id_history;
            } else {
                ctx["has_content"] = false;
            }

        } else {
            ctx["is_auth"] = false;
            ctx["has_content"] = false;
        }

        auto page = crow::mustache::load("index.html");
        return crow::response(page.render(ctx));
    }

    static crow::response filter_history(const Users& user, int limit, const std::string& db_conn) {
            crow::mustache::context ctx;
            
            ctx["is_auth"] = true;
            ctx["user_name"] = user.name;
            ctx["user_role"] = user.role;

            HistoryRepository historyRepo(db_conn);
            auto history = historyRepo.getUserHistoryWithLimit(user.id, limit);

            for (size_t i = 0; i < history.size(); i++) {
                ctx["user_history"][i]["title"] = history[i].title;
                ctx["user_history"][i]["id_history"] = history[i].id_history;
            }

            ctx["has_content"] = false; 
            ctx["filter_message"] = "Показано последних записей: " + std::to_string(limit);

            return crow::mustache::load("index.html").render(ctx);
    }
    static crow::response save_history(const crow::request& req, const std::string& db_conn, int user_id) {
         auto body = crow::json::load(req.body);
        
        if (!body || !body.has("title") || !body.has("content")) {
            return crow::response(400, "Invalid JSON: missing title or content");
        }

        std::string title = body["title"].s();
        std::string content = body["content"].s();

        try {
            HistoryRepository repo(db_conn);
 
            if (body.has("id_history") && body["id_history"].t() == crow::json::type::Number) {
                int history_id = body["id_history"].i();
                repo.updateRecord(history_id, user_id, content);
            } else {
                repo.addRecord(user_id, title, content);
            }

            crow::json::wvalue res;
            res["status"] = "success";
            return crow::response(200, res);
        } catch (const std::exception& e) {
            return crow::response(500, std::string("DB Error: ") + e.what());
        }
    }
};