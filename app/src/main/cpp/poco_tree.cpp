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
    if (!IsAlive(gameObject)) {
        return {};
    }

    auto getTransform = GameObjectClass()
        .GetMethod("get_transform", 0)
        .cast<Il2CppObject*>();

    auto getParent = TransformClass()
        .GetMethod("get_parent", 0)
        .cast<Il2CppObject*>();

    if (!getTransform.IsValid() || !getParent.IsValid()) {
        return {};
    }

    // GameObject から Transform を一度だけ取得する
    Il2CppObject* currentTransform = getTransform[gameObject]();
    std::string path;

    for (int depth = 0;
         depth < 100 && IsAlive(currentTransform);
         ++depth) {

        const std::string name = GetObjectName(currentTransform);
        if (name.empty()) {
            break;
        }

        if (path.empty()) {
            path = name;
        } else {
            path = name + "/" + path;
        }

        // 次も Transform として親をたどる
        currentTransform = getParent[currentTransform]();
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

Il2CppObject* GetComponentByClass(
    Il2CppObject* gameObject,
    BNM::Class componentClass
) {
    if (!IsAlive(gameObject) || !componentClass) {
        return nullptr;
    }

    auto method = GameObjectClass()
        .GetMethod("GetComponent", 1)
        .cast<Il2CppObject*>();

    if (!method.IsValid()) {
        return nullptr;
    }

    return method[gameObject](componentClass.GetMonoType());
}


bool IsClickable(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject) ||
        !GetActiveInHierarchy(gameObject)) {
        return false;
    }

    // 1. 既存の Unity UI 判定を維持
    auto selectableClass =
        BNM::Class("UnityEngine.UI", "Selectable");

    Il2CppObject* selectable =
        GetComponentByClass(gameObject, selectableClass);

    if (IsAlive(selectable)) {
        auto getEnabled = BNM::Class("UnityEngine", "Behaviour")
            .GetMethod("get_enabled", 0)
            .cast<bool>();

        if (!getEnabled.IsValid() ||
            !getEnabled[selectable]()) {
            return false;
        }

        auto isInteractable = selectableClass
            .GetMethod("IsInteractable", 0)
            .cast<bool>();

        if (!isInteractable.IsValid()) {
            return false;
        }

        return isInteractable[selectable]();
    }

    // 2. ワールド内の卵回収対象を判定
    auto petEggItemClass =
        BNM::Class("M1PetGame", "PetEggItem");

    Il2CppObject* petEggItem =
        GetComponentByClass(gameObject, petEggItemClass);

    if (!IsAlive(petEggItem)) {
        return false;
    }

    // Collider2D が存在し、有効な場合のみ候補にする
    auto collider2DClass =
        BNM::Class("UnityEngine", "Collider2D");

    Il2CppObject* collider2D =
        GetComponentByClass(gameObject, collider2DClass);

    if (!IsAlive(collider2D)) {
        return false;
    }

    auto getEnabled = BNM::Class("UnityEngine", "Behaviour")
        .GetMethod("get_enabled", 0)
        .cast<bool>();

    if (!getEnabled.IsValid()) {
        return false;
    }

    return getEnabled[collider2D]();
}


bool GetComponentEnabled(
    Il2CppObject* component,
    BNM::Class componentClass
) {
    if (!IsAlive(component) || !componentClass) {
        return false;
    }

    auto getEnabled = componentClass
        .GetMethod("get_enabled", 0)
        .cast<bool>();

    if (!getEnabled.IsValid()) {
        return false;
    }

    return getEnabled[component]();
}

struct ColliderState {
    bool hasCollider = false;
    bool colliderEnabled = false;
    bool hasCollider2D = false;
    bool collider2DEnabled = false;
};

