#!/bin/bash
sudo apt-get update && sudo apt-get install -y openjdk-21-jdk
mkdir -p /home/codespace/android-sdk/cmdline-tools
wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip commandlinetools-linux-11076708_latest.zip -d /home/codespace/android-sdk/cmdline-tools
mv /home/codespace/android-sdk/cmdline-tools/cmdline-tools /home/codespace/android-sdk/cmdline-tools/latest
sdkmanager "platform-tools" "platforms;android-37.0" "build-tools;37.0.0" "ndk;25.2.9519653"
mv /home/codespace/android-sdk/platforms/android-37.0 /home/codespace/android-sdk/platforms/android-37
sed -i 's/android-37.0/android-37/g' /home/codespace/android-sdk/platforms/android-37/package.xml