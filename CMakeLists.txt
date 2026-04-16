cmake_minimum_required(VERSION 3.15)
project(LectoriumAI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) 

add_subdirectory(vendor/whisper/whisper.cpp build_whisper)

add_executable(lectorium_app
    main.cpp 
    controllers/controller_login.hpp
    controllers/main_controller.hpp
    controllers/admin_controller.hpp
    object_relational_mapping/code_first.hpp
    object_relational_mapping/models.hpp
    object_relational_mapping/repo.hpp
    helpers/cookie_helpers.hpp
    ai/silero.hpp
    ai/llama_engine.hpp 
 )

target_include_directories(lectorium_app PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(lectorium_app PRIVATE ${CMAKE_SOURCE_DIR}/onnxruntime-linux-x64-1.24.4/include/)
target_include_directories(lectorium_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/controllers)
target_include_directories(lectorium_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/object_relational_mapping)
target_include_directories(lectorium_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/helpers)
target_include_directories(lectorium_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/models_ai)
target_include_directories(lectorium_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ai)
target_include_directories(lectorium_app PRIVATE ${PQXX_INCLUDE_DIRS})
target_include_directories(lectorium_app PRIVATE 
    vendor/whisper/whisper.cpp/include
    vendor/whisper/whisper.cpp/ggml/include
)

target_link_directories(lectorium_app PRIVATE ${CMAKE_SOURCE_DIR}/onnxruntime-linux-x64-1.24.4/lib/)


target_link_libraries(lectorium_app PRIVATE 
    onnxruntime 
    whisper 
    Threads::Threads 
    ${PQXX_LIBRARIES}
)

set_target_properties(lectorium_app PROPERTIES 
    INSTALL_RPATH "${CMAKE_SOURCE_DIR}/onnxruntime-linux-x64-1.24.4/lib/"
    BUILD_WITH_INSTALL_RPATH TRUE
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/models_ai DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()