ColliderState GetColliderState(Il2CppObject* gameObject) {
    ColliderState state;

    const auto colliderClass =
        BNM::Class("UnityEngine", "Collider");

    const auto collider2DClass =
        BNM::Class("UnityEngine", "Collider2D");

    Il2CppObject* collider =
        GetComponentByClass(gameObject, colliderClass);

    Il2CppObject* collider2D =
        GetComponentByClass(gameObject, collider2DClass);

    state.hasCollider = IsAlive(collider);
    state.colliderEnabled =
        state.hasCollider &&
        GetComponentEnabled(collider, colliderClass);

    state.hasCollider2D = IsAlive(collider2D);
    state.collider2DEnabled =
        state.hasCollider2D &&
        GetComponentEnabled(collider2D, collider2DClass);

    return state;
}

std::string GetComponentTypesJson(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject)) {
        return "[]";
    }

    // GameObject.GetComponents(Type) でアタッチ済みコンポーネントを取得
    auto getComponents = GameObjectClass()
        .GetMethod("GetComponents", 1)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

    if (!getComponents.IsValid()) {
        return "[]";
    }

    auto componentClass = BNM::Class("UnityEngine", "Component");
    auto components =
        getComponents[gameObject](componentClass.GetMonoType());

    if (!components) {
        return "[]";
    }

    const auto count =
        static_cast<std::size_t>(components->GetSize());

    auto data = components->GetData();

    std::string json = "[";
    bool first = true;

    // JSON の肥大化を防ぐため、1ノードあたり最大32個
    const auto limit = std::min<std::size_t>(count, 32);

    for (std::size_t i = 0; i < limit; ++i) {
        Il2CppObject* component = data[i];

        if (!IsAlive(component)) {
            continue;
        }

        auto klass = BNM::Class(component).GetClass();
        if (!klass || !klass->name) {
            continue;
        }

        std::string typeName;

        if (klass->namespaze && klass->namespaze[0] != '\0') {
            typeName = std::string(klass->namespaze) + ".";
        }

        typeName += klass->name;

        if (!first) {
            json += ',';
        }

        first = false;
        json += "\"" + EscapeJson(typeName) + "\"";
    }

    json += "]";
    return json;
}

