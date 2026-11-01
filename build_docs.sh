#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

DOCS_DIR="docs"
API_DOCS_DIR="${DOCS_DIR}/api"
HS_DIR="haskell"
SPEC_MD="${DOCS_DIR}/system_spec.md"
SPEC_PDF="${DOCS_DIR}/system_spec.pdf"
SPEC_HTML="${DOCS_DIR}/system_spec.html"

echo "[1/3] Setting up documentation directory..."
mkdir -p "${API_DOCS_DIR}"

echo "[2/3] Generating Haddock API documentation..."
# テンポラリディレクトリで MicroHs 向け構文を Haddock 向けに前処理
TMP_HS_DIR=$(mktemp -d)
trap 'rm -rf "${TMP_HS_DIR}"' EXIT

for src in "${HS_DIR}/Logic.hs" "${HS_DIR}/MonitorLogic.hs"; do
  if [ -f "${src}" ]; then
    fname=$(basename "${src}")
    # MicroHs 固有のヘッダやインクルードがあれば整形して一時退避
    sed -e 's/^module Main/-- module Main/' "${src}" > "${TMP_HS_DIR}/${fname}"
  fi
done

if command -v haddock >/dev/null 2>&1; then
  haddock -h -o "${API_DOCS_DIR}" \
    --title="M5-MicroHs Remote Vibration Monitor API" \
    "${TMP_HS_DIR}/Logic.hs" "${TMP_HS_DIR}/MonitorLogic.hs" 2>/dev/null || true
  echo "Haddock documentation generated in ${API_DOCS_DIR}/"
else
  echo "haddock not found. Skipped HTML API doc generation."
fi

echo "[3/3] Building system specification document..."
if command -v pandoc >/dev/null 2>&1; then
  # HTML 生成
  pandoc "${SPEC_MD}" -o "${SPEC_HTML}" \
    --standalone --toc --number-sections --metadata title="遠隔振動監視システム 仕様書" || true
  echo "Specification HTML generated: ${SPEC_HTML}"

  # PDF 生成 (xelatex/lualatex がある場合)
  if command -v xelatex >/dev/null 2>&1 || command -v lualatex >/dev/null 2>&1; then
    PDF_ENGINE=$(command -v xelatex || command -v lualatex)
    pandoc "${SPEC_MD}" -o "${SPEC_PDF}" \
      --pdf-engine="${PDF_ENGINE}" --toc --number-sections 2>/dev/null || true
    if [ -f "${SPEC_PDF}" ]; then
      echo "Specification PDF generated: ${SPEC_PDF}"
    fi
  fi
else
  echo "pandoc not found. Skipped spec conversion."
fi

echo "Documentation build completed successfully."