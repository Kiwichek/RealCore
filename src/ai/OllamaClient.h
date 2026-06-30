#pragma once

#include <string>
#include <vector>

struct OllamaModel {
    std::string name;
    std::string displayName;
};

class OllamaClient {
public:
    static bool listModels(const std::string& baseUrl, std::vector<OllamaModel>& models, std::string& error);
    static bool generateScriptHelp(
        const std::string& baseUrl,
        const std::string& modelName,
        const std::string& userRequest,
        const std::string& currentScript,
        std::string& response,
        std::string& error);
    static bool generateEngineHelp(
        const std::string& baseUrl,
        const std::string& modelName,
        const std::string& userRequest,
        const std::string& engineContext,
        const std::string& currentScript,
        std::string& response,
        std::string& error);
};
