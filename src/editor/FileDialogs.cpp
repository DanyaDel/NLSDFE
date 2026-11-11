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
#else
#include <cstdio>
#include <cstdlib>
#include <vector>

// На Linux/macOS родного диалога у GLFW нет — зовём zenity или kdialog
namespace {
    bool commandExists(const char* name) {
        std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    }

    std::string shellQuote(const std::string& s) {
        std::string out = "'";
        for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
        return out + "'";
    }

    std::string runDialog(const std::string& cmd) {
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::string();
        std::string result;
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
        int status = pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
        return status == 0 ? result : std::string();
    }

    // Фильтр в формате Win32: "Описание\0*.ext\0Описание\0*.*\0\0"
    std::vector<std::pair<std::string, std::string>> parseFilter(const char* filter) {
        std::vector<std::pair<std::string, std::string>> out;
        while (filter && *filter) {
            std::string desc = filter; filter += desc.size() + 1;
            if (!*filter) break;
            std::string pattern = filter; filter += pattern.size() + 1;
            if (pattern == "*.*") pattern = "*";
            for (char& c : pattern) if (c == ';') c = ' ';   // В Win32 маски разделяет ';', в zenity — пробел
            out.push_back({desc, pattern});
        }
        return out;
    }

    enum class DialogKind { Open, Save, Folder };

    std::string nativeDialog(DialogKind kind, const char* filter, const std::string& initialDir, const std::string& defaultName) {
        std::string start = initialDir.empty() ? std::string() : initialDir + "/";
        if (kind == DialogKind::Save) start += defaultName;
        auto filters = parseFilter(filter);

        if (commandExists("zenity")) {
            std::string cmd = "zenity --file-selection";
            if (kind == DialogKind::Save) cmd += " --save --confirm-overwrite";
            if (kind == DialogKind::Folder) cmd += " --directory";
            if (!start.empty()) cmd += " --filename=" + shellQuote(start);
            for (const auto& f : filters) cmd += " --file-filter=" + shellQuote(f.first + " | " + f.second);
            return runDialog(cmd + " 2>/dev/null");
        }
        if (commandExists("kdialog")) {
            std::string cmd = "kdialog ";
            cmd += (kind == DialogKind::Save) ? "--getsavefilename " : (kind == DialogKind::Folder) ? "--getexistingdirectory " : "--getopenfilename ";
            cmd += shellQuote(start.empty() ? "." : start);
            if (kind != DialogKind::Folder && !filters.empty()) cmd += " " + shellQuote(filters[0].second + "|" + filters[0].first);
            return runDialog(cmd + " 2>/dev/null");
        }
        fprintf(stderr, "No file dialog available: install zenity or kdialog\n");
        return std::string();
    }
}
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
    return std::string();
#else
    return nativeDialog(DialogKind::Open, filter, "", "");
#endif
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
    return std::string();
#else
    return nativeDialog(DialogKind::Save, filter, initialDir, "scene.nos");
#endif
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
    return std::string();
#else
    return nativeDialog(DialogKind::Folder, nullptr, "", "");
#endif
}
