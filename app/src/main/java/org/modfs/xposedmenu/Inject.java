package org.modfs.xposedmenu;

import android.os.Build;
import android.util.Log;
import androidx.annotation.NonNull;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Objects;

import io.github.libxposed.api.XposedModule;
import io.github.libxposed.api.XposedModuleInterface;

public class Inject extends XposedModule {

   
    private final String target_abi = Build.SUPPORTED_ABIS[0]; // 配列の最初の要素を取得

    // クラスローダーからリソースを取得するヘルパーメソッド
    public static InputStream resourceStream(String name) {
        return Objects.requireNonNull(Inject.class.getClassLoader()).getResourceAsStream(name);
    }

    public Inject() {
        super();
    }

    // 【修正1】コンストラクタの引数を base だけに変更
    public Inject(@NonNull XposedModuleInterface base, @NonNull XposedModuleInterface.ModuleLoadedParam param) {
        super(); // libxposedの親クラスはbaseのみを要求します
    }

    // ターゲットアプリのプロセスが起動すると自動的に呼ばれます
    @Override
    public void onPackageLoaded(@NonNull PackageLoadedParam param) {
        super.onPackageLoaded(param);

        // 1. 動的に現在ロードされたパッケージ名を取得
        String currentApp = param.getPackageName();
        
        // 2. プロセス名も取得して、WebView などのシステムコンポーネントプロセスであれば完全に除外（スルー）する
        String processName = android.os.Build.VERSION.SDK_INT >= 28 ? android.app.Application.getProcessName() : "";
        
        if (currentApp.contains("webview") || processName.contains("webview") || processName.contains("privileged")) {
            return; // WebView やシステム権限プロセスなら即座に処理をスキップ
        }

        // 3. 多重ロードの防止（パッケージごとに管理するか、簡易的にフラグチェック）
        
        try {
            // 【超重要】ハードコーディングを廃止し、取得した currentApp を使って動的にパスを生成
            String pathname = "/data/user/0/" + currentApp + "/cache/libxposedmenu.so";
            File soFile = new File(pathname);

            // 2. モジュール（自分自身）のAPK内から指定ABIの .so を読み込む
            InputStream soFileStream = resourceStream("lib/" + target_abi + "/libxposedmenu.so");
            if (soFileStream == null) {
                log(Log.INFO, "XposedMenu", "Error: libxposedmenu.so not found in resources for ABI: " + target_abi);
                return;
            }

            // 3. ファイルの書き出し
            byte[] soFileContent = new byte[soFileStream.available()];
            int readBytes = soFileStream.read(soFileContent);
            soFileStream.close();

            // ディレクトリがなければ作成
            File parent = soFile.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }

            if (!soFile.exists()) {
                soFile.createNewFile();
            }

            FileOutputStream out = new FileOutputStream(soFile);
            out.write(soFileContent, 0, readBytes);
            out.close();

            // 4. 正式なAPI 102のメソッド名である getDefaultClassLoader() を使用してクラスローダーを取得
            ClassLoader targetClassLoader = param.getDefaultClassLoader();
            if (targetClassLoader == null) {
                log(Log.WARN, "XposedMenu", "Could not retrieve target ClassLoader for " + currentApp);
                return;
            }
            
            // 5. 対象アプリのClassLoaderを使って動的ロードを実行
            /*
            java.lang.reflect.Method nativeLoadMethod = Class.forName("java.lang.Runtime")
                    .getDeclaredMethod("nativeLoad", String.class, ClassLoader.class);
            nativeLoadMethod.setAccessible(true);
            nativeLoadMethod.invoke(null, pathname, targetClassLoader);
            */
            System.load(pathname);


            log(Log.INFO, "XposedMenu", "Successfully injected libxposedmenu.so into " + currentApp);

        } catch (Throwable t) {
            log(Log.ERROR, "XposedMenu", "Failed to inject menu into " + currentApp + ": " + t.getMessage());
            t.printStackTrace();
        }
        
    }

}
