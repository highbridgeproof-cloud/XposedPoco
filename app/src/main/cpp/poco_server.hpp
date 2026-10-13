
#pragma once

#include <string>

namespace XposedPoco {

enum class RequestType {
    Dump,
    Clickables,
    Click
};
void StartCommandServer();

// Unity メインスレッドから呼び出す
bool TryBeginRequest(RequestType& type, std::string& target);

// ダンプ完了時に Unity メインスレッドから呼び出す
void CompleteDumpRequest(std::string json);

}
