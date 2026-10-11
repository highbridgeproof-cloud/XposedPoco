
#pragma once

#include <string>

namespace XposedPoco {

void StartCommandServer();

// Unity メインスレッドから呼び出す
bool TryBeginDumpRequest();

// ダンプ完了時に Unity メインスレッドから呼び出す
void CompleteDumpRequest(std::string json);

}
