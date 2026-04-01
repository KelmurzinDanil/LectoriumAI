#include "crow_all.h"
#include "main_controller.hpp"
#include "controller_login.hpp"
#include "admin_controller.hpp"
#include "code_first.hpp"
#include "cookie_helpers.hpp"
#include "models.hpp"

int main() {
    std::string db_conn = "host=localhost port=5433 dbname=LectoriumDB user=devuser password=devpassword";
    DatabseManger::init(db_conn);

    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if (!userIdStr.empty()) {
        try {
            UserRepository repo(db_conn);
            auto user_opt = repo.getById(std::stoi(userIdStr));
            if (user_opt) { 
                Users u = user_opt.value();
                return MainController::index(db_conn, u); 
            }
        } catch (...) {
            return MainController::index(db_conn);
        }
    }

    return MainController::index(db_conn);
    });

    CROW_ROUTE(app, "/auth")([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if (!userIdStr.empty()) {
        try {
            UserRepository repo(db_conn);
            auto user_opt = repo.getById(std::stoi(userIdStr));
            if (user_opt) {
                crow::response res(302);
                res.add_header("Location", "/"); 
                return res;
            }
        } catch(...) {}
        }
  
        return ControllerLogin::show_form();
    });
    CROW_ROUTE(app, "/logout")([](){
        crow::response res(302);

        res.add_header("Location", "/auth#login");

        std::string dead_cookie = "user_id=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
        res.add_header("Set-Cookie", dead_cookie);
        return res;
    });
    CROW_ROUTE(app, "/admin")([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if(userIdStr.empty()){
            crow::response res(302);
            res.add_header("Location", "/auth"); 
            return res;
        }
        try {

            UserRepository repo(db_conn);
            auto user_opt = repo.getById(std::stoi(userIdStr));

            if (user_opt && user_opt->IsAdmin()) {
                return AdminController::show_form(user_opt);
            } else {
                return crow::response(403, "Доступ только для администраторов!");
            }
        } catch (...) {
            return crow::response(400, "Ошибка чтения куки");
        }

    });
    CROW_ROUTE(app, "/history/filter")([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if (userIdStr.empty()) {
            return crow::response(302, "/auth");
        }

        try {
            UserRepository userRepo(db_conn);
            auto user_opt = userRepo.getById(std::stoi(userIdStr));

            if (user_opt) {
                char* limit_param = req.url_params.get("limit");
                

                int limit = 5; 
                
                if (limit_param) {
                    try {
                        limit = std::stoi(limit_param); 
                        if (limit <= 0) limit = 5;     
                        if (limit > 50) limit = 50;     
                    } catch (...) {
                        
                    }
                }

                return MainController::filter_history(*user_opt, limit, db_conn);
            }
        } catch (...) {
            return crow::response(400, "Ошибка сессии");
        }

        return crow::response(302, "/auth");
    });

    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)([&db_conn](const crow::request& req){
        return ControllerLogin::handle_login(req, db_conn);
    });

    CROW_ROUTE(app, "/register").methods(crow::HTTPMethod::POST)([&db_conn](const crow::request& req){
        return ControllerLogin::handle_register(req, db_conn);
    });

    crow::mustache::set_base("templates");
    app.port(8081).multithreaded().run();
}