#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <deque>


#include "crow_all.h"
#include "main_controller.hpp"
#include "controller_login.hpp"
#include "admin_controller.hpp"
#include "code_first.hpp"
#include "cookie_helpers.hpp"
#include "models.hpp"
#include "silero.hpp"
#include "whisper.h"
#include "llama_engine.hpp"

whisper_context* g_whisper_ctx = nullptr; 
LlamaEngine* g_llama = nullptr;

std::unordered_set<crow::websocket::connection*> g_active_connections;
std::mutex g_connections_mtx;


struct AudioSession {
    std::vector<float> accumulation_buffer;
    std::vector<float> speech_to_transcribe;
    int silence_counter = 0;
    
    SileroVAD personal_vad{"models_ai/silero_vad.onnx"};
    
    std::deque<std::string> llm_context_words;
};

std::vector<std::string> split_into_words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

crow::response redirect_to(const std::string& url) {
    crow::response res(302);
    res.add_header("Location", url);
    return res;
}

int main() {

    g_whisper_ctx = whisper_init_from_file_with_params("models_ai/ggml-base.en-q5_1.bin", whisper_context_default_params());
    g_llama = new LlamaEngine(
        "../vendor/llama/llama.cpp/build_isolated/bin/llama-server", 
        "../models_ai/Bonsai-8B.gguf"
    );
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

    CROW_ROUTE(app, "/api/history/save").methods(crow::HTTPMethod::POST)([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        
        if (userIdStr.empty()) {
            return crow::response(401); 
        }

        try {
            int user_id = std::stoi(userIdStr);
            return MainController::save_history(req, db_conn, user_id);
        } catch (...) {
            return crow::response(400);
        }
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
            return redirect_to("/auth");
        }
        try {

            UserRepository repo(db_conn);
            auto user_opt = repo.getById(std::stoi(userIdStr));

            if (user_opt && user_opt->IsAdmin()) {
                return AdminController::show_form(user_opt);
            } else {
                return redirect_to("/"); 
            }
        } catch (...) {
            return redirect_to("/auth");
        }

    });
    CROW_ROUTE(app, "/history/filter")([&db_conn](const crow::request& req){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if (userIdStr.empty()) {
            return redirect_to("/auth");
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
            return redirect_to("/auth");
        }

        return redirect_to("/auth");
    });

    CROW_ROUTE(app, "/history/<int>")([&db_conn](const crow::request& req, int history_id){
        std::string userIdStr = get_cookie_value(req, "user_id");
        if (userIdStr.empty()) return redirect_to("/auth");

        try {
            int user_id = std::stoi(userIdStr);
            UserRepository userRepo(db_conn);
            auto user_opt = userRepo.getById(user_id);

            if (user_opt) {
                HistoryRepository historyRepo(db_conn);
                auto lecture_opt = historyRepo.getRecordById(history_id, user_id);

                if (lecture_opt) {
                    return MainController::index(db_conn, *user_opt, *lecture_opt);
                } else {
                    return redirect_to("/");
                }
            }
        } catch (...) {}
        
        return redirect_to("/auth");
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
                    if (session_ptr->silence_counter > 0 && !session_ptr->speech_to_transcribe.empty()) {
                        
                        std::vector<float> audio_to_process = session_ptr->speech_to_transcribe;
                        crow::websocket::connection* conn_ptr = &conn;                   
                        
                        std::string current_context = "";
                        for (const auto& word : session_ptr->llm_context_words) {
                            current_context += word + " ";
                        }

                        session_ptr->speech_to_transcribe.clear();
                        session_ptr->silence_counter = 0;
                        session_ptr->personal_vad.reset_states();

                        std::thread([audio_to_process, conn_ptr, current_context](){
                            whisper_state* wstate = whisper_init_state(g_whisper_ctx);

                            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                            wparams.language  = "en"; 
                            wparams.n_threads = 4;
                            wparams.print_progress = false;

                            if (whisper_full_with_state(g_whisper_ctx, wstate, wparams, audio_to_process.data(),
                             audio_to_process.size()) == 0){
                                std::string result_text = "";
                                int n_segments = whisper_full_n_segments_from_state(wstate);
                                for (int i = 0; i < n_segments; ++i) {
                                    result_text += whisper_full_get_segment_text_from_state(wstate, i);
                                }
                                std::string raw_text = result_text; 
                                std::string markdown_text =  g_llama -> process_text(raw_text, current_context);
                                crow::json::wvalue response_json;
                                response_json["status"] = "done";
                                response_json["text"] = markdown_text;


                                {
                                    std::lock_guard<std::mutex> lock(g_connections_mtx);
                                    if (g_active_connections.find(conn_ptr) != g_active_connections.end()) {
                                        AudioSession* safe_session = static_cast<AudioSession*>(conn_ptr->userdata());
                                        if(safe_session){
                                            if (safe_session) {
                                                std::vector<std::string> new_words = split_into_words(markdown_text);
                                                
                                                for (const auto& w : new_words) {
                                                    safe_session->llm_context_words.push_back(w);
                                                }

                                                const size_t MAX_CONTEXT_WORDS = 50;
                                                while (safe_session->llm_context_words.size() > MAX_CONTEXT_WORDS) {
                                                    safe_session->llm_context_words.pop_front();
                                                }
                                            }
                                        }
                                        conn_ptr->send_text(response_json.dump()); 


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

    delete g_llama;
}