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

add_executable(SDF_Engine main.cpp Renderer.cpp EditorUI.cpp EngineBridge.cpp Scene.cpp Input.cpp Globals.cpp FileDialogs.cpp Settings.cpp Game.cpp ${IMGUI_SOURCES})

# Инклюды для ImGui
target_include_directories(SDF_Engine PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

if(TARGET Vulkan::Vulkan)
    target_link_libraries(SDF_Engine PRIVATE Vulkan::Vulkan glfw glm::glm spdlog::spdlog)
else()
    target_link_libraries(SDF_Engine PRIVATE Vulkan::Headers ${Vulkan_LIBRARIES} glfw glm::glm spdlog::spdlog)
endif()

# VMA include path (FetchContent target might not expose it directly depending on version)
target_include_directories(SDF_Engine PRIVATE ${vma_SOURCE_DIR}/include)

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
    add_custom_command(
        TARGET SDF_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_OUTPUTS} $<TARGET_FILE_DIR:SDF_Engine>
        COMMENT "Copying shaders to executable directory..."
    )
else()
    message(WARNING "glslc not found! Compile shaders manually (see README).")
endif()
