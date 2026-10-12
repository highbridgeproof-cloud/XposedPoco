#include "poco_tree.hpp"
#include "include/log.h"

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

struct SceneValue {
    std::int32_t handle;
};

BNM::Class SceneClass() {
    return BNM::Class("UnityEngine.SceneManagement", "Scene");
}

BNM::Class SceneManagerClass() {
    return BNM::Class("UnityEngine.SceneManagement", "SceneManager");
}

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


std::string GetParentName(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject)) {
        return {};
    }

    auto getTransform = GameObjectClass()
        .GetMethod("get_transform", 0)
        .cast<Il2CppObject*>();

    if (!getTransform.IsValid()) {
        return {};
    }

    Il2CppObject* transform = getTransform[gameObject]();

    if (!IsAlive(transform)) {
        return {};
    }

    auto getParent = TransformClass()
        .GetMethod("get_parent", 0)
        .cast<Il2CppObject*>();

    if (!getParent.IsValid()) {
        return {};
    }

    Il2CppObject* parent = getParent[transform]();

    // 親がない場合はルート GameObject
    if (!IsAlive(parent)) {
        return "<root>";
    }

    // Transform の名前は対応する GameObject の名前と同じ
    return GetObjectName(parent);
}

std::string GetObjectPath(Il2CppObject* gameObject) {
    std::string path;
    Il2CppObject* current = gameObject;

    // 親をたどる。循環や異常な階層に備えて上限を設ける
    for (int depth = 0; depth < 100 && IsAlive(current); ++depth) {
        const std::string name = GetObjectName(current);
        if (name.empty()) {
            break;
        }

        if (path.empty()) {
            path = name;
        } else {
            path = name + "/" + path;
        }

        auto getTransform = GameObjectClass()
            .GetMethod("get_transform", 0)
            .cast<Il2CppObject*>();

        if (!getTransform.IsValid()) {
            break;
        }

        Il2CppObject* transform = getTransform[current]();
        if (!IsAlive(transform)) {
            break;
        }

        auto getParent = TransformClass()
            .GetMethod("get_parent", 0)
            .cast<Il2CppObject*>();

        if (!getParent.IsValid()) {
            break;
        }

        current = getParent[transform]();
    }

    return path;
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

    std::string json =
        "{\"name\":\"" + EscapeJson(name) +
        "\",\"payload\":{" 
        "\"name\":\"" + EscapeJson(name) +
        "\",\"type\":\"GameObject\"," 
        "\"visible\":" + (active ? "true" : "false") +
        ",\"active\":" + (active ? "true" : "false") +
        ",\"clickable\":false"
        "},\"children\":[";   

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
    LOGI("TREE node='%s' depth=%zu children=%d active=%s",
        name.c_str(),
        depth,
        childCount,
        active ? "true" : "false"
    );
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


void LogAllGameObjects() {
    auto objectClass = BNM::Class("UnityEngine", "Object");
    auto method = objectClass
        .GetMethod("FindObjectsOfType", 2)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

    if (!method.IsValid()) {
        LOGE("OBJECT_SCAN FindObjectsOfType(Type, bool) unavailable");
        return;
    }

    auto result = method(
        GameObjectClass().GetMonoType(),
        true
    );

    if (!result) {
        LOGE("OBJECT_SCAN returned null");
        return;
    }

    const auto count = result->GetSize();
    auto data = result->GetData();

    LOGI("OBJECT_SCAN count=%zu",
        static_cast<std::size_t>(count));

    //const auto limit = count < 200 ? count : 200;
    const auto limit = count;
    for (std::size_t i = 0; i < static_cast<std::size_t>(limit); ++i) {
        auto* object = data[i];
        if (!IsAlive(object)) {
            continue;
        }

        const auto name = GetObjectName(object);
        //const auto parentName = GetParentName(object);
        const auto path = GetObjectPath(object);
        LOGI("OBJECT_SCAN name=%s path=%s ptr=%p",
            name.c_str(),
            path.c_str(),
            static_cast<void*>(object));
    }
}


void LogClassMethods(const char *className, BNM::Class klass) {
    if (!klass) {
        LOGI("METHOD_SCAN class=%s unavailable", className);
        return;
    }

    auto methods = klass.GetMethods(false);

    LOGI("METHOD_SCAN class=%s count=%zu",
        className, methods.size());

    for (const auto &method : methods) {
        auto *info = method.GetInfo();
        if (!info || !info->name) {
            continue;
        }

        std::string returnType = BNM::Class(info->return_type).str();
        std::string parameters;

        for (unsigned int i = 0; i < info->parameters_count; ++i) {
            if (i > 0) {
                parameters += ", ";
            }

            if (info->parameters && info->parameters[i]) {
                parameters += BNM::Class(info->parameters[i]).str();
            } else {
                parameters += "<unknown>";
            }
        }

        LOGI("METHOD_SCAN %s.%s return=%s params=(%s) argc=%u static=%u",
            className,
            info->name,
            returnType.c_str(),
            parameters.c_str(),
            static_cast<unsigned>(info->parameters_count),
            static_cast<unsigned>(method._isStatic));
    }
}


std::string DumpGameObjectTree(const char* rootName) {
    if (!BNM::IsLoaded()) {
        return R"({"error":"BNM is not loaded"})";
    }

    LogClassMethods("GameObject", GameObjectClass());
    LogClassMethods("Object", ObjectClass());
    LogClassMethods("Component", BNM::Class("UnityEngine", "Component"));
    LogClassMethods("Resources", BNM::Class("UnityEngine", "Resources"));
    LogClassMethods("Scene", SceneClass());
    LogClassMethods("SceneManager", SceneManagerClass());
    LogAllGameObjects();

    auto getActiveScene = SceneManagerClass()
        .GetMethod("GetActiveScene", 0)
        .cast<SceneValue>();

    if (!getActiveScene.IsValid()) {
        LOGE("DUMP SceneManager.GetActiveScene unavailable");
        return R"({"error":"GetActiveScene unavailable"})";
    }

    // Scene は IL2CPP の参照型オブジェクトではなく、int handle を持つ値型。
    // BNM から値型として取得し、その構造体のアドレスを instance として渡す。
    SceneValue scene = getActiveScene();

    auto getRoots = SceneClass()
        .GetMethod("GetRootGameObjects", 0)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

    if (!getRoots.IsValid()) {
        LOGE("DUMP Scene.GetRootGameObjects() unavailable");
        return R"({"error":"GetRootGameObjects unavailable"})";
    }

    auto* roots = getRoots[
        reinterpret_cast<Il2CppObject*>(&scene)
    ]();

    if (!roots) {
        LOGE("DUMP GetRootGameObjects returned null (scene handle=%d)",
            scene.handle);
        return R"({"error":"GetRootGameObjects returned null"})";
    }

    const auto rootCount = static_cast<std::size_t>(roots->GetSize());
    auto rootData = roots->GetData();

    LOGI("DUMP active scene handle=%d rootCount=%zu requestedRoot=%s",
        scene.handle,
        rootCount,
        (rootName && *rootName) ? rootName : "<all>");

    // Poco には Scene を仮想ルートとして返し、その children に
    // アクティブシーンの各ルート GameObject のツリーを格納する。
    std::string json =
        R"({"name":"ActiveScene","payload":{"name":"ActiveScene","type":"Scene","visible":true,"active":true,"clickable":false},"children":[)";


    std::size_t visited = 1; // 仮想 Scene ノード
    bool first = true;

    for (std::size_t i = 0;
         i < rootCount && visited < kMaxNodes;
         ++i) {
        Il2CppObject* root = rootData[i];

        if (!IsAlive(root)) {
            continue;
        }

        if (!first) {
            json += ',';
        }
        first = false;

        ++visited;
        json += BuildNode(root, 1, visited);
    }

    json += "]}";

    LOGI("DUMP complete: sceneRoots=%zu nodes=%zu bytes=%zu",
        rootCount,
        visited,
        json.size());

    return json;
}

} // namespace XposedPoco