#pragma once

#include <string>

namespace XposedPoco {
    // Unity のメインスレッド上で呼び出すこと。
    // アクティブシーンの全ルート GameObject と子孫をダンプする。
    // rootName は既存呼び出しとの互換性のため残している（検索には使わない）。
    std::string DumpGameObjectTree(const char* rootName);
    std::string DumpClickableObjects();
    std::string ClickGameObject(const std::string& target);
    std::string ClickComponent(const std::string& componentType,
                               const std::string& target);
}