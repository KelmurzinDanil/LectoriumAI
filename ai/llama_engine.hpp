#pragma once

#include "crow_all.h"
#include "httplib.h" 

#include <string>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h> 

class LlamaEngine {
private:
    pid_t server_pid = -1; 

public:
    LlamaEngine(const std::string& binary_path, const std::string& model_path) {
        std::cout << "[LLAMA] Запуск фонового микросервиса (порт 8082)..." << std::endl;

        std::string cmd = binary_path + " -m " + model_path + 
                        " -c 4096 "          
                        "-t 7 "             
                        "-tb 8 "             
                        "--port 8082 "       
                        "-np 1 "             
                        "--mlock "                        
                        "--no-mmap "        
                        "> /dev/null 2> errors.log & echo $!";

        FILE* pipe = popen(cmd.c_str(), "r");
        if(pipe){
            char buffer[128];
            if(fgets(buffer, sizeof(buffer), pipe)){
                server_pid = std::stoi(buffer);
            }
            pclose(pipe);
        }

        if (server_pid <= 0) {
            std::cerr << "[LLAMA] КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить бинарник сервера!" << std::endl;
            return;
        }

        std::cout << "[LLAMA] Ожидание готовности нейросети..." << std::flush;
        httplib::Client cli("127.0.0.1", 8082);
        cli.set_connection_timeout(1, 0);
        
        bool is_ready = false;
        for (int i = 0; i < 30; ++i) { 
            if (auto res = cli.Get("/health")) {
                if (res->status == 200) {
                    is_ready = true;
                    break;
                }
            }
            std::cout << "." << std::flush;
            sleep(1);
        }
        
        if (is_ready) {
            std::cout << "\n[LLAMA] Сервер готов к обработке текста!" << std::endl;
        } else {
            std::cerr << "\n[LLAMA] ОШИБКА: Сервер не ответил. Проверьте errors.log" << std::endl;
        }
    }

    ~LlamaEngine() {
        if (server_pid > 0) {
            std::cout << "[LLAMA] Остановка фонового сервера (PID: " << server_pid << ")" << std::endl;
            std::string kill_cmd = "kill " + std::to_string(server_pid);
            system(kill_cmd.c_str());
        }
    }

    std::string process_text(const std::string& raw_whisper_text, const std::string& previous_context) {
        httplib::Client cli("127.0.0.1", 8082);
        cli.set_connection_timeout(5, 0); 
        cli.set_read_timeout(30, 0);    

        std::string prompt = 
        "<|im_start|>system\n"
        "You are an expert academic assistant creating real-time lecture notes for a student. "
        "You will receive raw, messy speech-to-text transcriptions with errors, hesitations, and background noise tags.\n"
        "RULES:\n"
        "1. SYNTHESIZE & IMPROVE: Do not just correct typos. Understand the core meaning and rewrite it into 1-2 clear, professional academic sentences.\n"
        "2. MARKDOWN: Bold **key terms**. Use bullet points (-) for lists.\n"
        "3. MATH: Convert spoken equations into LaTeX wrapped in $ (e.g., $u = \\frac{s}{1}$).\n"
        "4. NOISE FILTER: If the input is pure noise, meaningless tags (like [Music], *sigh*), or fragmented garbage. Delete this tags\n"
        "5. NO CHAT: Do not say 'Here are the notes'. Output ONLY the markdown note.<|im_end|>\n";

        if (!previous_context.empty()) {
            prompt += "<|im_start|>user\n"
                    "[CONTEXT FROM PREVIOUS MINUTES]:\n" 
                    + previous_context + "\n"
                    "Use this context to understand acronyms or missing subjects in the next phrase.<|im_end|>\n";
        }

        prompt += "<|im_start|>user\n"
                "[RAW SPEECH CHUNK TO PROCESS]:\n" 
                + raw_whisper_text + "<|im_end|>\n"
                "<|im_start|>assistant\n";

        crow::json::wvalue req_json;
        req_json["prompt"] = prompt;
        req_json["n_predict"] = 256; 
        req_json["temperature"] = 0.3;

        auto res = cli.Post("/completion", req_json.dump(), "application/json");

        if (res && res->status == 200) {
            try {
                auto res_json = crow::json::load(res->body);
                if (res_json && res_json.has("content")) {
                    return res_json["content"].s();
                }
            } catch (...) {

            }
        }
        
        return raw_whisper_text;
    }
};