#!/bin/bash
# Prepare a separate Developer ID candidate, or finish its Apple notarization.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export DESKPORT_TARGET=macos
source "$repo/scripts/build-paths.sh"
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
mode=${1:?Usage: release-macos.sh prepare SOURCE_APP OUTPUT_DIR | notarize OUTPUT_DIR}
: "${DESKPORT_SIGN_IDENTITY:?Set the exact Developer ID Application certificate SHA-1}"
: "${DESKPORT_SIGN_TEAM:?Set the Developer ID team}"
case "$mode" in
    prepare) [ "$#" -eq 3 ] || { echo 'Usage: release-macos.sh prepare SOURCE_APP OUTPUT_DIR' >&2; exit 2; }; output=$3 ;;
    notarize) [ "$#" -eq 2 ] || { echo 'Usage: release-macos.sh notarize OUTPUT_DIR' >&2; exit 2; }; output=$2 ;;
    *) echo "Unknown mode: $mode" >&2; exit 2 ;;
esac
python3 -B "$repo/scripts/build_storage.py" validate-app "$output" >/dev/null
storage_prepare
scratch_root="$DESKPORT_MACOS_BUILD_DIR/release-scratch.noindex"
mkdir -p "$scratch_root"
stage=
check=
if [ "$mode" = prepare ]; then
    source_app=${2:?Source application required}
    output=${3:?New output directory required}
    if [ -e "$output" ]; then
        echo "Output exists; choose a new directory to preserve earlier artifacts." >&2
        exit 1
    fi
    # Exercise this exact session's key access and timestamp service first.
    probe=$(mktemp -d "$scratch_root/.signing-probe.XXXXXX")
    trap 'rm -rf "$probe"' EXIT
    cp /usr/bin/true "$probe/probe"
    codesign --force --sign "$DESKPORT_SIGN_IDENTITY" --timestamp --options runtime "$probe/probe"
    codesign --verify --strict -R "=anchor apple generic and certificate leaf[field.1.2.840.113635.100.6.1.13] exists and certificate leaf[subject.OU] = \"$DESKPORT_SIGN_TEAM\"" "$probe/probe"
    python3 "$repo/scripts/check-macos-bundle.py" "$source_app"
    mkdir -p "$output"
    output=$(cd "$output" && pwd)
    ditto "$source_app" "$output/DeskPort.app"
    chmod -R u+w "$output/DeskPort.app"
    cp "$repo/docs/BUNDLED_COMPONENTS.md" "$output/DeskPort.app/Contents/Resources/"
    python3 "$repo/scripts/sign-macos-distribution.py" "$output/DeskPort.app" \
        --identity "$DESKPORT_SIGN_IDENTITY" --team "$DESKPORT_SIGN_TEAM"
    python3 "$repo/scripts/check-macos-bundle.py" "$output/DeskPort.app"
    ditto -c -k --norsrc --noextattr --noacl --keepParent "$output/DeskPort.app" "$output/submission.zip"
    shasum -a 256 "$output/submission.zip" > "$output/submission.sha256"
    echo 'SIGNED ONLY: Apple notarization has not completed. Do not publish this candidate.' > "$output/STATUS.txt"
    echo "Prepared: $output"
    exit 0
elif [ "$mode" != notarize ]; then
    echo "Unknown mode: $mode" >&2
    exit 1
fi
output=$(cd "${2:?Candidate output directory required}" && pwd)
: "${DESKPORT_NOTARY_PROFILE:?Set the notarytool Keychain profile name}"
app="$output/DeskPort.app"
python3 "$repo/scripts/sign-macos-distribution.py" "$app" --verify-only --team "$DESKPORT_SIGN_TEAM"
python3 "$repo/scripts/check-macos-bundle.py" "$app"
shasum -a 256 -c "$output/submission.sha256"

# Save the submission ID immediately. A retry polls it instead of uploading again.
# Logs stay in the ignored candidate directory, outside public source control.
notarize() {
    local artifact=$1 label=$2 id status
    if [ ! -s "$output/$label-submit.json" ]; then
        xcrun notarytool submit "$artifact" --keychain-profile "$DESKPORT_NOTARY_PROFILE" \
            --output-format json > "$output/$label-submit.json"
    fi
    id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$output/$label-submit.json")
    echo "Apple submission: $id"
    while true; do
        xcrun notarytool info "$id" --keychain-profile "$DESKPORT_NOTARY_PROFILE" \
            --output-format json > "$output/$label-info.json"
        status=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["status"])' "$output/$label-info.json")
        echo "$label: $status"
        case "$status" in
            Accepted) break ;;
            'In Progress') sleep 15 ;;
            *) xcrun notarytool log "$id" --keychain-profile "$DESKPORT_NOTARY_PROFILE" \
                   "$output/$label-log.json" || true
               echo "Apple did not accept $label; see local logs." >&2; return 1 ;;
        esac
    done
}
notarize "$output/submission.zip" app
xcrun stapler staple "$app"
xcrun stapler validate "$app"
codesign --verify --deep --strict "$app"
spctl --assess --type execute --verbose=2 "$app"
version=$(/usr/libexec/PlistBuddy -c 'Print :DeskPortDisplayVersion' "$app/Contents/Info.plist")
dmg="$output/DeskPort-$version-macos-arm64.dmg"
zip="$output/DeskPort-$version-macos-arm64.zip"
if [ ! -s "$output/dmg-submit.json" ]; then
    stage=$(mktemp -d "$scratch_root/.dmg-stage.XXXXXX")
    trap 'rm -rf "$stage"' EXIT
    ditto "$app" "$stage/DeskPort.app"
    ln -s /Applications "$stage/Applications"
    hdiutil create -volname DeskPort -srcfolder "$stage" -ov -format UDZO "$dmg"
    codesign --force --sign "$DESKPORT_SIGN_IDENTITY" --timestamp "$dmg"
fi
notarize "$dmg" dmg
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
rm -f "$zip"
ditto -c -k --norsrc --noextattr --noacl --keepParent "$app" "$zip"
check=$(mktemp -d "$scratch_root/.zip-check.XXXXXX")
trap 'rm -rf "${stage:-}" "$check"' EXIT
/usr/bin/unzip -q "$zip" -d "$check"
codesign --verify --deep --strict "$check/DeskPort.app"
xcrun stapler validate "$check/DeskPort.app"
spctl --assess --type execute --verbose=2 "$check/DeskPort.app"
python3 "$repo/scripts/check-macos-bundle.py" "$check/DeskPort.app"
shasum -a 256 "$dmg" "$zip" > "$output/SHA256SUMS"
echo 'NOTARIZED: app and DMG tickets validated; extracted ZIP passed Gatekeeper and bundled Sunshine loader smoke.' > "$output/STATUS.txt"
cat "$output/STATUS.txt" "$output/SHA256SUMS"
