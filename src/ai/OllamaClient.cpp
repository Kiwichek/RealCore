#include <ai/OllamaClient.h>

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

struct OllamaBaseUrl {
    bool secure = false;
    std::wstring host;
    INTERNET_PORT port = 11434;
    std::string apiPrefix;
};

std::string trimCopy(std::string value) {
    while (!value.empty() && std::isspace((unsigned char)value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace((unsigned char)value.back())) {
        value.pop_back();
    }
    return value;
}

bool startsWith(const std::string& value, const char* prefix) {
    const std::string needle(prefix);
    return value.size() >= needle.size() && value.compare(0, needle.size(), needle) == 0;
}

bool endsWith(const std::string& value, const char* suffix) {
    const std::string needle(suffix);
    return value.size() >= needle.size() && value.compare(value.size() - needle.size(), needle.size(), needle) == 0;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), needed);
    return wide;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 32);
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
                break;
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parseHex4(const std::string& text, size_t pos, unsigned int& value) {
    if (pos + 4 > text.size()) return false;
    value = 0;
    for (size_t i = 0; i < 4; i++) {
        int digit = hexDigit(text[pos + i]);
        if (digit < 0) return false;
        value = (value << 4) | (unsigned int)digit;
    }
    return true;
}

void appendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        out += (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        out += (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        out += (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out += (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        out += (char)(0xF0 | ((codepoint >> 18) & 0x07));
        out += (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out += (char)(0x80 | (codepoint & 0x3F));
    }
}

bool readJsonString(const std::string& json, size_t quotePos, std::string& out, size_t* endPos = nullptr) {
    if (quotePos >= json.size() || json[quotePos] != '"') return false;

    out.clear();
    for (size_t i = quotePos + 1; i < json.size(); i++) {
        char c = json[i];
        if (c == '"') {
            if (endPos) *endPos = i + 1;
            return true;
        }
        if (c != '\\') {
            out += c;
            continue;
        }

        if (++i >= json.size()) return false;
        char e = json[i];
        switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned int codepoint = 0;
                if (!parseHex4(json, i + 1, codepoint)) return false;
                i += 4;

                if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                    i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u') {
                    unsigned int low = 0;
                    if (parseHex4(json, i + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                        i += 6;
                    }
                }

                appendUtf8(out, codepoint);
                break;
            }
            default:
                out += e;
                break;
        }
    }
    return false;
}

bool extractStringField(const std::string& json, const char* field, std::string& out) {
    std::string key = std::string("\"") + field + "\"";
    size_t keyPos = json.find(key);
    if (keyPos == std::string::npos) return false;
    size_t colon = json.find(':', keyPos + key.size());
    if (colon == std::string::npos) return false;
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;
    return readJsonString(json, quote, out);
}

size_t findMatchingObjectBrace(const std::string& json, size_t openBrace) {
    if (openBrace >= json.size() || json[openBrace] != '{') return std::string::npos;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = openBrace; i < json.size(); i++) {
        char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::string extractErrorMessage(const std::string& json) {
    std::string message;
    if (extractStringField(json, "error", message)) {
        return message;
    }
    if (extractStringField(json, "message", message)) {
        return message;
    }
    return {};
}

bool parseBaseUrl(const std::string& baseUrl, OllamaBaseUrl& parsed, std::string& error) {
    std::string url = trimCopy(baseUrl.empty() ? "http://localhost:11434" : baseUrl);
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    size_t offset = 0;
    if (startsWith(url, "http://")) {
        parsed.secure = false;
        offset = 7;
        parsed.port = 11434;
    } else if (startsWith(url, "https://")) {
        parsed.secure = true;
        offset = 8;
        parsed.port = INTERNET_DEFAULT_HTTPS_PORT;
    } else {
        parsed.secure = false;
        parsed.port = 11434;
    }

    std::string rest = url.substr(offset);
    size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string prefix = slash == std::string::npos ? std::string() : rest.substr(slash);

    if (authority.empty()) {
        error = "Ollama URL is missing a host.";
        return false;
    }

    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        std::string portText = authority.substr(colon + 1);
        authority = authority.substr(0, colon);
        char* end = nullptr;
        long portValue = std::strtol(portText.c_str(), &end, 10);
        if (!end || *end != '\0' || portValue <= 0 || portValue > 65535) {
            error = "Ollama URL has an invalid port.";
            return false;
        }
        parsed.port = (INTERNET_PORT)portValue;
    }

    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix == "/api") {
        prefix.clear();
    } else if (endsWith(prefix, "/api")) {
        prefix.resize(prefix.size() - 4);
    }

    parsed.host = toWide(authority);
    parsed.apiPrefix = prefix;
    if (parsed.host.empty()) {
        error = "Ollama URL host is not valid UTF-8.";
        return false;
    }
    return true;
}

