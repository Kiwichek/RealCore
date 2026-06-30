#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

class asIScriptEngine;
class asIScriptModule;
class asIScriptContext;

class ScriptEngine {
public:
    ScriptEngine() = default;
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    bool init();
    void shutdown();

    bool loadScript(const std::string& scriptPath);
    bool loadScriptFromSource(const std::string& moduleName, const std::string& source);
    void unloadScript(const std::string& moduleName);
    std::string moduleNameForPath(const std::string& scriptPath) const;

    void callFunction(const std::string& moduleName, const std::string& funcName);
    void callFunctionFloat(const std::string& moduleName, const std::string& funcName, float value);
    void callFunctionOnObject(const std::string& moduleName, const std::string& objType, void* obj, const std::string& funcName);

    asIScriptEngine* engine() { return m_engine; }

private:
    void registerTypes();
    bool registerValueTypes();
    bool registerObjectTypes();
    bool registerSystemTypes();
    void registerEngineAPI();

    asIScriptEngine* m_engine = nullptr;
    std::unordered_map<std::string, asIScriptModule*> m_modules;
};
