#pragma once

#include <string>

namespace XposedPoco {
    // Unity のメインスレッド上で呼び出すこと。
    // rootName は GameObject.Find() で検索する名前。
    std::string DumpGameObjectTree(const char* rootName);
}