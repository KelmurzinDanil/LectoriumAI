#pragma once
#include <iostream>
#include <pqxx/pqxx> 

class DatabseManger{
    public:
        static void init(std::string conn_str){
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            W.exec(R"(
                CREATE TABLE IF NOT EXISTS users(
                id SERIAL PRIMARY KEY,
                name TEXT NOT NULL,
                login TEXT NOT NULL,
                password TEXT NOT NULL,
                role TEXT DEFAULT 'user');)");
            
            W.exec(R"(
                CREATE TABLE IF NOT EXISTS history (
                id_history SERIAL PRIMARY KEY,
                user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                title TEXT NOT NULL,
                content TEXT NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);)");
            
            W.commit();
            std::cout << "Database initialized (Tables: users, history)." << std::endl;
        }
};