bool httpRequest(
    const std::string& baseUrl,
    const wchar_t* method,
    const std::string& endpoint,
    const std::string& body,
    std::string& response,
    std::string& error)
{
    response.clear();
    error.clear();

    OllamaBaseUrl parsed;
    if (!parseBaseUrl(baseUrl, parsed, error)) {
        return false;
    }

    HINTERNET session = WinHttpOpen(L"RealCore Ollama/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHttpOpen failed.";
        return false;
    }

    WinHttpSetTimeouts(session, 5000, 5000, 30000, 300000);

    HINTERNET connect = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect failed.";
        return false;
    }

    std::wstring path = toWide(parsed.apiPrefix + endpoint);
    DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, method, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest failed.";
        return false;
    }

    const wchar_t* headers = body.empty() ? L"Accept: application/json\r\n" : L"Content-Type: application/json\r\nAccept: application/json\r\n";
    DWORD headersLen = (DWORD)-1L;
    LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
    DWORD bodyLen = (DWORD)body.size();

    BOOL ok = WinHttpSendRequest(request, headers, headersLen, bodyPtr, bodyLen, bodyLen, 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);

    if (!ok) {
        DWORD code = GetLastError();
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Ollama request failed. Is Ollama running? WinHTTP error: %lu", (unsigned long)code);
        error = buf;
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) break;

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
        chunk.resize(read);
        response += chunk;
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (status < 200 || status >= 300) {
        std::string message = extractErrorMessage(response);
        if (message.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Ollama HTTP error: %lu", (unsigned long)status);
            message = buf;
        }
        error = message;
        return false;
    }

    return true;
}

void parseModels(const std::string& json, std::vector<OllamaModel>& models) {
    models.clear();
    size_t pos = json.find("\"models\"");
    if (pos == std::string::npos) {
        pos = 0;
    }

    while ((pos = json.find("\"name\"", pos)) != std::string::npos) {
        size_t objectStart = json.rfind('{', pos);
        size_t objectEnd = objectStart == std::string::npos ? std::string::npos : findMatchingObjectBrace(json, objectStart);
        if (objectStart == std::string::npos || objectEnd == std::string::npos || objectEnd <= objectStart) {
            pos += 6;
            continue;
        }

        std::string objectJson = json.substr(objectStart, objectEnd - objectStart + 1);
        pos = objectEnd + 1;

        OllamaModel model;
        if (!extractStringField(objectJson, "name", model.name)) {
            continue;
        }
        if (!extractStringField(objectJson, "model", model.displayName) || model.displayName.empty()) {
            model.displayName = model.name;
        }
        models.push_back(model);
    }

    std::sort(models.begin(), models.end(), [](const OllamaModel& a, const OllamaModel& b) {
        return a.displayName < b.displayName;
    });
}

bool extractChatMessageContent(const std::string& json, std::string& text) {
    text.clear();
    size_t messagePos = json.find("\"message\"");
    if (messagePos == std::string::npos) {
        return extractStringField(json, "content", text);
    }

    size_t objectStart = json.find('{', messagePos);
    size_t objectEnd = objectStart == std::string::npos ? std::string::npos : findMatchingObjectBrace(json, objectStart);
    if (objectStart == std::string::npos || objectEnd == std::string::npos || objectEnd <= objectStart) {
        return false;
    }

    std::string messageJson = json.substr(objectStart, objectEnd - objectStart + 1);
    return extractStringField(messageJson, "content", text);
}

std::string makeChatBody(
    const std::string& modelName,
    const std::string& systemPrompt,
    const std::string& userPrompt,
    float temperature)
{
    char tempBuf[32];
    std::snprintf(tempBuf, sizeof(tempBuf), "%.2f", temperature);
    return
        "{"
        "\"model\":\"" + jsonEscape(modelName) + "\","
        "\"stream\":false,"
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"" + jsonEscape(systemPrompt) + "\"},"
        "{\"role\":\"user\",\"content\":\"" + jsonEscape(userPrompt) + "\"}"
        "],"
        "\"options\":{\"temperature\":" + std::string(tempBuf) + "}"
        "}";
}

