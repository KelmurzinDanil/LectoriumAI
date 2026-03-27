#pragma once
#include <iostream>
#include <pqxx/pqxx>
#include <vector>
#include <models.hpp>
#include "picosha2.h"
class HistoryRepository {
    std::string conn_str;

    public:
        HistoryRepository(std::string s) : conn_str(s){}

        void addRecord(int userId, const std::string& title, const std::string& content){
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            W.exec_params("INSERT INTO history (user_id, title, content) VALUES ($1, $2, $3)",
                    userId, title, content);
            W.commit();
        }
        std::vector<History> getUserHistory(int userId){
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            pqxx::result R = W.exec_params(R"(
                SELECT id_history, user_id, title, content, created_at::text 
                FROM history WHERE user_id = $1 ORDER BY created_at DESC)", userId);
    
            std::vector<History> records;
            for(auto row: R){
                records.push_back({
                    row[0].as<int>(),
                    row[1].as<int>(),
                    row[2].as<std::string>(),
                    row[3].as<std::string>(),
                    row[4].as<std::string>()
                });
            }
        
            return records;
        }
};

class UserRepository{
    std::string conn_str;
    public:
        UserRepository(std::string s) : conn_str(s) {}
    
        bool save(const Users& u) { 
            if(existsByLogin(u.login)){
                return false;
            }
            std::string role = "user";
            pqxx::connection C(conn_str);
            pqxx::work W(C);
            std::string hashed_password = picosha2::hash256_hex_string(u.password);

            W.exec_params("INSERT INTO users (name, login, password, role) VALUES ($1, $2, $3, $4)",
                 u.name, u.login, hashed_password, role);
            W.commit();
            return true;
        }
        bool existsByLogin(std::string login) { 
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            pqxx::result R = W.exec_params("SELECT 1 FROM users WHERE login = $1", login);

            return !R.empty();
        }
        std::optional<Users> authenticate(std::string login, std::string password){
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            std::string hashed_password = picosha2::hash256_hex_string(password);
            pqxx::result R = W.exec_params("SELECT id, name, login, role FROM users WHERE login = $1 AND password = $2",
                 login, hashed_password);

            if(R.empty()){
                return std::nullopt;
            }

            Users u;
            u.id = R[0][0].as<int>();
            u.name = R[0][1].as<std::string>();
            u.login = R[0][2].as<std::string>();
            u.role = R[0][3].as<std::string>();

            return u;
        }
        std::optional<Users> getById(int id) {
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            pqxx::result R = W.exec_params(
                "SELECT id, name, login, role FROM users WHERE id = $1", id
            );

            if (R.empty()) {
                return std::nullopt;
            }

            Users u;
            u.id = R[0][0].as<int>();
            u.name = R[0][1].as<std::string>();
            u.login = R[0][2].as<std::string>();
            u.role = R[0][3].as<std::string>();
            
            
            return u;
        }
        
};