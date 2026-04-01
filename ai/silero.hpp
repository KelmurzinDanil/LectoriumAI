#pragma once

#include <iostream>
#include <vector>
#include <cstring>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <algorithm> 


class SileroVAD{
    private:
        Ort::Env env;
        Ort::SessionOptions session_options;
        std::shared_ptr<Ort::Session> session;
        Ort::MemoryInfo memory_info;

        

        std::vector<const char*> input_node_names = {"input", "state", "sr"};
        std::vector<const char*> output_node_names = {"output", "stateN"};

        const int64_t input_node_dims[2] = {1, 512};   // [1 батч, 512 семплов]
        const int64_t state_node_dims[3] = {2, 1, 128}; // [2 слоя, 1 батч, 128 фичей]
        const int64_t sr_node_dims[1] = {1};           // Одно число

        std::vector<float> _state;
        std::vector<int64_t> sr;
        const unsigned int size_state = 2 * 1 * 128; 

    public:
        SileroVAD(const std::string& model_path) : 
                        env(ORT_LOGGING_LEVEL_ERROR, "SileroVAD"),
                        memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU))
        {
            session_options.SetIntraOpNumThreads(1);
            session_options.SetInterOpNumThreads(1);
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            _state.resize(size_state, 0.0f); 
            sr = {16000};

            try {
                session = std::make_shared<Ort::Session>(env, model_path.c_str(), session_options);
                std::cout << "[SileroVAD] Модель успешно загружена: " << model_path << std::endl;
            } catch (const Ort::Exception& e) {
                std::cerr << "[SileroVAD] Ошибка загрузки модели: " << e.what() << std::endl;
                throw;
            }
        }

        ~SileroVAD() {}

        // принимает 512 float, возвращает от 0.0 до 1.0
        float predict(const std::vector<float>& data_chunk){
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                                                memory_info, 
                                                const_cast<float*>(data_chunk.data()),
                                                data_chunk.size(),
                                                input_node_dims, 2);

            Ort::Value state_tensor = Ort::Value::CreateTensor<float>(
                                                memory_info,
                                                _state.data(), 
                                                _state.size(),
                                                state_node_dims, 3);
            
            Ort::Value sr_tensor  = Ort::Value::CreateTensor<int64_t>(
                                                memory_info,
                                                sr.data(), 
                                                sr.size(),
                                                sr_node_dims, 1);
            
            std::vector<Ort::Value> ort_inputs;
            ort_inputs.push_back(std::move(input_tensor));
            ort_inputs.push_back(std::move(state_tensor));
            ort_inputs.push_back(std::move(sr_tensor));
            
            std::vector<Ort::Value> ort_outputs;
            ort_outputs = session->Run(
                                Ort::RunOptions{ nullptr },
                                input_node_names.data(), ort_inputs.data(), ort_inputs.size(),
                                output_node_names.data(), output_node_names.size());

            float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
            float* stateN = ort_outputs[1].GetTensorMutableData<float>();
            std::memcpy(_state.data(), stateN, size_state * sizeof(float));

            return speech_prob; 
        }

        void reset_states(){
            std::fill(_state.begin(), _state.end(), 0.0f);
        }
};