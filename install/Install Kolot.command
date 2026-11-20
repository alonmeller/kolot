#!/bin/bash
# Installs Kolot (AU + VST3) for the current user and makes Logic Pro rescan it.
cd "$(dirname "$0")"
AU=~/Library/Audio/Plug-Ins/Components
VST3=~/Library/Audio/Plug-Ins/VST3
mkdir -p "$AU" "$VST3"
rm -rf "$AU/Kolot.component" "$VST3/Kolot.vst3"
cp -R Kolot.component "$AU/"
cp -R Kolot.vst3 "$VST3/"
xattr -dr com.apple.quarantine "$AU/Kolot.component" "$VST3/Kolot.vst3" 2>/dev/null
[ -d Kolot.app ] && rm -rf /Applications/Kolot.app && cp -R Kolot.app /Applications/ && xattr -dr com.apple.quarantine /Applications/Kolot.app 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Kolot installed."
echo "Open Logic Pro -> new Software Instrument track -> AU Instruments -> Alon Meller -> Kolot"
echo ""
read -n 1 -s -r -p "Press any key to close"
