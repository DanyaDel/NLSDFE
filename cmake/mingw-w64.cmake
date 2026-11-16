# Кросс-сборка под Windows из Linux через MinGW-w64:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${MINGW_TRIPLE}-windres)

# Библиотеки и заголовки ищем только в окружении MinGW, программы (glslc) — на хосте
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# libgcc, libstdc++ и winpthread вшиваются в .exe: рядом не нужны DLL от MinGW
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

# Тесты (ctest) запускаются через wine, если он есть
find_program(WINE_EXECUTABLE wine)
if(WINE_EXECUTABLE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE_EXECUTABLE})
endif()
