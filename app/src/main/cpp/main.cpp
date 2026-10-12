#include "includes.h"
#include "include/log.h"
#include <unistd.h>
#include "poco_runner.hpp"
#include "poco_server.hpp"
#include <KittyInclude.hpp>
#include <BNM/Utils.hpp>
#include <BNM/Loading.hpp>

JavaVM* g_jvm = nullptr;
static KittyScanner::ElfScanner g_il2cppElf;

static void* KittyMemoryFinder(const char* name, void* data) {
    auto* scanner =
        static_cast<KittyScanner::ElfScanner*>(data);

    if (scanner == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<void*>(scanner->findSymbol(name));
}

static void OnPocoBnmLoaded() {
    LOGI("BNM loaded callback fired");
    InstallPocoMainThreadHook();
}

EGLBoolean (*old_eglSwapBuffers)(...);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &glWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);

    if (!init) {
        SetupImGui();
        init = true;
    }

    ImGuiIO &io = GetIO();
    if (io.WantTextInput && !iskeyboardopen) {
        displayKeyboard();
        iskeyboardopen = true;
    } else if (!io.WantTextInput && iskeyboardopen) {
        displayKeyboard();
        iskeyboardopen = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    NewFrame();

    DrawMenu();

    EndFrame();
    Render();
    glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());

    //Patches();
    return old_eglSwapBuffers(dpy, surface);
}