bool IsVisible(Il2CppObject* gameObject) {
    if (!IsAlive(gameObject) ||
        !GetActiveInHierarchy(gameObject)) {
        return false;
    }

    // Unity UI の描画状態を確認
    auto canvasRendererClass =
        BNM::Class("UnityEngine", "CanvasRenderer");

    Il2CppObject* canvasRenderer =
        GetComponentByClass(gameObject, canvasRendererClass);

    if (IsAlive(canvasRenderer)) {
        auto getCull = canvasRendererClass
            .GetMethod("get_cull", 0)
            .cast<bool>();

        if (getCull.IsValid()) {
            return !getCull[canvasRenderer]();
        }
    }

    // 通常の Renderer を持つ GameObject にも対応
    auto rendererClass =
        BNM::Class("UnityEngine", "Renderer");

    Il2CppObject* renderer =
        GetComponentByClass(gameObject, rendererClass);

    if (IsAlive(renderer)) {
        auto getEnabled = BNM::Class("UnityEngine", "Renderer")
            .GetMethod("get_enabled", 0)
            .cast<bool>();

        if (getEnabled.IsValid()) {
            return getEnabled[renderer]();
        }
    }

    // 描画コンポーネントを持たない親オブジェクトなどは、
    // activeInHierarchy を暫定的な判定として使用する
    return true;
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
    const bool visible = IsVisible(gameObject);
    const bool clickable = IsClickable(gameObject);
    const ColliderState collider = GetColliderState(gameObject);
    const std::string componentTypes = GetComponentTypesJson(gameObject);
    

    std::string json =
        "{\"name\":\"" + EscapeJson(name) +
        "\",\"payload\":{"
        "\"name\":\"" + EscapeJson(name) +
        "\",\"type\":\"GameObject\","
        "\"visible\":" + (visible ? "true" : "false") +
        ",\"active\":" + (active ? "true" : "false") +
        ",\"clickable\":" + (clickable ? "true" : "false") +
        ",\"hasCollider\":" +
            (collider.hasCollider ? "true" : "false") +
        ",\"colliderEnabled\":" +
            (collider.colliderEnabled ? "true" : "false") +
        ",\"hasCollider2D\":" +
            (collider.hasCollider2D ? "true" : "false") +
        ",\"collider2DEnabled\":" +
            (collider.collider2DEnabled ? "true" : "false") +
        ",\"componentTypes\":" + componentTypes +
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
    LOGI("scene1");    

    if (!getActiveScene.IsValid()) {
        LOGE("DUMP SceneManager.GetActiveScene unavailable");
        return R"({"error":"GetActiveScene unavailable"})";
    }

    // Scene は IL2CPP の参照型オブジェクトではなく、int handle を持つ値型。
    // BNM から値型として取得し、その構造体のアドレスを instance として渡す。
    SceneValue scene = getActiveScene();
    LOGI("scene2");  

    auto getRoots = SceneClass()
        .GetMethod("GetRootGameObjects", 0)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();
    LOGI("scene3");      

    if (!getRoots.IsValid()) {
        LOGE("DUMP Scene.GetRootGameObjects() unavailable");
        return R"({"error":"GetRootGameObjects unavailable"})";
    }

    // Scene は値型なので、BNM の operator[] にスタック上の SceneValue を
    // Il2CppObject* として渡さない。BNM::Method::Call は instance を
    // IL2CPP オブジェクトとして検査するため、値型の this ポインタを
    // methodPointer に直接渡して呼び出す。
    using RootArray = BNM::Structures::Mono::Array<Il2CppObject*>*;
    auto* methodInfo = getRoots.GetInfo();
    if (!methodInfo || !methodInfo->methodPointer) {
        LOGE("DUMP GetRootGameObjects method pointer unavailable");
        return R"({"error":"GetRootGameObjects method pointer unavailable"})";
    }

    LOGI("scene4-before handle=%d scene_ptr=%p method=%p",
        scene.handle,
        static_cast<void*>(&scene),
        reinterpret_cast<void*>(methodInfo->methodPointer));

    using GetRootsFn = RootArray (*)(
        SceneValue*,
        const BNM::IL2CPP::MethodInfo*);
    auto getRootsFn = reinterpret_cast<GetRootsFn>(methodInfo->methodPointer);
    RootArray roots = getRootsFn(&scene, methodInfo);
    LOGI("scene4-after roots=%p", static_cast<void*>(roots));   

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
std::string DumpClickableObjects() {
    if (!BNM::IsLoaded()) {
        return R"({"error":"BNM is not loaded"})";
    }

    auto objectClass = BNM::Class("UnityEngine", "Object");

    auto findObjects = objectClass
        .GetMethod("FindObjectsOfType", 2)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

    if (!findObjects.IsValid()) {
        return R"({"error":"FindObjectsOfType unavailable"})";
    }

    auto objects = findObjects(
        GameObjectClass().GetMonoType(),
        true
    );

    if (!objects) {
        return "[]";
    }

    const auto count =
        static_cast<std::size_t>(objects->GetSize());
    auto data = objects->GetData();

    std::string json = "[";
    bool first = true;
    std::size_t added = 0;

    for (std::size_t i = 0;
         i < count && added < kMaxNodes;
         ++i) {

        Il2CppObject* object = data[i];

        if (!IsAlive(object) || !IsClickable(object)) {
            continue;
        }

        const std::string name = GetObjectName(object);
        const std::string path = GetObjectPath(object);

        if (path.empty()) {
            continue;
        }

        if (!first) {
            json += ',';
        }
        first = false;

        json +=
            "{\"name\":\"" + EscapeJson(name) +
            "\",\"path\":\"" + EscapeJson(path) +
            "\",\"active\":true"
            ",\"visible\":" +
                std::string(IsVisible(object) ? "true" : "false") +
            ",\"clickable\":true"
            ",\"componentTypes\":" +
                GetComponentTypesJson(object) +
            "}";

        ++added;
    }

    json += "]";
    return json;
}

std::string ClickGameObject(const std::string& target) {
    auto errorJson = [](const std::string& message) {
        return std::string("{\"ok\":false,\"error\":\"") +
            EscapeJson(message) + "\"}";
    };
    if (!BNM::IsLoaded()) return errorJson("BNM is not loaded");
    if (target.empty()) return errorJson("CLICK requires a name or path");

    auto findObjects = ObjectClass().GetMethod("FindObjectsOfType", 2)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();
    if (!findObjects.IsValid())
        return errorJson("FindObjectsOfType(Type, bool) unavailable");
    auto objects = findObjects(GameObjectClass().GetMonoType(), true);
    if (!objects) return errorJson("FindObjectsOfType returned null");

    Il2CppObject* match = nullptr;
    std::string matchedName, matchedPath;
    std::size_t matches = 0;
    const auto count = static_cast<std::size_t>(objects->GetSize());
    auto data = objects->GetData();
    for (std::size_t i = 0; i < count; ++i) {
        Il2CppObject* object = data[i];
        if (!IsAlive(object)) continue;
        const std::string name = GetObjectName(object);
        const std::string path = GetObjectPath(object);
        if (target != name && target != path) continue;
        ++matches;
        match = object;
        matchedName = name;
        matchedPath = path;
    }
    if (matches == 0 || !match)
        return errorJson("No GameObject matched the supplied name or path");
    if (matches > 1)
        return errorJson("Name is ambiguous; use the full GameObject path");
    if (!GetActiveInHierarchy(match))
        return errorJson("Target GameObject is not active in hierarchy");
    if (!IsClickable(match))
        return errorJson("Target is not currently clickable");

    const auto buttonClass = BNM::Class("UnityEngine.UI", "Button");
    Il2CppObject* button = GetComponentByClass(match, buttonClass);
    if (IsAlive(button)) {
        auto isInteractable = BNM::Class("UnityEngine.UI", "Selectable")
            .GetMethod("IsInteractable", 0).cast<bool>();
        if (!isInteractable.IsValid() || !isInteractable[button]())
            return errorJson("Button is not interactable");
        auto getOnClick = buttonClass.GetMethod("get_onClick", 0)
            .cast<Il2CppObject*>();
        if (!getOnClick.IsValid())
            return errorJson("Button.get_onClick() unavailable");
        Il2CppObject* unityEvent = getOnClick[button]();
        if (!IsAlive(unityEvent)) return errorJson("Button.onClick is null");
        auto invoke = BNM::Class("UnityEngine.Events", "UnityEvent")
            .GetMethod("Invoke", 0).cast<void>();
        if (!invoke.IsValid())
            return errorJson("UnityEvent.Invoke() unavailable");
        invoke[unityEvent]();
        LOGI("CLICK success type=ui-button name=%s path=%s",
             matchedName.c_str(), matchedPath.c_str());
        return std::string("{\"ok\":true,\"type\":\"ui-button\",\"name\":\"") +
            EscapeJson(matchedName) + "\",\"path\":\"" +
            EscapeJson(matchedPath) + "\"}";
    }

    const auto petEggItemClass = BNM::Class("M1PetGame", "PetEggItem");
    Il2CppObject* petEggItem = GetComponentByClass(match, petEggItemClass);
    if (IsAlive(petEggItem)) {
        LogClassMethods("PetEggItem", petEggItemClass);
        return errorJson("PetEggItem handler is not implemented; inspect METHOD_SCAN PetEggItem in logcat");
    }
    return errorJson("Matched object type does not have a supported click handler");
}

} // namespace XposedPoco