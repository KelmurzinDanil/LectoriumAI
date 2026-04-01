#pragma once
#include <iostream>
#include <pqxx/pqxx> 

class DatabseManger{
    public:
        static void init(std::string conn_str){
        try {
            pqxx::connection C(conn_str);
            pqxx::work W(C);

            W.exec(R"(
                CREATE TABLE IF NOT EXISTS schema_history (
                    version INTEGER PRIMARY KEY,
                    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                );
            )");

            pqxx::result r = W.exec("SELECT version FROM schema_history ORDER BY version DESC LIMIT 1");
            int current_version = r.empty() ? 0 : r[0][0].as<int>();

            std::cout << "Current database version: v" << current_version << std::endl;

            if (current_version < 1) {
                W.exec(R"(
                    CREATE TABLE IF NOT EXISTS users(
                    id SERIAL PRIMARY KEY,
                    name TEXT NOT NULL,
                    login TEXT NOT NULL,
                    password TEXT NOT NULL);)");
                
                W.exec(R"(
                    CREATE TABLE IF NOT EXISTS history (
                    id_history SERIAL PRIMARY KEY,
                    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                    title TEXT NOT NULL,
                    content TEXT NOT NULL,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);)");

                W.exec("INSERT INTO schema_history (version) VALUES (1);");
                current_version = 1;
            }
            
            if (current_version < 2) {
                std::cout << "Applying Migration v2 (Adding Roles)..." << std::endl;
                

                W.exec("ALTER TABLE users ADD COLUMN role TEXT DEFAULT 'user';");

                // Пароль: admin 
                std::string admin_hash = "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918"; 
                
                W.exec_params(
                    "INSERT INTO users (name, login, password, role) VALUES ($1, $2, $3, $4) ON CONFLICT DO NOTHING;",
                    "System Admin", "admin@admin.com", admin_hash, "admin"
                );

                W.exec("INSERT INTO schema_history (version) VALUES (2);");
                current_version = 2;
            }

            W.commit(); 
            std::cout << "Database is up to date!" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Migration Error: " << e.what() << std::endl;
            throw; 
        }
    }
};