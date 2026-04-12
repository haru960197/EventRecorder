cmake_minimum_required(VERSION 3.5)

project(event_recorder)

set(CMAKE_CXX_STANDARD 17)

find_package(MetavisionSDK COMPONENTS core stream ui REQUIRED)

set (sample event_recorder)
add_executable(${sample} main.cpp)
target_link_libraries(${sample} MetavisionSDK::core MetavisionSDK::stream MetavisionSDK::ui)

set (bench_sample camera_bench)
add_executable(${bench_sample} scripts/camera-bench.cpp)
target_link_libraries(${bench_sample} MetavisionSDK::core MetavisionSDK::stream MetavisionSDK::ui)

set (player_sample events_player)
add_executable(${player_sample} scripts/events-player.cpp)
target_link_libraries(${player_sample} MetavisionSDK::core MetavisionSDK::stream MetavisionSDK::ui)
