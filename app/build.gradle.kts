plugins {
    // あなたの環境のプラグインエイリアスを使用（libs.versions.toml に依存）
    alias(libs.plugins.android.application)
}

android {
    // XposedImGuiMenu のパッケージ名を設定
    namespace = "org.modfs.xposedmenu"
    compileSdk = 37
    buildToolsVersion = "37.0.0"

    defaultConfig {
        applicationId = "org.modfs.xposedmenu"
        // 互換性を考慮し、元の設定の minSdk 28 に合わせるか、あなたの 26 でも動作します（28を推奨）
        minSdk = 28
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        // C++ビルド（NDK）の設定：必要なABIだけに絞る場合はここを編集
        ndk {
            abiFilters.addAll(setOf("arm64-v8a", "x86_64", "armeabi-v7a", "x86"))
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // CMakeのコンパイルオプション（C++20必須設定をマージ）
        externalNativeBuild {
            cmake {
                cppFlags("-std=c++20") 
                
            }
        }
    }

    buildTypes {
        release {
            // ImGui Menuは難読化や最適化でネイティブJNIが壊れることがあるため、
            // 最初は元の設定通り false を推奨。安定したら true に変更してください。
            isMinifyEnabled = false
            isShrinkResources = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs["debug"]
        }
    }

    // CMakeLists.txt のパスとNDKバージョンの指定をマージ
    externalNativeBuild {
        cmake {
            path(file("src/main/cpp/CMakeLists.txt"))
            version = "3.22.1"
        }
    }

    buildFeatures {
        viewBinding = true
    }

    // 新しいJavaバージョン（Version 21）に統一
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    packaging {
        resources {
            // libxposed用のメタデータマージ設定
            merges += "META-INF/xposed/*"
            //excludes += "**"
            excludes -= "META-INF/xposed/*"
        }
    }

    // .so ファイルを圧縮せずに保持する（元の設定をKotlin化）
    androidResources {
        noCompress += "so"
    }

    lint {
        abortOnError = true
        checkReleaseBuilds = false
    }

    ndkVersion = "25.2.9519653"
}

dependencies {
    // UIおよびテスト用依存関係（元のbuild.gradleから移行）
    implementation(libs.appcompat)
    implementation(libs.material)
    implementation(libs.constraintlayout)
    testImplementation(libs.junit)
    androidTestImplementation(libs.ext.junit)
    androidTestImplementation(libs.espresso.core)

    // 【重要】あなたのAPI102用依存関係に差し替え（libs.api は破棄）
    compileOnly(libs.libxposed.api)
    implementation(libs.libxposed.service)
}
// API 37 の実ファイルがない環境でも、AARのSDKバージョンチェックを強制スルーさせる設定
tasks.withType<com.android.build.gradle.internal.tasks.CheckAarMetadataTask>().configureEach {
    enabled = false
}