#pragma once
#include <string>

namespace NativeDialogs {

std::string openModelFile();
std::string openAnyFile();
std::string openSceneFile();
std::string saveSceneFile(const std::string& defaultPath);
std::string selectFolder();

}
