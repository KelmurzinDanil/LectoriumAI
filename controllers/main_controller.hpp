#pragma once
#include "crow_all.h" 
#include "models.hpp"
#include "repo.hpp" 
#include <optional>

class MainController{
public:
    static crow::response index(const std::string& conn_str, std::optional<Users> user = std::nullopt) {
          crow::mustache::context ctx;
          if (user.has_value()) {
            ctx["is_auth"] = true;
            ctx["user_name"] = user->name;
            ctx["user_role"] = user->role;

            HistoryRepository historyRepo(conn_str);
            auto  historyUser = historyRepo.getUserHistory(user->id);

            for (size_t i = 0; i < historyUser.size(); i++) {
            ctx["user_history"][i]["title"] = historyUser[i].title;
            ctx["user_history"][i]["id_history"] = historyUser[i].id_history;
            }
            ctx["has_content"] = false;

        } else {
            ctx["is_auth"] = false;
        }

        auto page = crow::mustache::load("index.html");
        return crow::response(page.render(ctx));
    }
};