#pragma once
#include "crow_all.h" 
#include "repo.hpp"

class AdminController{
    public:
        static crow::response show_form(std::optional<Users>& user_opt) {
            if (!user_opt.has_value()) {
                return crow::response(403, "Доступ запрещен");
            }

            crow::mustache::context ctx;
            ctx["admin_name"] = user_opt->name;
            auto page = crow::mustache::load("admin.html");
            return crow::response(page.render(ctx));
    }
};