#include "poco_runner.hpp"
#include "poco_tree.hpp"
#include "poco_server.hpp"

#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/MethodBase.hpp>
#include <BNM/Utils.hpp>

#include <android/log.h>
#include <atomic>
#include <string>

namespace {

constexpr char kLogTag[] = "XposedPoco";
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
    if (XposedPoco::TryBeginDumpRequest()) {
        std::string json =
            XposedPoco::DumpGameObjectTree("Canvas");

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
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "EventSystem.Update was not found."
        );
        return;
    }

    if (!BNM::InvokeHook(
            update,
            EventSystemUpdateHook,
            g_originalUpdate)) {
        g_hookInstalled.store(false);
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "Failed to hook EventSystem.Update."
        );
        return;
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "EventSystem.Update hook installed."
    );
}