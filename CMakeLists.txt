cmake_minimum_required(VERSION 3.16)
project(LolStandings LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- GLFW (window + input) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)   # X11 works everywhere (incl. XWayland)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE)

# --- Dear ImGui (the GUI) ---
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.5
  GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw imgui)

find_package(OpenGL REQUIRED)

add_executable(LolStandings WIN32
  main.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(LolStandings PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)

target_link_libraries(LolStandings PRIVATE glfw OpenGL::GL)

# No console window on Windows, but keep a normal main()
if(MSVC)
  target_link_options(LolStandings PRIVATE /ENTRY:mainCRTStartup)
endif()
# Static runtime with MinGW so the .exe runs on machines without MinGW DLLs
if(MINGW)
  target_link_options(LolStandings PRIVATE -static)
endif()
