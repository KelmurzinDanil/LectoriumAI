#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>
#include <mutex>

#include "crow_all.h"
#include "main_controller.hpp"
#include "controller_login.hpp"
#include "admin_controller.hpp"
#include "code_first.hpp"
#include "cookie_helpers.hpp"
#include "models.hpp"
#include "silero.hpp"
#include "whisper.h"

whisper_context* g_whisper_ctx = whisper_init_from_file_with_params("models/ggml-small.bin", whisper_context_default_params());

std::unordered_set<crow::websocket::connection*> g_active_connections;
std::mutex g_connections_mtx;

struct AudioSession {
    std::vector<float> accumulation_buffer;
    std::vector<float> speech_to_transcribe;
    int silence_counter = 0;

    std::shared_ptr<std::atomic<bool>> is_alive;
    SileroVAD personal_vad{"models_ai/silero_vad.onnx"};
};


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

    CROW_ROUTE(app, "/ws/audio")
        .websocket()
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "Новое соединение для аудио";

        {
            std::lock_guard<std::mutex> lock(g_connections_mtx);
            g_active_connections.insert(&conn);
        }

        AudioSession* audio_session = new AudioSession();
        conn.userdata(audio_session);
        })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
            if (!is_binary) return;
            AudioSession* session_ptr = static_cast<AudioSession*>(conn.userdata());

            if (!session_ptr) return;

            const float* raw_ptr = reinterpret_cast<const float*>(data.data());
            size_t num_samples = data.size() / sizeof(float);

            session_ptr -> accumulation_buffer.insert(
                session_ptr -> accumulation_buffer.end(),
                raw_ptr,
                raw_ptr + num_samples
            );

            while (session_ptr->accumulation_buffer.size() >= 512) {
                std::vector<float> vad_chunk(
                    session_ptr->accumulation_buffer.begin(), 
                    session_ptr->accumulation_buffer.begin() + 512
                );
                session_ptr->accumulation_buffer.erase(
                    session_ptr->accumulation_buffer.begin(), 
                    session_ptr->accumulation_buffer.begin() + 512
                );

                float prob = session_ptr -> personal_vad.predict(vad_chunk);

                if(prob > 0.5){
                    session_ptr -> speech_to_transcribe.insert(
                        session_ptr->speech_to_transcribe.end(), 
                        vad_chunk.begin(), 
                        vad_chunk.end()
                    );

                    session_ptr -> silence_counter = 0;
                } else {
                    session_ptr->silence_counter++;
                    if (session_ptr->silence_counter > 20 && !session_ptr->speech_to_transcribe.empty()) {
                        
                        std::vector<float> audio_to_process = session_ptr->speech_to_transcribe;
                        crow::websocket::connection* conn_ptr = &conn;                   
                        
                        session_ptr->speech_to_transcribe.clear();
                        session_ptr->silence_counter = 0;
                        session_ptr->personal_vad.reset_states();

                        std::thread([audio_to_process, conn_ptr](){
                            whisper_state* wstate = whisper_init_state(g_whisper_ctx);

                            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                            wparams.language  = "ru"; 
                            wparams.n_threads = 4;
                            wparams.print_progress = false;

                            if (whisper_full_with_state(g_whisper_ctx, wstate, wparams, audio_to_process.data(),
                             audio_to_process.size()) == 0){
                                std::string result_text = "";
                                int n_segments = whisper_full_n_segments_from_state(wstate);
                                for (int i = 0; i < n_segments; ++i) {
                                    result_text += whisper_full_get_segment_text_from_state(wstate, i);
                                }

                                std::string json = "{\"status\": \"done\", \"text\": \"" + result_text + "\"}";
                                {
                                    std::lock_guard<std::mutex> lock(g_connections_mtx);
                                    if (g_active_connections.find(conn_ptr) != g_active_connections.end()) {
                                        conn_ptr->send_text(json); 
                                    } else {
                                        //Ничего не делаем.
                                    }
                                }
                            } 
                            whisper_free_state(wstate);
                        }).detach();
                    }
                }
                
            }
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
        CROW_LOG_INFO << "Соединение закрыто";
        {
            std::lock_guard<std::mutex> lock(g_connections_mtx);
            g_active_connections.erase(&conn);
        }

        AudioSession* session_ptr = static_cast<AudioSession*>(conn.userdata());
        if (session_ptr) {
            delete session_ptr; 
            conn.userdata(nullptr); 
        }
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