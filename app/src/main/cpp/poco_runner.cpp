#include "poco_runner.hpp"
#include "poco_tree.hpp"
#include "poco_server.hpp"
#include "include/log.h"
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/MethodBase.hpp>
#include <BNM/Utils.hpp>

#include <atomic>
#include <string>

namespace {

constexpr char kRootName[] = "Canvas";

// Unity IL2CPP のインスタンスメソッド用フック
using Il2CppObject = BNM::IL2CPP::Il2CppObject;
using MethodInfo = BNM::IL2CPP::MethodInfo;
using EventSystemUpdateFn = void (*)(Il2CppObject*, MethodInfo*);

EventSystemUpdateFn g_originalUpdate = nullptr;

std::atomic<bool> g_hookInstalled{false};
std::atomic<bool> g_dumpComplete{false};

// EventSystem.Update は Unity のメインスレッドで呼ばれる想定。
// ツリー取得は毎フレーム行わず、Canvas が見つかるまで一定間隔で再試行する。
void EventSystemUpdateHook(Il2CppObject* instance, MethodInfo* method) {

    if (g_originalUpdate != nullptr) {
        g_originalUpdate(instance, method);
    }

    // PC から DUMP 要求が来たフレームだけツリーを取得する
    XposedPoco::RequestType requestType;
    std::string target;
    std::string componentType;

    if (XposedPoco::TryBeginRequest(requestType, target, componentType)) {
        std::string json;

        if (requestType == XposedPoco::RequestType::Clickables) {
            json = XposedPoco::DumpClickableObjects();
        } else if (requestType == XposedPoco::RequestType::Click) {
            json = XposedPoco::ClickGameObject(target);
        } else if (requestType == XposedPoco::RequestType::ClickComponent) {
            json = XposedPoco::ClickComponent(componentType, target);
        } else if (requestType == XposedPoco::RequestType::Boost) {
            json = XposedPoco::SetBoost(target);    
        } else {
            json = XposedPoco::DumpGameObjectTree("Canvas");
        }

        XposedPoco::CompleteDumpRequest(std::move(json));
    }
}

} // namespace

void InstallPocoMainThreadHook() {
    if (g_hookInstalled.exchange(true)) {
        return;
    }

    const auto eventSystem =
        BNM::Class("UnityEngine.EventSystems", "EventSystem");

    const auto update = eventSystem.GetMethod("Update", 0);

    if (!update.IsValid()) {
        g_hookInstalled.store(false);
        LOGE("EventSystem.Update was not found.");
        return;
    }

    if (!BNM::InvokeHook(
            update,
            EventSystemUpdateHook,
            g_originalUpdate)) {
        g_hookInstalled.store(false);
        LOGE("Failed to hook EventSystem.Update.");
        return;
    }

    LOGI("EventSystem.Update hook installed.");
}