cmake_minimum_required(VERSION 3.5)

project(event_recorder)

set(CMAKE_CXX_STANDARD 17)

find_package(MetavisionSDK COMPONENTS core stream ui REQUIRED)

set (sample event_recorder)
add_executable(${sample} main.cpp)
target_link_libraries(${sample} MetavisionSDK::core MetavisionSDK::stream MetavisionSDK::ui)