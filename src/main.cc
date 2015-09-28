#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <functional>

#include <windows.h>
#include <ShlObj.h>

static std::map<std::string, std::string> g_variables;
typedef std::vector<std::string> func_args;
static std::map<std::string, std::function<std::string(func_args& args)>> g_functions;

bool is_64bit() {
    BOOL b64;
    return IsWow64Process(GetCurrentProcess(), &b64)
        && b64 != FALSE;
}

void initialize_globals() {
    auto initialize_variables = []() {
        char path[MAX_PATH];

        // the exe directory
        if (GetModuleFileName(NULL, path, _countof(path)) > 0) {
            *strrchr(path, '\\') = '\0';
            g_variables["exe_dir"] = path;
        }

        // the Windows directory
        path[GetWindowsDirectory(path, _countof(path))] = '\0';
        if (path[3] == '\0') path[2] = '\0'; // on root drive, removes backslash
        g_variables["windows"] = path;

        // the Current Directory
        path[GetCurrentDirectory(_countof(path), path)] = '\0';
        if (path[3] == '\0') path[2] = '\0'; // on root drive, removes backslash
        g_variables["cd"] = path;

        // the System directory
        path[GetSystemDirectory(path, _countof(path))] = '\0';
        if (path[3] == '\0') path[2] = '\0'; // on root drive, removes backslash
        g_variables["system"] = path;

        // the AppData
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path)))
            g_variables["appdata"] = path;

        // the home
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, path)))
            g_variables["home"] = path;

        // the desktop
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, path)))
            g_variables["desktop"] = path;

        // the Program Files (x86)
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, 0, path)))
            g_variables["program_x86"] = path;

        // the Program Files
        if (GetEnvironmentVariable("ProgramW6432", path, _countof(path)) > 0)
            g_variables["program"] = path;
    };

    auto initialize_functions = []() {
        g_functions["reg"] = [](func_args& args)->std::string {
            std::string result;
            if (args.size() >= 3) {
                static std::map<std::string, HKEY> _hkeys{
                    { "HKEY_CLASS_ROOT", HKEY_CLASSES_ROOT },
                    { "HKCR", HKEY_CLASSES_ROOT },
                    { "HKEY_CURRENT_USER", HKEY_CURRENT_USER },
                    { "HKCU", HKEY_CURRENT_USER },
                    { "HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },
                    { "HKLM", HKEY_LOCAL_MACHINE },
                };

                if (_hkeys.count(args[0])) {
                    char value[2048];
                    DWORD cb = sizeof(value);

                    REGSAM sam = KEY_READ;
                    if (is_64bit()) {
                        if (args.size() >= 4 && args[3] == "64")
                            sam |= KEY_WOW64_64KEY;
                        else
                            sam |= KEY_WOW64_32KEY;
                    }

                    HKEY hkey;
                    if (RegOpenKeyEx(_hkeys[args[0]], NULL, 0, sam, &hkey) == ERROR_SUCCESS) {
                        if (RegGetValue(hkey, args[1].c_str(), args[2].c_str(),
                            RRF_RT_REG_SZ, nullptr, (void*)value, &cb) == ERROR_SUCCESS) {
                            result = value;
                        }
                        RegCloseKey(hkey);
                    }
                }
            }
            return result;
        };

        g_functions["env"] = [](func_args& args)->std::string {
            std::string result;
            if (args.size() >= 1) {
                char buf[2048];
                if (GetEnvironmentVariable(args[0].c_str(), buf, _countof(buf)) > 0) {
                    result = buf;
                }
            }

            return result;
        };

        g_functions["app_path"] = [](func_args& args)->std::string {
            std::string result;
            auto reg = g_functions["reg"];

            if (args.size() >= 1) {
                func_args as{
                    "HKLM",
                    "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + args[0],
                    ""
                };

                result = reg(as);
            }

            return result;
        };
    };

    initialize_variables();
    initialize_functions();
}

std::string expand_variable(const std::string& var) {
    if (g_variables.count(var) > 0)
        return g_variables[var];

    return "";
}

std::string expand_function(const std::string& fn, func_args& args) {
    if (g_functions.count(fn))
        return g_functions[fn](args);

    return "";
}

std::string expand(const std::string& raw) {
    std::string result;
    const char* p = raw.c_str();

    auto read_arg = [](const char*& p) {
        std::string s;
        while (*p && *p != ',' && *p != ')') {
            s += *p;
            p++;
        }

        return s;
    };

    auto read_ident = [](const char*& p) {
        std::string s;
        while (::isalnum(*p) || strchr("_", *p)) {
            s += *p;
            p++;
        }

        return s;
    };

    while (*p != '\0') {
        if (*p == '$') {
            p++;
            if (*p == '{') {
                p++;
                auto var = read_ident(p);
                if (*p++ != '}') {
                    // assert(0);
                }

                result += expand_variable(var);
            }
            else if (::isalpha(*p)) {
                auto fn = read_ident(p);
                if (*p++ != '(') {
                    // assert(0);
                }

                func_args args;
                while ((true)) {
                    std::string arg(read_arg(p));
                    args.push_back(arg);

                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    else if (*p == ')') {
                        break;
                    }
                }

                if (*p++ != ')') {
                    // assert(0);
                }

                result += expand_function(fn, args);
            }
            else {
                // assert(0);
                p++;
            }
        }
        else {
            result += *p;
            p++;
        }
    }
    return result;
}

int main() {
    const char* paths[] = {
        "${exe_dir}",
        "${windows}",
        "${cd}",
        "${system}",
        "${appdata}",
        "${home}",
        "${program}",
        "${program_x86}",
        "${desktop}",
        "${windows}\\explorer.exe",
        "$reg(HKLM,Software\\Vim\\Gvim,path,64)",
        "$env(USERPROFILE)",
        "$app_path(firefox.exe)",
    };

    initialize_globals();

    for (auto& path : paths) {
        std::cout << expand(path) << std::endl;
    };

    return 0;
}