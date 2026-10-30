#include "poco_tree.hpp"
#include "include/log.h"

#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <cstdint>
#include <link.h>
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/BasicMonoStructures.hpp>
#include <BNM/UnityStructures.hpp>
#include <BNM/Utils.hpp>
#include "BNM/FieldBase.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace XposedPoco {
namespace {

using Il2CppObject = BNM::IL2CPP::Il2CppObject;
using MonoString = BNM::Structures::Mono::String;
using UnityObject = BNM::UnityEngine::Object;

struct Il2CppModuleBaseSearch {
    uintptr_t base = 0;
};

int FindIl2CppModuleBase(
    struct dl_phdr_info* info,
    size_t,
    void* userData
) {
    if (!info || !info->dlpi_name || !userData) {
        return 0;
    }

    if (std::strstr(info->dlpi_name, "libil2cpp.so")) {
        auto* result =
            static_cast<Il2CppModuleBaseSearch*>(userData);

        result->base = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
    }

    return 0;
}

void LogOpenEggCallbackMetadata() {
    Il2CppModuleBaseSearch module;

    dl_iterate_phdr(FindIl2CppModuleBase, &module);

    if (module.base == 0) {
        LOGE("PET_EGG_CALLBACK module libil2cpp.so not found");
        return;
    }

    using MethodInfo = BNM::IL2CPP::MethodInfo;

    // TouchEnd の逆アセンブルで確認した ELF 仮想アドレス。
    constexpr uintptr_t kSuccessSlotRva = 0x31A1540;
    constexpr uintptr_t kFailureSlotRva = 0x31A1530;

    auto logSlot = [&](const char* label, uintptr_t slotRva) {
        const uintptr_t slotAddress = module.base + slotRva;

        // TouchEnd では:
        //   ldr x8, [module_base + slotRva]
        //   ldr x2, [x8]
        // の順で読み込まれる。
        auto* cell = *reinterpret_cast<uintptr_t**>(slotAddress);

        MethodInfo* info = nullptr;

        if (cell != nullptr) {
            info = *reinterpret_cast<MethodInfo**>(cell);
        }

        LOGI(
            "PET_EGG_CALLBACK %s slot=%p cell=%p info=%p",
            label,
            reinterpret_cast<void*>(slotAddress),
            static_cast<void*>(cell),
            static_cast<void*>(info)
        );

        if (info == nullptr) {
            LOGW(
                "PET_EGG_CALLBACK %s metadata is null",
                label
            );
            return;
        }

        const uintptr_t methodAddress =
            reinterpret_cast<uintptr_t>(info->methodPointer);

        const uintptr_t methodRva =
            methodAddress >= module.base
                ? methodAddress - module.base
                : 0;

        LOGI(
            "PET_EGG_CALLBACK %s name=%s method=%p "
            "methodRva=0x%llx params=%u token=0x%x",
            label,
            info->name ? info->name : "<null>",
            reinterpret_cast<void*>(info->methodPointer),
            methodRva,
            static_cast<unsigned>(info->parameters_count),
            static_cast<unsigned>(info->token)
        );
    };

    LOGI(
        "PET_EGG_CALLBACK moduleBase=%p",
        reinterpret_cast<void*>(module.base)
    );

    logSlot("SUCCESS", kSuccessSlotRva);
    logSlot("FAILURE", kFailureSlotRva);
}
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

static void LogEggItemInfo(
    Il2CppObject* gameObject,
    BNM::Class petEggItemClass,
    BNM::Class collider2DClass,
    bool colliderEnabled)
{
    if (!IsAlive(gameObject)) {
        LOGW("EggLog: GameObject is null");
        return;
    }

    Il2CppObject* eggItem =
        GetComponentByClass(gameObject, petEggItemClass);

    if (!IsAlive(eggItem)) {
        LOGI("EggLog: PetEggItem not found");
        return;
    }

    // GameObject / Transform
    auto gameObjectClass = BNM::Class("UnityEngine", "GameObject");
    auto transformClass = BNM::Class("UnityEngine", "Transform");

    auto getName = gameObjectClass.GetMethod("get_name", 0);
    
    auto getPosition = transformClass.GetMethod("get_position", 0);


    auto getTransform = BNM::Class("UnityEngine", "GameObject")
        .GetMethod("get_transform", 0)
        .cast<Il2CppObject*>();
    if (!getTransform.IsValid() || !getPosition.IsValid()) {
        LOGW("EggLog: Transform methods unavailable");
        return;
    }
    Il2CppObject* transform = getTransform[gameObject]();

    if (!IsAlive(transform)) {
        LOGW("EggLog: Transform is null");
        return;
    }

    // Vector3 は値型なので、BNM 側の戻り値取得方法は
    // プロジェクト内で使っている Vector3 の定義に合わせて調整する。
    LOGI("EggLog: gameObject=%p eggItem=%p transform=%p",
             gameObject, eggItem, transform);

    // Collider2D の有無と enabled 状態
    Il2CppObject* collider =
        GetComponentByClass(gameObject, collider2DClass);

    if (!IsAlive(collider)) {
        LOGI("EggLog: Collider2D missing");
        return;
    }

    auto getEnabled = BNM::Class("UnityEngine", "Behaviour")
        .GetMethod("get_enabled", 0);

    if (getEnabled.IsValid()) {
        bool enabled = getEnabled.cast<bool>()(collider);
        LOGI("EggLog: Collider2D=%p enabled=%d",
                 collider,colliderEnabled ? 1 : 0);
    } else {
        LOGW("EggLog: Behaviour.get_enabled unavailable");
    }

    // IsPoopOrRotten の結果
    auto isPoopOrRotten =
        petEggItemClass.GetMethod("IsPoopOrRotten", 0);

    if (isPoopOrRotten.IsValid()) {
        bool result = isPoopOrRotten.cast<bool>()(eggItem);
        LOGI("EggLog: IsPoopOrRotten=%d", result ? 1 : 0);
    } else {
        LOGW("EggLog: IsPoopOrRotten unavailable");
    }
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
    /*

    // 2. ワールド内の卵回収対象を判定
    auto petEggItemClass =
        BNM::Class("M1PetGame", "PetEggItem");

    Il2CppObject* petEggItem =
        GetComponentByClass(gameObject, petEggItemClass);

    if (!IsAlive(petEggItem)) {
        return false;
    }
    */

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
    
    
    bool enabled = getEnabled[collider2D]();
    /*
    LogEggItemInfo(gameObject, petEggItemClass, collider2DClass, enabled);
    LOGI(
        "IsClickable: gameObject=%p collider2D=%p enabled=%d result=%d",
        (void*)gameObject,
        (void*)collider2D,
        enabled ? 1 : 0,
        enabled ? 1 : 0
    );
    */

    return enabled;
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

        if (!IsAlive(object)) {
            continue;
        }

        const bool clickable = IsClickable(object);

        LOGI(
            "DumpClickableObjects: object=%p clickable=%d name=%s",
            (void*)object,
            clickable ? 1 : 0,
            GetObjectName(object).c_str()
        );

        if (!clickable) {
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
        const auto managerClass =
            BNM::Class("M1PetGame", "PetGameManager");

        LogClassMethods("PetEggItem", petEggItemClass);
        LogClassMethods("PetGameManager", managerClass);

        LOGI("PET_EGG target=%s path=%s component=%p",
            matchedName.c_str(),
            matchedPath.c_str(),
            static_cast<void*>(petEggItem));

        return errorJson(
            "PetEggItem detected; inspect PetEggItem and PetGameManager METHOD_SCAN logs"
        );
    }
    return errorJson("No supported clickable component found on target");
}


std::string ClickComponent(
    const std::string& componentType,
    const std::string& target
) {
    auto errorJson = [](const std::string& message) {
        return std::string("{\"ok\":false,\"error\":\"") +
            EscapeJson(message) + "\"}";
    };

    if (!BNM::IsLoaded()) return errorJson("BNM is not loaded");
    if (componentType.empty())
        return errorJson("CLICK_COMPONENT requires a component type");
    if (target.empty())
        return errorJson("CLICK_COMPONENT requires a name or path");

    BNM::Class componentClass;
    if (componentType == "Button") {
        componentClass = BNM::Class("UnityEngine.UI", "Button");
    } else if (componentType == "ToggleEX") {
        componentClass = BNM::Class("", "ToggleEX");
    } else if (componentType == "Toggle") {
        componentClass = BNM::Class("UnityEngine.UI", "Toggle");
    } else if (componentType == "PetEggItem") {
        componentClass = BNM::Class("M1PetGame", "PetEggItem");
    } else {
        return errorJson("Unsupported component type: " + componentType);
    }

    if (!componentClass)
        return errorJson("Component class not found: " + componentType);

    // 型指定でコンポーネントを検索し、対象名/パスでさらに絞り込む。
    auto findObjects = ObjectClass().GetMethod("FindObjectsOfType", 2)
        .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();
    if (!findObjects.IsValid())
        return errorJson("FindObjectsOfType(Type, bool) unavailable");

    auto components = findObjects(componentClass.GetMonoType(), true);
    if (!components)
        return errorJson("FindObjectsOfType returned null");

    auto getGameObject = BNM::Class("UnityEngine", "Component")
        .GetMethod("get_gameObject", 0)
        .cast<Il2CppObject*>();
    if (!getGameObject.IsValid())
        return errorJson("Component.get_gameObject() unavailable");

    Il2CppObject* matchedComponent = nullptr;
    Il2CppObject* matchedGameObject = nullptr;
    std::string matchedName;
    std::string matchedPath;
    std::size_t matches = 0;
    const auto count = static_cast<std::size_t>(components->GetSize());
    auto data = components->GetData();

    for (std::size_t i = 0; i < count; ++i) {
        Il2CppObject* component = data[i];
        if (!IsAlive(component)) continue;

        Il2CppObject* gameObject = getGameObject[component]();
        if (!IsAlive(gameObject)) continue;

        const std::string name = GetObjectName(gameObject);
        const std::string path = GetObjectPath(gameObject);
        if (target != name && target != path) continue;

        ++matches;
        matchedComponent = component;
        matchedGameObject = gameObject;
        matchedName = name;
        matchedPath = path;
    }

    if (matches == 0 || !matchedComponent || !matchedGameObject)
        return errorJson("No component matched the supplied type and name/path");
    if (matches > 1)
        return errorJson("Target is ambiguous; use the full GameObject path");
    if (!GetActiveInHierarchy(matchedGameObject))
        return errorJson("Target GameObject is not active in hierarchy");
    if (!IsClickable(matchedGameObject))
        return errorJson("Target is not currently clickable");

    if (componentType == "PetEggItem") {
        using UnityVector3 = BNM::Structures::Unity::Vector3;

        // PetEggItem の GameObject からワールド座標を取得
        auto getTransform = GameObjectClass()
            .GetMethod("get_transform", 0)
            .cast<Il2CppObject*>();

        auto getPosition = TransformClass()
            .GetMethod("get_position", 0)
            .cast<UnityVector3>();

        if (!getTransform.IsValid() || !getPosition.IsValid()) {
            return errorJson("Transform position methods unavailable");
        }

        Il2CppObject* transform = getTransform[matchedGameObject]();
        if (!IsAlive(transform)) {
            return errorJson("PetEggItem Transform is invalid");
        }

        UnityVector3 worldPosition = getPosition[transform]();

        // ワールド座標をスクリーン座標へ変換
        auto cameraClass = BNM::Class("UnityEngine", "Camera");
        auto behaviourClass = BNM::Class("UnityEngine", "Behaviour");
        auto componentClass = BNM::Class("UnityEngine", "Component");

        auto findCameras = ObjectClass()
            .GetMethod("FindObjectsOfType", 2)
            .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

        auto getCameraEnabled = behaviourClass
            .GetMethod("get_enabled", 0)
            .cast<bool>();

        auto getCameraGameObject = componentClass
            .GetMethod("get_gameObject", 0)
            .cast<Il2CppObject*>();

        auto getTag = GameObjectClass()
            .GetMethod("get_tag", 0)
            .cast<MonoString*>();

        auto worldToScreenPoint = cameraClass
            .GetMethod("WorldToScreenPoint", 1)
            .cast<UnityVector3>();

        if (!findCameras.IsValid() ||
            !getCameraEnabled.IsValid() ||
            !getCameraGameObject.IsValid() ||
            !getTag.IsValid() ||
            !worldToScreenPoint.IsValid()) {
            return errorJson("Camera lookup methods unavailable");
        }

        auto cameras = findCameras(cameraClass.GetMonoType(), true);

        if (!cameras) {
            return errorJson("Camera search returned null");
        }

        Il2CppObject* camera = nullptr;
        Il2CppObject* mainTaggedCamera = nullptr;
        Il2CppObject* topCamera = nullptr;
        int validCameraCount = 0;

        auto cameraData = cameras->GetData();
        const auto cameraCount =
            static_cast<std::size_t>(cameras->GetSize());

        for (std::size_t i = 0; i < cameraCount; ++i) {
            Il2CppObject* candidate = cameraData[i];

            if (!IsAlive(candidate) ||
                !getCameraEnabled[candidate]()) {
                continue;
            }

            Il2CppObject* cameraGO =
                getCameraGameObject[candidate]();

            if (!IsAlive(cameraGO) ||
                !GetActiveInHierarchy(cameraGO)) {
                continue;
            }

            ++validCameraCount;

            MonoString* tagString = getTag[cameraGO]();
            const std::string tag =
                tagString ? tagString->str() : "";

            LOGI(
                "PET_EGG_CAMERA candidate=%p tag=%s",
                static_cast<void*>(candidate),
                tag.c_str()
            );

            if (tag == "MainCamera") {
                if (mainTaggedCamera != nullptr) {
                    return errorJson(
                        "Multiple active cameras have MainCamera tag"
                    );
                }
                mainTaggedCamera = candidate;
            }
            if (tag == "TopCamera") {
                topCamera = candidate;
                LOGI("PET_EGG_CAMERA selected TopCamera=%p", camera);
            }

            camera = candidate;
        }

        if (mainTaggedCamera != nullptr) {
            camera = mainTaggedCamera;
        } else if (validCameraCount != 1) {
            camera = topCamera ;    
        } else if (validCameraCount != 1) {
            return errorJson(
                "Camera.main missing and active camera selection is ambiguous"
            );
        }

        if (!IsAlive(camera)) {
            return errorJson("No usable active Camera found");
        }

        UnityVector3 screenPosition =
            worldToScreenPoint[camera](worldPosition);

        LOGI(
            "PET_EGG_POSITION camera=%p world=(%.2f,%.2f,%.2f) "
            "screen=(%.2f,%.2f,%.2f)",
            static_cast<void*>(camera),
            worldPosition.x, worldPosition.y, worldPosition.z,
            screenPosition.x, screenPosition.y, screenPosition.z
        );
        // PetTapManager のインスタンスを検索
        auto petTapManagerClass =
            BNM::Class("M1PetGame", "PetTapManager");

        if (!petTapManagerClass) {
            return errorJson("PetTapManager class not found");
        }

        auto findManagers = ObjectClass()
            .GetMethod("FindObjectsOfType", 2)
            .cast<BNM::Structures::Mono::Array<Il2CppObject*>*>();

        if (!findManagers.IsValid()) {
            return errorJson("FindObjectsOfType unavailable");
        }

        auto managers = findManagers(
            petTapManagerClass.GetMonoType(),
            true
        );

        if (!managers) {
            return errorJson("PetTapManager search returned null");
        }

        auto getManagerGameObject = BNM::Class("UnityEngine", "Component")
            .GetMethod("get_gameObject", 0)
            .cast<Il2CppObject*>();

        if (!getManagerGameObject.IsValid()) {
            return errorJson("Component.get_gameObject unavailable");
        }

        Il2CppObject* manager = nullptr;
        int activeManagerCount = 0;

        auto managerData = managers->GetData();
        const auto managerCount =
            static_cast<std::size_t>(managers->GetSize());

        for (std::size_t i = 0; i < managerCount; ++i) {
            Il2CppObject* candidate = managerData[i];

            if (!IsAlive(candidate)) {
                continue;
            }

            Il2CppObject* managerGameObject =
                getManagerGameObject[candidate]();

            if (!IsAlive(managerGameObject) ||
                !GetActiveInHierarchy(managerGameObject)) {
                continue;
            }

            manager = candidate;
            ++activeManagerCount;
        }

        if (activeManagerCount != 1 || !IsAlive(manager)) {
            LOGE(
                "PET_EGG_TOUCH manager count=%d",
                activeManagerCount
            );
            return errorJson(
                "Expected exactly one active PetTapManager"
            );
        }

        // private TouchEnd(int touchID, Vector3 touchPosition) を解決
        auto touchEnd = petTapManagerClass
            .GetMethod("TouchEnd", 2)
            .cast<void>();

        if (!touchEnd.IsValid()) {
            LogClassMethods("PetTapManager", petTapManagerClass);
            return errorJson("PetTapManager.TouchEnd(int, Vector3) unavailable");
        }

        // touchID=0、touchPosition=スクリーン座標
        LOGI(
            "PET_EGG_TOUCH invoke manager=%p item=%p "
            "touchID=0 screen=(%.2f,%.2f,%.2f)",
            static_cast<void*>(manager),
            static_cast<void*>(matchedComponent),
            screenPosition.x,
            screenPosition.y,
            screenPosition.z
        );

        touchEnd[manager](0, screenPosition);

        LOGI(
            "PET_EGG_TOUCH TouchEnd returned name=%s",
            matchedName.c_str()
        );

        return std::string("{\"ok\":true,\"type\":\"PetEggItem\","
            "\"name\":\"") + EscapeJson(matchedName) +
            "\",\"path\":\"" + EscapeJson(matchedPath) +
            "\",\"touchID\":0}";        
    } else if (componentType == "Button") {

            auto buttonClass = BNM::Class("UnityEngine.UI", "Button");
            auto isInteractable = BNM::Class("UnityEngine.UI", "Selectable")
                .GetMethod("IsInteractable", 0).cast<bool>();
            if (!isInteractable.IsValid() || !isInteractable[matchedComponent]())
                return errorJson("Button is not interactable");

            auto getOnClick = buttonClass.GetMethod("get_onClick", 0)
                .cast<Il2CppObject*>();
            if (!getOnClick.IsValid())
                return errorJson("Button.get_onClick() unavailable");
            Il2CppObject* unityEvent = getOnClick[matchedComponent]();
            if (!IsAlive(unityEvent))
                return errorJson("Button.onClick is null");

            auto invoke = BNM::Class("UnityEngine.Events", "UnityEvent")
                .GetMethod("Invoke", 0).cast<void>();
            if (!invoke.IsValid())
                return errorJson("UnityEvent.Invoke() unavailable");
            invoke[unityEvent]();
    } else {
        // ToggleEX は UnityEngine.UI.Toggle の派生クラス。
        // isOn setter を使い、ToggleGroup と onValueChanged の通常経路を通す。
        auto toggleClass = BNM::Class("UnityEngine.UI", "Toggle");
        auto isInteractable = BNM::Class("UnityEngine.UI", "Selectable")
            .GetMethod("IsInteractable", 0).cast<bool>();
        if (!isInteractable.IsValid() || !isInteractable[matchedComponent]())
            return errorJson("Toggle is not interactable");

        auto getIsOn = toggleClass.GetMethod("get_isOn", 0).cast<bool>();
        auto setIsOn = toggleClass.GetMethod("set_isOn", 1).cast<void>();
        if (!getIsOn.IsValid() || !setIsOn.IsValid())
            return errorJson("Toggle isOn accessors unavailable");
        setIsOn[matchedComponent](!getIsOn[matchedComponent]());
    }

    LOGI("CLICK_COMPONENT success type=%s name=%s path=%s",
         componentType.c_str(), matchedName.c_str(), matchedPath.c_str());
    return std::string("{\"ok\":true,\"type\":\"") +
        EscapeJson(componentType) + "\",\"name\":\"" +
        EscapeJson(matchedName) + "\",\"path\":\"" +
        EscapeJson(matchedPath) + "\"}";
}

std::string SetBoostFeatureRunning(bool value, const std::string& target) {
    if (!BNM::IsLoaded()) {
        return R"({"ok":false,"error":"BNM is not loaded"})";
    }

    const auto managerClass =
        BNM::Class("M1PetGame", "PetGameManager");

    // Singleton instance
    auto getInstance = managerClass
        .GetMethod("get_Instance", 0)
        .cast<Il2CppObject*>();

    Il2CppObject* manager = getInstance();

    if (!IsAlive(manager)) {
        LOGI("BOOST: PetGameManager instance is null or invalid");
        return R"({"ok":false,"error":"PetGameManager instance is invalid"})";
    }

    // void set_IsBoostFeedFeatureRunning(bool value)
    BNM::MethodBase setBoost;

    if (target == "Feed") {
        setBoost = managerClass
            .GetMethod("set_IsBoostFeedFeatureRunning", 1);
    } else if (target == "Pet") {
        setBoost = managerClass
            .GetMethod("set_IsBoostPetFeatureRunning", 1);
    } else if (target == "Vitamin") {
        setBoost = managerClass
            .GetMethod("set_IsBoostVitaminFeatureRunning", 1);
    } else {
        return R"({"ok":false,"error":"incorrect boost target"})";
    }

    if (!setBoost.IsValid()) {
        LOGI("BOOST: setter not found: target=%s", target.c_str());
        return R"({"ok":false,"error":"boost method is not available"})";
    }

    setBoost.cast<void>()[manager](true);

    LOGI("BOOST: setter invoked successfully");

    return value
        ? R"({"ok":true,"method":"set_IsBoostFeatureRunning","value":true})"
        : R"({"ok":true,"method":"set_IsBoostFeatureRunning","value":false})";
}
    
std::string SetBoost(const std::string& target) {
    std::string result = SetBoostFeatureRunning(true,target);
    return result;
}    

} // namespace XposedPoco