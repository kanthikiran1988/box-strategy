cmake_minimum_required(VERSION 3.14)
project(box_strategy VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

# Find required packages
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fmt/include
)

# Define source files
set(SOURCES
    src/main.cpp
    src/config/ConfigManager.cpp
    src/utils/Logger.cpp
    src/utils/HttpClient.cpp
    src/utils/ThreadPool.cpp
    src/models/InstrumentModel.cpp
    src/models/OrderModel.cpp
    src/models/BoxSpreadModel.cpp
    src/auth/AuthManager.cpp
    src/market/MarketDataManager.cpp
    src/market/ExpiryManager.cpp
    src/analysis/CombinationAnalyzer.cpp
    src/analysis/MarketDepthAnalyzer.cpp
    src/risk/RiskCalculator.cpp
    src/risk/FeeCalculator.cpp
    src/trading/OrderManager.cpp
    src/trading/PaperTrader.cpp
)

# Define the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    fmt::fmt
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES config.json DESTINATION etc/${PROJECT_NAME})

# Enable testing
#enable_testing()

# Add tests
#add_subdirectory(tests)