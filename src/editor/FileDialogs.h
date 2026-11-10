#pragma once

#include <string>
#include <GLFW/glfw3.h>

class FileDialogs {
public:
    static std::string OpenFile(GLFWwindow* window, const char* filter);
    static std::string SaveFile(GLFWwindow* window, const char* filter, const std::string& initialDir = "");
    static std::string SelectFolder(GLFWwindow* window);
};