void *hack_thread(void *) {
    static bool is_initialized = false;
    if (is_initialized) return nullptr;
    is_initialized = true;

    LOGI("Menu step J1 - C++ Initialization started.");    
    // ==========================================
    // ① libil2cpp.so のベースマップ取得
    // ==========================================
    // getElfBaseMap ではなく、ElfScanner::findElf() を使ってベースアドレスを取得します

    auto il2cppElf = KittyScanner::ElfScanner{};

    for (int attempt = 0; attempt < 60; ++attempt) {
        il2cppElf = KittyScanner::ElfScanner::findElf("libil2cpp.so");

        if (il2cppElf.isValid()) {
            LOGI(" findElf succeeded, attempt=%d", attempt + 1);
            break;
        }

        LOGI("findElf failed, attempt=%d", attempt + 1);
        sleep(1);
    }

    if (il2cppElf.isValid()) {
        g_il2cppBaseMap.startAddress = il2cppElf.base();

        LOGI(
            "il2cpp base=%p",
            reinterpret_cast<void*>(g_il2cppBaseMap.startAddress)
        );
    } else {
        g_il2cppBaseMap.startAddress = 0;
        LOGI("libil2cpp.so not found after 60 attempts");
    }        
    
    if (g_il2cppBaseMap.startAddress != 0) {
        static bool bnm_loaded = false;
        if (!bnm_loaded) {
            LOGI("il2cpp base found at %p. Loading BNM...", (void*)g_il2cppBaseMap.startAddress);            
            JNIEnv *currentEnv = nullptr;
            // JNI_OnLoad で保存した g_jvm を使用して安全に env を引き出します
            if (g_jvm != nullptr) {
                jint res = g_jvm->GetEnv((void**)&currentEnv, JNI_VERSION_1_6);
                if (res == JNI_EDETACHED) {
                    g_jvm->AttachCurrentThread(&currentEnv, nullptr);
                }
            }

            if (currentEnv != nullptr) {
                LOGI("hack_thread: reaching BNM initialization");
                BNM::Loading::AddOnLoadedEvent([]() {
                    LOGI("BNM OnLoaded callback fired");
                    InstallPocoMainThreadHook();
                });                
                
                LOGI("preparing custom BNM finder");
                BNM::Loading::SetMethodFinder(
                    KittyMemoryFinder,
                    &g_il2cppElf
                );
                BNM::Loading::AllowLateInitHook();
                LOGI("waiting for libil2cpp.so");
                // ロード済みの ELF をスキャナーに設定
                for (int attempt = 0; attempt < 60; ++attempt) {
                    g_il2cppElf =
                        KittyScanner::ElfScanner::findElf("libil2cpp.so");
                    if (g_il2cppElf.isValid()) {
                        break;
                    }
                    sleep(1);
                }
                if (!g_il2cppElf.isValid()) {
                    LOGI("createWithPath failed");
                    return nullptr; // hack_thread が void* を返す場合
                }
                g_il2cppBaseMap.startAddress = g_il2cppElf.base();
                LOGI(
                    "ELF found at %p",
                    reinterpret_cast<void*>(g_il2cppBaseMap.startAddress)
                );
                // ロード完了イベントは初期化前に登録
                BNM::Loading::AddOnLoadedEvent(OnPocoBnmLoaded);
                LOGI("calling TryLoadByUsersFinder");
                const bool loaded =
                    BNM::Loading::TryLoadByUsersFinder();
                LOGI(
                    "TryLoadByUsersFinder returned=%d",
                    static_cast<int>(loaded)
                );
                for (int attempt = 0; attempt < 30; ++attempt) {
                    if (BNM::IsLoaded()) {
                        LOGI("BNM::IsLoaded is true");
                        break;
                    }
                    LOGI("waiting for BNM, attempt=%d", attempt + 1);
                    sleep(1);
                }
                if (!BNM::IsLoaded()) {
                    LOGI("BNM initialization timed out");
                }    

                LOGI(
                    "TryLoadByJNI returned: %d, BNM::IsLoaded: %d",
                    static_cast<int>(loaded),
                    static_cast<int>(BNM::IsLoaded())
                );
                if (loaded) {
                    XposedPoco::StartCommandServer();
                    bnm_loaded = true;
                    LOGI("BNM loading hooks installed.");
                } else {
                    LOGE("BNM loading setup failed.");
                }
                LOGI("BNM successfully loaded via global JavaVM.");
            } else {
                LOGE("Error: Global JNIEnv not found. Could not initialize BNM!");
            }
        }
    }

    uintptr_t nativeInjectEventAddr = 0;
    int retry_count = 0;

    // 【最終解決策】OSのキャッシュ起因によるタイミングのズレを完全に破壊するループ
    // アドレスが正常に抜けるまで、最大15回（約1.5秒間）粘り強く再検索を繰り返します。
    while (nativeInjectEventAddr == 0 && retry_count < 15) {
        auto unityELF = KittyScanner::ElfScanner::findElf("libunity.so");
        
        if (unityELF.isValid()) {
            RegisterNativeFn fnInfo = unityELF.findRegisterNativeFn("nativeInjectEvent", "(Landroid/view/InputEvent;I)Z");
            if (!fnInfo.isValid())
                fnInfo = unityELF.findRegisterNativeFn("nativeInjectEvent", "(Landroid/view/InputEvent;)Z");
            if (!fnInfo.isValid())
                fnInfo = unityELF.findRegisterNativeFn("nativeInjectEvent", "");

            if (fnInfo.isValid()) {
                nativeInjectEventAddr = (uintptr_t)fnInfo.fnPtr;
            } else {
                nativeInjectEventAddr = unityELF.findSymbol("nativeInjectEvent");
            }
        }

        // まだ見つからない場合は、100ミリ秒（0.1秒）だけ待ってから次のループへ進む
        if (nativeInjectEventAddr == 0) {
            LOGW("libunity.so address not ready yet (Retry %d/15)... Waiting...", retry_count + 1);
            usleep(100 * 1000); // 100ms スリープ (unistd.h が必要です)
            retry_count++;
        }
    }

    // ② 最終判定部分
    if (nativeInjectEventAddr != 0) {
        LOGI("Found nativeInjectEvent after %d retries! Address: %p", retry_count, (void*)nativeInjectEventAddr);
        DobbyHook((void*)nativeInjectEventAddr, (dobby_dummy_func_t)nativeInjectEvent, (dobby_dummy_func_t *)&old_nativeInjectEvent);
    } else {
        LOGE("InjectEventPtr is dead after 15 retries, menu unable to initialize.");
        return nullptr;
    }


    Pointers();
    //InitPatches();
    //Hooks();
    //DobbyHookSM("libEGL.so", "eglSwapBuffers", (void *) hook_eglSwapBuffers, (void **) &old_eglSwapBuffers);
    LOGI("Menu finished loading");

    return nullptr;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    g_jvm = vm;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    jclass UnityPlayer_cls = env->FindClass("com/unity3d/player/UnityPlayer");
    if (UnityPlayer_cls != nullptr) {
        // フィールドの取得を試みる
        UnityPlayer_CurrentActivity_fid = env->GetStaticFieldID(UnityPlayer_cls, "currentActivity", "Landroid/app/Activity;");
        
        // 【最重要】もし難読化等で見つからなかった場合、Java例外をクリアしてクラッシュを防ぐ
        if (env->ExceptionCheck()) {
            env->ExceptionClear(); // 安全装置：エラーを消去して処理を続行させる
            LOGW("Warning: UnityPlayer.currentActivity not found (Obfuscated?).");
            UnityPlayer_CurrentActivity_fid = nullptr;
        }
    } else {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGW("Warning: com/unity3d/player/UnityPlayer class not found.");
    }      

    int ret;
    pthread_t ntid;
    if ((ret = pthread_create(&ntid, nullptr, hack_thread, nullptr)))
        LOGE("can't create thread: %s\n", strerror(ret));
    return JNI_VERSION_1_6;
}