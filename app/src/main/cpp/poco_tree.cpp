#include "poco_tree.hpp"

#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <cstdint>

#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/BasicMonoStructures.hpp>
#include <BNM/UnityStructures.hpp>
#include <BNM/Utils.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

namespace XposedPoco {
namespace {

using Il2CppObject = BNM::IL2CPP::Il2CppObject;
using MonoString = BNM::Structures::Mono::String;
using UnityObject = BNM::UnityEngine::Object;

constexpr std::size_t kMaxNodes = 3000;
constexpr std::size_t kMaxDepth = 96;

// JSON の文字列エスケープ
std::string EscapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);

    static constexpr char hex[] = "0123456789abcdef";

    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0f];
                    out += hex[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }

    return out;
}

bool IsAlive(Il2CppObject* object) {
    if (!object) {
        return false;
    }

    // UnityEngine.Object のネイティブ側オブジェクトも検査する。
    return reinterpret_cast<UnityObject*>(object)->Alive();
}

BNM::Class GameObjectClass() {
    return BNM::Class("UnityEngine", "GameObject");
}

BNM::Class TransformClass() {
    return BNM::Class("UnityEngine", "Transform");
}

BNM::Class ObjectClass() {
    return BNM::Class("UnityEngine", "Object");
}

Il2CppObject* FindGameObject(const char* name) {
    auto method = GameObjectClass()
        .GetMethod("Find", 1)
        .cast<Il2CppObject*>();

    if (!method.IsValid()) {
        return nullptr;
    }

    return method(BNM::CreateMonoString(name));
}

std::string GetObjectName(Il2CppObject* object) {
    if (!IsAlive(object)) {
        return {};
    }

    auto method = ObjectClass()
        .GetMethod("get_name", 0)
        .cast<MonoString*>();

    if (!method.IsValid()) {
        return {};
    }

    MonoString* result = method[object]();

    if (!result) {
        return {};
    }

    return result->str();
}

bool GetActiveInHierarchy(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject)) {
        return false;
    }

    auto method = GameObjectClass()
        .GetMethod("get_activeInHierarchy", 0)
        .cast<bool>();

    if (!method.IsValid()) {
        return false;
    }

    return method[gameObject]();
}

Il2CppObject* GetTransform(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject)) {
        return nullptr;
    }

    auto method = GameObjectClass()
        .GetMethod("get_transform", 0)
        .cast<Il2CppObject*>();

    if (!method.IsValid()) {
        return nullptr;
    }

    return method[gameObject]();
}

int GetChildCount(Il2CppObject* transform) {
    if (!IsAlive(transform)) {
        return 0;
    }

    auto method = TransformClass()
        .GetMethod("get_childCount", 0)
        .cast<int>();

    if (!method.IsValid()) {
        return 0;
    }

    return std::max(0, method[transform]());
}

Il2CppObject* GetChildGameObject(
    Il2CppObject* transform,
    int index
) {
    if (!IsAlive(transform)) {
        return nullptr;
    }

    auto getChild = TransformClass()
        .GetMethod("GetChild", 1)
        .cast<Il2CppObject*>();

    if (!getChild.IsValid()) {
        return nullptr;
    }

    Il2CppObject* childTransform = getChild[transform](index);

    if (!IsAlive(childTransform)) {
        return nullptr;
    }

    // Transform は Component を継承するため、
    // get_gameObject は親クラスから検索できる。
    auto getGameObject = TransformClass()
        .GetMethod("get_gameObject", 0)
        .cast<Il2CppObject*>();

    if (!getGameObject.IsValid()) {
        return nullptr;
    }

    return getGameObject[childTransform]();
}

std::string BuildNode(
    Il2CppObject* gameObject,
    std::size_t depth,
    std::size_t& visited
) {
    if (!IsAlive(gameObject)) {
        return "null";
    }

    const std::string name = GetObjectName(gameObject);
    const bool active = GetActiveInHierarchy(gameObject);

    __android_log_print(
        ANDROID_LOG_INFO, "XposedPoco",
        "DUMP generating fresh tree"
    );

    std::string json =
        "{\"name\":\"" + EscapeJson(name) +
        "\",\"payload\":{" 
        "\"name\":\"" + EscapeJson(name) +
        "\",\"type\":\"GameObject\"," 
        "\"visible\":" + (active ? "true" : "false") +
        ",\"active\":" + (active ? "true" : "false") +
        ",\"clickable\":false"
        "},\"children\":[";
    __android_log_print(
        ANDROID_LOG_INFO, "XposedPoco",
        "DUMP generated bytes=%zu",
        json.size()
    );        

    if (depth >= kMaxDepth || visited >= kMaxNodes) {
        json += "]}";
        return json;
    }

    Il2CppObject* transform = GetTransform(gameObject);
    if (!IsAlive(transform)) {
        json += "]}";
        return json;
    }

    const int childCount = GetChildCount(transform);
    bool first = true;

    for (int i = 0;
         i < childCount && visited < kMaxNodes;
         ++i) {

        Il2CppObject* child = GetChildGameObject(transform, i);

        if (!IsAlive(child)) {
            continue;
        }

        if (!first) {
            json += ',';
        }
        first = false;

        ++visited;
        json += BuildNode(child, depth + 1, visited);
    }

    json += "]}";
    return json;
}

} // namespace

std::string DumpGameObjectTree(const char* rootName) {
    if (!BNM::IsLoaded()) {
        return "{\"error\":\"BNM is not loaded\"}";
    }

    if (!rootName || !*rootName) {
        return "{\"error\":\"rootName is empty\"}";
    }

    Il2CppObject* root = FindGameObject(rootName);

    if (!IsAlive(root)) {
        return "{\"error\":\"root GameObject not found\"}";
    }
    __android_log_print(
        ANDROID_LOG_INFO,
        "XposedPoco",
        "DUMP root=%s ptr=%p",
        rootName,
        static_cast<void*>(root)
    );
    std::size_t visited = 1;
    return BuildNode(root, 0, visited);
}

} // namespace XposedPoco