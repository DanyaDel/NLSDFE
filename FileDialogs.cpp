#include "FileDialogs.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
// expose native API for glfw before including native header
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>
#endif

std::string FileDialogs::OpenFile(GLFWwindow* window, const char* filter) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
#endif
    return std::string();
}

std::string FileDialogs::SaveFile(GLFWwindow* window, const char* filter, const std::string& initialDir) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!initialDir.empty()) {
        ofn.lpstrInitialDir = initialDir.c_str();
    }
    strncpy(szFile, "scene.nos", sizeof(szFile) - 1);
    if (GetSaveFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
#endif
    return std::string();
}

std::string FileDialogs::SelectFolder(GLFWwindow* window) {
#ifdef _WIN32
    char path[MAX_PATH];
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = "Select Default Export Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.hwndOwner = glfwGetWin32Window(window);
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
#endif
    return std::string();
}
