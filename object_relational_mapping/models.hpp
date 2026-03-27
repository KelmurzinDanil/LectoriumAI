#pragma once
#include <iostream>


struct Users {
    int id;
    std::string name;
    std::string login;
    std::string password;
    std::string role;

    bool IsAdmin() const { return role == "admin"; }
    bool isUser() const { return role == "user"; }
};

struct History{
    int id_history;
    int id_user;
    std::string title;
    std::string content;
    std::string created_at;
};

