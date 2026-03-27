#pragma once
#include "crow_all.h" 
#include <string>

static std::string get_cookie_value(const crow::request& req, const std::string& key) {
    std::string cookie_header = req.get_header_value("Cookie");
    if (cookie_header.empty()) return "";

    size_t pos = cookie_header.find(key + "=");
    if (pos == std::string::npos) return "";

    size_t start = pos + key.length() + 1;
    size_t end = cookie_header.find(";", start);
    
    return cookie_header.substr(start, end - start);
}