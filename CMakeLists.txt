cmake_minimum_required(VERSION 3.20)
project(SDF_Engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan)

include(FetchContent)

if(NOT Vulkan_FOUND OR NOT Vulkan_INCLUDE_DIR)
    message(STATUS "Vulkan headers not found, fetching from GitHub...")
    FetchContent_Declare(
      vulkan_headers
      URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.275.zip
    )
    FetchContent_MakeAvailable(vulkan_headers)
    # Vulkan-Headers provides a header-only target: Vulkan::Headers
    # But we might need to define Vulkan_INCLUDE_DIR for older find_package logic or other uses
    get_target_property(Vulkan_INCLUDE_DIR Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)
    set(Vulkan_FOUND TRUE)
    set(Vulkan_LIBRARIES "vulkan") # Assume standard loader is available as libvulkan.so
endif()

# --- GLM ---
FetchContent_Declare(
  glm
  URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip
)
FetchContent_MakeAvailable(glm)

# --- VMA ---
FetchContent_Declare(
  vma
  URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.1.0.zip
)
FetchContent_MakeAvailable(vma)

# --- spdlog ---
FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.zip
)
FetchContent_MakeAvailable(spdlog)

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

# ImGui + бэкенды собираются один раз и подключаются к рендеру
set(IMGUI_DIR ${imgui_SOURCE_DIR})
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

if(TARGET Vulkan::Vulkan)
    set(NLS_VULKAN_LIBS Vulkan::Vulkan)
    set(NLS_VULKAN_HEADERS Vulkan::Headers)
    if(NOT TARGET Vulkan::Headers)
        set(NLS_VULKAN_HEADERS Vulkan::Vulkan)
    endif()
else()
    set(NLS_VULKAN_LIBS Vulkan::Headers ${Vulkan_LIBRARIES})
    set(NLS_VULKAN_HEADERS Vulkan::Headers)
endif()
target_link_libraries(imgui PUBLIC ${NLS_VULKAN_LIBS} glfw)

# =====================================================================
# Модули движка. Зависимости идут строго в одну сторону:
#   core <- scene <- physics, script <- render <- runtime <- (editor | player)
# Редактор не входит ни в одну библиотеку.
# =====================================================================

# core: типы, математика, настройки (только заголовки Vulkan/GLFW, без линковки)
add_library(nls_core STATIC src/core/Settings.cpp)
target_include_directories(nls_core PUBLIC src/core ${vma_SOURCE_DIR}/include $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(nls_core PUBLIC glm::glm spdlog::spdlog ${NLS_VULKAN_HEADERS})

# scene: граф сцены, SDF на CPU, файлы .nos, камера
add_library(nls_scene STATIC src/scene/Scene.cpp src/scene/Globals.cpp src/scene/Camera.cpp)
target_include_directories(nls_scene PUBLIC src/scene)
target_link_libraries(nls_scene PUBLIC nls_core)

# physics: твёрдые тела на SDF
add_library(nls_physics STATIC src/physics/Physics.cpp)
target_include_directories(nls_physics PUBLIC src/physics)
target_link_libraries(nls_physics PUBLIC nls_scene)

# script: язык NLS (компилятор, VM) и привязки к движку
add_library(nls_script STATIC
    src/script/Lexer.cpp
    src/script/Parser.cpp
    src/script/Compiler.cpp
    src/script/VM.cpp
    src/script/Bindings.cpp
    src/script/ScriptSystem.cpp
)
target_include_directories(nls_script PUBLIC src/script)
target_link_libraries(nls_script PUBLIC nls_scene nls_physics)

# render: Vulkan-рендерер и ImGui
add_library(nls_render STATIC src/render/Renderer.cpp)
target_include_directories(nls_render PUBLIC src/render)
target_link_libraries(nls_render PUBLIC nls_scene imgui ${NLS_VULKAN_LIBS} glfw)

# runtime: окно, игровой цикл, игрок
add_library(nls_runtime STATIC src/runtime/App.cpp src/runtime/Game.cpp)
target_include_directories(nls_runtime PUBLIC src/runtime)
target_link_libraries(nls_runtime PUBLIC nls_render nls_physics nls_script)

# Редактор
add_executable(SDF_Engine
    src/app/editor_main.cpp
    src/editor/EditorUI.cpp
    src/editor/EngineBridge.cpp
    src/editor/Input.cpp
    src/editor/FileDialogs.cpp
)
target_include_directories(SDF_Engine PRIVATE src/editor)
target_link_libraries(SDF_Engine PRIVATE nls_runtime)

# Плеер: движок без редактора
add_executable(NLSDFE_Player src/app/player_main.cpp)
target_link_libraries(NLSDFE_Player PRIVATE nls_runtime)

# Тесты без окна и GPU
add_executable(nls_tests tests/tests_main.cpp tests/test_scene.cpp tests/test_physics.cpp tests/test_script.cpp)
target_link_libraries(nls_tests PRIVATE nls_scene nls_physics nls_script)
enable_testing()
add_test(NAME nls_tests COMMAND nls_tests)

# Шейдеры: каждый .spv пересобирается только при изменении своего исходника
# и сразу кладётся рядом с исполняемым файлом (движок ищет их там)
find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(GLSLC_EXECUTABLE)
    set(SHADER_PAIRS
        "shader.comp|shader.spv"
        "composite.vert|composite_vert.spv"
        "composite.frag|composite_frag.spv"
        "mesh.vert|mesh_vert.spv"
        "mesh.frag|mesh_frag.spv"
    )
    set(SHADER_OUTPUTS "")
    foreach(PAIR ${SHADER_PAIRS})
        string(REPLACE "|" ";" PAIR_LIST ${PAIR})
        list(GET PAIR_LIST 0 SHADER_SRC)
        list(GET PAIR_LIST 1 SHADER_SPV)
        set(SHADER_OUT ${CMAKE_BINARY_DIR}/${SHADER_SPV})
        add_custom_command(
            OUTPUT ${SHADER_OUT}
            COMMAND ${GLSLC_EXECUTABLE} -O ${CMAKE_SOURCE_DIR}/shaders/${SHADER_SRC} -o ${SHADER_OUT}
            DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${SHADER_SRC}
            COMMENT "Compiling shader ${SHADER_SRC}"
        )
        list(APPEND SHADER_OUTPUTS ${SHADER_OUT})
    endforeach()
    add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(SDF_Engine Shaders)
    add_dependencies(NLSDFE_Player Shaders)
    add_custom_command(
        TARGET SDF_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_OUTPUTS} $<TARGET_FILE_DIR:SDF_Engine>
        COMMENT "Copying shaders to executable directory..."
    )
else()
    message(WARNING "glslc not found! Compile shaders manually (see README).")
endif()