std::string scriptSystemPrompt() {
    return
        "You are helping inside a small C++ game engine script editor. "
        "Always answer in Russian unless the user explicitly asks for another language. "
        "Keep code in the correct programming language; translate explanations, comments, and guidance to Russian when appropriate. "
        "RealCore supports both AngelScript (.as) and Lua (.lua). "
        "Use the language of the current open file; if the user asks for Lua, write Lua. "
        "AngelScript lifecycle: void init(), void update(float dt), void destroy(). "
        "Lua lifecycle: function init(), function update(dt), function destroy(). "
        "AngelScript core types: Vector2, Vector3, Quaternion, Color. "
        "AngelScript globals: Node@ node, Scene@ scene, Input@ input, physics, audio, time. "
        "AngelScript API: node.getPosition(), node.setPosition(pos), node.velocity, "
        "scene.findNode(name), input.getMouseDeltaX(), input.getMouseDeltaY(), input.setMouseVisible(bool), "
        "physics.gravity, audio.playSound(path), time.deltaTime. "
        "Lua globals: node, scene, input, physics, audio, time. "
        "Lua API: Vector3(x,y,z), node:getPosition(), node:setPosition(pos), node.position, node.velocity, node.name, "
        "node.worldBounds, node.localBounds, node.bounds, "
        "scene:findNode(name), scene:load(name), input:getMouseDeltaX(), input:getMouseDeltaY(), "
        "input:setMouseVisible(bool), input:isMouseDown(button), input:isKeyDown(\"A\"), keyboard.isDown(\"A\"), keyboard.A, keyboard.Space, "
        "keyboard.isPressed(\"Space\"), keyboard.isReleased(\"Escape\"), physics.gravity, audio:playSound(path), time.deltaTime. "
        "Legacy AngelScript helpers still exist: print(int), print(float), print(bool), print(const string &in), loadScene(const string &in), quitGame(). "
        "Lua also has print(...), loadScene(name), quitGame(). "
        "Scene loading accepts a scene name like \"Game\" or a path like \"Scenes/Game.rcscene\". "
        "Return concise, practical help. When writing or changing code, use one of these fenced blocks: "
        "```realcore-script-full for a complete replacement of the current file, or "
        "```realcore-replace-function for exactly one complete function to replace by name. "
        "Prefer realcore-script-full when adding globals or multiple functions. Do not return partial code fragments unless the user asks for a small snippet.";
}

std::string engineSystemPrompt() {
    return
        "You are an assistant inside the RealCore game engine editor. "
        "Always answer in Russian unless the user explicitly asks for another language. "
        "Keep JSON action names, file paths, code, and API names exactly as required; translate explanations and guidance to Russian. "
        "You can inspect the scene snapshot and control the local editor through JSON actions that the user applies with the Apply Actions button. "
        "Do not say that you cannot access or modify the engine. If the user asks to change the scene, create objects, edit components, start/stop, save, load, or export, return actions. "
        "Return a short explanation, then append one fenced block exactly named realcore-actions containing a JSON array. "
        "If no editor change is needed, return an empty JSON array in that block.\n\n"
        "Allowed actions:\n"
        "- {\"action\":\"set_transform\",\"id\":1,\"position\":[x,y,z],\"rotationDeg\":[x,y,z],\"scale\":[x,y,z]}\n"
        "- {\"action\":\"move\",\"id\":1,\"delta\":[x,y,z]}\n"
        "- {\"action\":\"rename\",\"id\":1,\"name\":\"New Name\"}\n"
        "- {\"action\":\"set_active\",\"id\":1,\"active\":true}\n"
        "- {\"action\":\"set_mesh\",\"id\":1,\"visible\":true}\n"
        "- {\"action\":\"add_rigidbody\",\"id\":1,\"shape\":\"Box|Sphere|Capsule|Cylinder\",\"dynamic\":true,\"frozen\":false,\"syncTransform\":true,\"velocity\":[x,y,z]}\n"
        "- {\"action\":\"set_rigidbody\",\"id\":1,\"frozen\":true,\"syncTransform\":true,\"velocity\":[x,y,z]}\n"
        "- {\"action\":\"remove_rigidbody\",\"id\":1}\n"
        "- {\"action\":\"add_light\",\"id\":1,\"direction\":[x,y,z],\"color\":[r,g,b],\"intensity\":1.0,\"ambient\":0.2,\"enabled\":true}\n"
        "- {\"action\":\"remove_light\",\"id\":1}\n"
        "- {\"action\":\"add_camera\",\"id\":1,\"fovDeg\":60,\"near\":0.01,\"far\":1000,\"primary\":true,\"enabled\":true}\n"
        "- {\"action\":\"remove_camera\",\"id\":1}\n"
        "- {\"action\":\"remove_script\",\"id\":1}\n"
        "- {\"action\":\"set_parent\",\"id\":1,\"parent\":2} or {\"action\":\"set_parent\",\"id\":1,\"parent\":0} to unparent\n"
        "- {\"action\":\"create_primitive\",\"type\":\"Box|Sphere|Capsule|Cylinder\",\"name\":\"Name\",\"position\":[x,y,z],\"scale\":[x,y,z]}\n"
        "- {\"action\":\"create_empty\",\"name\":\"Name\",\"position\":[x,y,z]}\n"
        "- {\"action\":\"create_camera\",\"name\":\"Name\",\"position\":[x,y,z],\"rotationDeg\":[x,y,z],\"fovDeg\":60}\n"
        "- {\"action\":\"create_light\",\"name\":\"Name\",\"direction\":[x,y,z],\"intensity\":1.0,\"ambient\":0.2}\n"
        "- {\"action\":\"set_light\",\"id\":1,\"direction\":[x,y,z],\"intensity\":1.0,\"ambient\":0.2,\"enabled\":true}\n"
        "- {\"action\":\"set_camera\",\"id\":1,\"fovDeg\":60,\"near\":0.01,\"far\":1000,\"primary\":true,\"enabled\":true}\n"
        "- {\"action\":\"attach_script\",\"id\":1,\"path\":\"Scripts/Menu.as\"}\n"
        "- {\"action\":\"attach_script\",\"id\":1,\"path\":\"Scripts/Menu.lua\"}\n"
        "- {\"action\":\"select\",\"id\":1}\n"
        "- {\"action\":\"focus\",\"id\":1}\n"
        "- {\"action\":\"delete\",\"id\":1}\n"
        "- {\"action\":\"play\"}\n"
        "- {\"action\":\"pause\"}\n"
        "- {\"action\":\"stop\"}\n"
        "- {\"action\":\"new_scene\"}\n"
        "- {\"action\":\"save_scene\"} or {\"action\":\"save_scene\",\"path\":\"Scenes/Name.rcscene\"}\n"
        "- {\"action\":\"load_scene\",\"path\":\"Scenes/Name.rcscene\"}\n"
        "- {\"action\":\"export_game\"}\n"
        "- {\"action\":\"use_scene_camera\",\"enabled\":true}\n\n"
        "Use object ids from the scene snapshot. Use degrees for rotationDeg. Do not output unsupported actions. "
        "Use [x,y,z] arrays or objects like {\"x\":0,\"y\":1,\"z\":0} for vectors. "
        "Use attach_script to bind an AngelScript .as or Lua .lua file to an object. If an OpenScript path is shown and the user asks to attach the current script, use that path. "
        "If the user asks for size, use scale. If the user asks for distance/spacing, use position or move. "
        "RealCore supports scripts in AngelScript (.as) and Lua (.lua). Lua files use function init(), function update(dt), function destroy(). "
        "Lua API includes node:getPosition(), node:setPosition(pos), node.velocity, node.worldBounds, scene:findNode(name), input:getMouseDeltaX(), input:setMouseVisible(bool), keyboard.isDown(\"A\"), keyboard.Space.";
}

