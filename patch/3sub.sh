#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
git rm -f app/src/main/cpp/libraries/Dobby
rm -rf .git/modules/app/src/main/cpp/libraries/Dobby
git submodule update --init --recursive
git submodule update --remote --recursive
git submodule add https://github.com/LSPosed/Dobby app/src/main/cpp/libraries/Dobby
cd app/src/main/cpp/libraries/BNM-Android
patch -p1 < $SCRIPT_DIR/BNM.patch
cd