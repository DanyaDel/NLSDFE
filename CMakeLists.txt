cmake_minimum_required(VERSION 3.20)
project(SDF_Engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Находим Vulkan SDK
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

# Собираем список исходников ImGui + бэкенды (Vulkan + GLFW)
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

# Основные файлы проекта
set(PROJECT_SOURCES
    main.cpp
    EditorUI.cpp
    EngineBridge.cpp
    Scene.cpp
    Input.cpp
    Globals.cpp
    FileDialogs.cpp
)

add_executable(SDF_Engine ${PROJECT_SOURCES} ${IMGUI_SOURCES})

# Инклюды: Текущая папка (для своих .h), ImGui и бэкенды
target_include_directories(SDF_Engine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
)

# Линковка основных библиотек
target_link_libraries(SDF_Engine PRIVATE Vulkan::Vulkan glfw)

# --- ВАЖНО: Линковка системных библиотек Windows ---
# Без этого функции открытия файлов (GetOpenFileName) и COM (CoInitialize) не скомпилируются
if(WIN32)
    target_link_libraries(SDF_Engine PRIVATE 
        comdlg32  # Диалоги открытия/сохранения файлов
        ole32     # Работа с COM (Drag-n-Drop, Dialogs)
        shell32   # Выбор папок (SHBrowseForFolder)
    )
endif()

# --- Компиляция шейдера ---
find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(GLSLC_EXECUTABLE)
    # Создаем папку build/Debug или build/Release, куда упадет shader.spv
    # Но лучше, чтобы shader.spv лежал рядом с exe.
    # Этот скрипт компилирует шейдер при каждой сборке.
    add_custom_command(
        TARGET SDF_Engine PRE_BUILD
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/shader.comp -o ${CMAKE_BINARY_DIR}/shader.spv
        COMMENT "Compiling Compute Shader to ${CMAKE_BINARY_DIR}/shader.spv"
    )
else()
    message(WARNING "glslc not found! You must compile shaders/shader.comp manually to shader.spv")
endif()