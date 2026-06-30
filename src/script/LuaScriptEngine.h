#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

struct lua_State;

class LuaScriptEngine {
public:
    LuaScriptEngine() = default;
    LuaScriptEngine(const LuaScriptEngine&) = delete;
    LuaScriptEngine& operator=(const LuaScriptEngine&) = delete;

    bool init();
    void shutdown();

    bool loadScript(const std::string& scriptPath);
    void unloadScript(const std::string& moduleName);
    std::string moduleNameForPath(const std::string& scriptPath) const;

    void callFunction(const std::string& moduleName, const std::string& funcName);
    void callFunctionFloat(const std::string& moduleName, const std::string& funcName, float value);

private:
    struct Module {
        lua_State* state = nullptr;
        std::string path;
    };

    void closeModule(Module& module);

    std::unordered_map<std::string, Module> m_modules;
};
