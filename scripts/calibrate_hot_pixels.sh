#!/usr/bin/env bash
# ===========================================================================
# calibrate_hot_pixels.sh – ホットピクセルキャリブレーションスクリプト
#
# 概要:
#   metavision_active_pixel_detection を5回実行し、
#   全測定に共通するホットピクセルを特定して config/hot_pixels.json に保存する。
#
# 使用方法:
#   ./scripts/calibrate_hot_pixels.sh            # プロジェクトルートから実行
#   ./scripts/calibrate_hot_pixels.sh -n 10      # 測定回数を変更
#
# 注意:
#   - 遮光環境で実行してください
#   - metavision_active_pixel_detection の GUI 上でスペースキー操作が必要です
# ===========================================================================
set -euo pipefail

# -------------------------------------------------------------------------
# デフォルト設定
# -------------------------------------------------------------------------
NUM_MEASUREMENTS=5
SOURCE_DIR="${HOME}/.local/share/metavision/hal"
SOURCE_FILE="active_pixel_calib.txt"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# -------------------------------------------------------------------------
# ヘルプ表示
# -------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

ホットピクセルキャリブレーションスクリプト。
metavision_active_pixel_detection を複数回実行し、
全測定に共通するホットピクセルを config/hot_pixels.json に保存します。

OPTIONS:
  -n NUM    測定回数 (default: ${NUM_MEASUREMENTS})
  -s DIR    active_pixel_calib.txt の検索元ディレクトリ
            (default: ${SOURCE_DIR})
  -h        このヘルプを表示

EXAMPLES:
  $(basename "$0")             # 5回測定（デフォルト）
  $(basename "$0") -n 10       # 10回測定
EOF
    exit 0
}

# -------------------------------------------------------------------------
# 引数パース
# -------------------------------------------------------------------------
while getopts "n:s:h" opt; do
    case "$opt" in
        n) NUM_MEASUREMENTS="$OPTARG" ;;
        s) SOURCE_DIR="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

# -------------------------------------------------------------------------
# 入力チェック
# -------------------------------------------------------------------------
if ! command -v metavision_active_pixel_detection &>/dev/null; then
    echo "[ERROR] metavision_active_pixel_detection が見つかりません。"
    echo "        Metavision SDK がインストールされていることを確認してください。"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo "[ERROR] python3 が見つかりません。"
    exit 1
fi

# -------------------------------------------------------------------------
# 一時ディレクトリの作成
# -------------------------------------------------------------------------
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/hp_calibration_XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT

echo "=============================================="
echo " ホットピクセルキャリブレーション"
echo "=============================================="
echo ""
echo "  測定回数     : ${NUM_MEASUREMENTS}"
echo "  ソースパス   : ${SOURCE_DIR}/${SOURCE_FILE}"
echo "  一時保存先   : ${TMP_DIR}/"
echo "  出力先       : ${PROJECT_ROOT}/config/hot_pixels.json"
echo ""
echo "----------------------------------------------"
echo " ※ 遮光環境を確認してください"
echo " ※ カメラが接続されていることを確認してください"
echo "----------------------------------------------"
echo ""
read -rp "準備ができたら Enter を押して開始 → "
echo ""

# -------------------------------------------------------------------------
# メインループ: 測定を繰り返す
# -------------------------------------------------------------------------
for i in $(seq 1 "$NUM_MEASUREMENTS"); do
    echo "━━━ 測定 ${i}/${NUM_MEASUREMENTS} ━━━"

    # 既存の結果ファイルを削除（前回の残留を防止）
    if [ -f "${SOURCE_DIR}/${SOURCE_FILE}" ]; then
        rm -f "${SOURCE_DIR}/${SOURCE_FILE}"
    fi

    # metavision_active_pixel_detection を起動
    echo "  🔬 metavision_active_pixel_detection を起動します..."
    echo "     → GUI 上でスペースキーを押して検出を完了してください"
    echo ""

    metavision_active_pixel_detection || {
        echo "[WARNING] 検出コマンドが異常終了しました (exit code: $?)"
    }

    # 結果ファイルを一時ディレクトリにコピー
    SRC="${SOURCE_DIR}/${SOURCE_FILE}"
    WAITED=0
    while [ ! -f "$SRC" ] && [ "$WAITED" -lt 60 ]; do
        sleep 1
        WAITED=$((WAITED + 1))
    done

    if [ -f "$SRC" ]; then
        DEST_NAME="measurement_$(printf '%02d' "$i").txt"
        cp "$SRC" "${TMP_DIR}/${DEST_NAME}"
        echo "  📁 保存完了: ${DEST_NAME}"
        rm -f "$SRC"
    else
        echo "  ❌ ${SOURCE_FILE} が見つかりませんでした (60秒タイムアウト)。"
        echo "     この測定をスキップします。"
    fi

    echo ""

    # 最終回以外は操作待ち
    if [ "$i" -lt "$NUM_MEASUREMENTS" ]; then
        read -rp "  次の測定に進むには Enter を押してください → "
        echo ""
    fi
done

# -------------------------------------------------------------------------
# パーススクリプトで共通ホットピクセルを特定
# -------------------------------------------------------------------------
echo ""
echo "━━━ 解析 ━━━"

COLLECTED_FILES=("${TMP_DIR}"/measurement_*.txt)
NUM_COLLECTED=${#COLLECTED_FILES[@]}

if [ "$NUM_COLLECTED" -eq 0 ]; then
    echo "[ERROR] 測定データが1件もありません。キャリブレーション失敗。"
    exit 1
fi

echo "  ${NUM_COLLECTED} 件の測定データを解析します..."

python3 "${SCRIPT_DIR}/parse_hot_pixels.py" \
    "${TMP_DIR}" \
    -o "${PROJECT_ROOT}/config/hot_pixels.json"

echo ""
echo "=============================================="
echo " キャリブレーション完了"
echo "=============================================="
echo ""
echo "次のステップ:"
echo "  ホットピクセルマスク付きで録画:"
echo "    ./build/event_recorder"
echo ""
