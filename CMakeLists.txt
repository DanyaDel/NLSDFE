cmake_minimum_required(VERSION 3.20)
project(SDF_Engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

include(FetchContent)

# --- GLFW ---
FetchContent_Declare(
  glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.3.8.zip
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui ---
FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.89.9.zip
)
FetchContent_MakeAvailable(imgui)

# Собираем список исходников ImGui + бэкенды
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

add_executable(SDF_Engine main.cpp EditorUI.cpp EngineBridge.cpp Scene.cpp Input.cpp Globals.cpp FileDialogs.cpp ${IMGUI_SOURCES})

# Инклюды для ImGui
target_include_directories(SDF_Engine PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

target_link_libraries(SDF_Engine PRIVATE Vulkan::Vulkan glfw)

find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(GLSLC_EXECUTABLE)
    add_custom_command(
        TARGET SDF_Engine PRE_BUILD
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/shader.comp -o ${CMAKE_BINARY_DIR}/shader.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/depth_prime.vert -o ${CMAKE_BINARY_DIR}/depth_prime_vert.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/depth_prime.frag -o ${CMAKE_BINARY_DIR}/depth_prime_frag.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/mesh.vert -o ${CMAKE_BINARY_DIR}/mesh_vert.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/mesh.frag -o ${CMAKE_BINARY_DIR}/mesh_frag.spv
    )
    add_custom_command(
        TARGET SDF_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/shader.spv
            ${CMAKE_BINARY_DIR}/depth_prime_vert.spv
            ${CMAKE_BINARY_DIR}/depth_prime_frag.spv
            ${CMAKE_BINARY_DIR}/mesh_vert.spv
            ${CMAKE_BINARY_DIR}/mesh_frag.spv
            $<TARGET_FILE_DIR:SDF_Engine>
        COMMENT "Copying shaders to executable directory..."
    )
else()
    message(WARNING "glslc not found! Compile shader manually.")
endif()