bool generateChat(
    const std::string& baseUrl,
    const std::string& modelName,
    const std::string& systemPrompt,
    const std::string& userPrompt,
    float temperature,
    std::string& response,
    std::string& error)
{
    if (modelName.empty()) {
        error = "Select an Ollama model first.";
        return false;
    }
    if (trimCopy(userPrompt).empty()) {
        error = "Ask Ollama what to do first.";
        return false;
    }

    std::string body = makeChatBody(modelName, systemPrompt, userPrompt, temperature);
    std::string raw;
    if (!httpRequest(baseUrl, L"POST", "/api/chat", body, raw, error)) {
        return false;
    }

    if (!extractChatMessageContent(raw, response)) {
        error = "Ollama response did not contain message.content.";
        return false;
    }
    return true;
}

} // namespace

bool OllamaClient::listModels(const std::string& baseUrl, std::vector<OllamaModel>& models, std::string& error) {
    std::string response;
    if (!httpRequest(baseUrl, L"GET", "/api/tags", {}, response, error)) {
        return false;
    }

    parseModels(response, models);
    if (models.empty()) {
        error = "No Ollama models returned. Pull a model first, for example: ollama pull qwen2.5-coder";
        return false;
    }
    return true;
}

bool OllamaClient::generateScriptHelp(
    const std::string& baseUrl,
    const std::string& modelName,
    const std::string& userRequest,
    const std::string& currentScript,
    std::string& response,
    std::string& error)
{
    std::string userPrompt =
        "User request:\n" + userRequest + "\n\n"
        "Current script:\n```text\n" + currentScript + "\n```";

    return generateChat(baseUrl, modelName, scriptSystemPrompt(), userPrompt, 0.25f, response, error);
}

bool OllamaClient::generateEngineHelp(
    const std::string& baseUrl,
    const std::string& modelName,
    const std::string& userRequest,
    const std::string& engineContext,
    const std::string& currentScript,
    std::string& response,
    std::string& error)
{
    std::string userPrompt =
        "User request:\n" + userRequest + "\n\n"
        "Engine snapshot:\n```text\n" + engineContext + "\n```\n\n"
        "Current open script, if any:\n```text\n" + currentScript + "\n```";

    return generateChat(baseUrl, modelName, engineSystemPrompt(), userPrompt, 0.20f, response, error);
}
