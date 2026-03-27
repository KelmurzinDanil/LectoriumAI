#pragma once
#include "crow_all.h" 
#include "repo.hpp"
class ControllerLogin{
public:
    static crow::response show_form() {
        auto page = crow::mustache::load("auth.html");
        return crow::response(page.render());
    }

    static crow::response handle_login(const crow::request& req, const std::string& conn_str){
        crow::query_string params(req.body);

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
            return crow::response(400, "Неверный логин или пароль");
        }
        
    }
    static crow::response handle_register(const crow::request& req, const std::string& db_conn) {
        crow::query_string params(req.body);
        
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
            return crow::response(400, "Такой пользователь уже существует");
        }
    }
};
