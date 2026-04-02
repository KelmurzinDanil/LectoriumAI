#pragma once
#include "crow_all.h" 
#include "repo.hpp"
class ControllerLogin{
public:
    static crow::response show_form(const std::string& error_msg = "") {
        crow::mustache::context ctx;
        
        if (!error_msg.empty()) {
            ctx["has_error"] = true;       
            ctx["error_text"] = error_msg; 
        }

        auto page = crow::mustache::load("auth.html");
        crow::response res(page.render(ctx));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    }
    static crow::response handle_login(const crow::request& req, const std::string& conn_str){
        std::string body_query = "?" + req.body;
        crow::query_string params(body_query);

        const char* row_email = params.get("email");
        const char* row_password = params.get("password");
        std::string email = row_email ? row_email : "";
        std::string password = row_password ? row_password : "";

        UserRepository userRepo(conn_str); 
        auto user = userRepo.authenticate(email, password);

        if(user){
            crow::response res(302);
            res.add_header("Location", "/");
            std::string cookie_value = "user_id=" + std::to_string(user->id) + "; Path=/";
            res.add_header("Set-Cookie", cookie_value);
            
            return res;
        } else {
            return show_form("Неверный логин или пароль. Попробуйте еще раз.");
        }
        
    }
    static crow::response handle_register(const crow::request& req, const std::string& db_conn) {
        std::string body_query = "?" + req.body;
        crow::query_string params(body_query);
        
        Users newUser;
        const char* row_name = params.get("name");
        const char* row_email = params.get("email");
        const char* row_password = params.get("password");
        
        newUser.name = row_name ? row_name : "";
        newUser.login = row_email ? row_email : ""; 
        newUser.password = row_password ? row_password : "";
        
        UserRepository userRepo(db_conn);
        
        if (userRepo.save(newUser)) {
            crow::response res(302);
            res.add_header("Location", "/auth#login"); 
            return res;
        } else {
            return show_form("Такой Email уже зарегистрирован в системе.");
        }
    }
};
