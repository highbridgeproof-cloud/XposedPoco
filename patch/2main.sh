#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
META_DIR="app/src/main/resources/META-INF/xposed"
mkdir -p $META_DIR
cp $SCRIPT_DIR/java_init.list $META_DIR/java_init.list
cp $SCRIPT_DIR/module.prop $META_DIR/module.prop
cp $SCRIPT_DIR/scope.list $META_DIR/scope.list
cp $SCRIPT_DIR/build.gradle_pro.kts build.gradle.kts
cp $SCRIPT_DIR/build.gradle_app.kts app/build.gradle.kts
rm build.gradle
rm app/build.gradle
patch -p1 < $SCRIPT_DIR/base.patch