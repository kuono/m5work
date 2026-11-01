# M5-MicroHs 遠隔振動監視システム (v1.0.0)

[日本語](README.md) | [English](README_en.md) | [简体中文](README_zh.md)

M5StickC Plus2（振動検知センサ）と M5Core2（集中監視モニタ）を ESP-NOW で連携させた、高信頼遠隔状態監視システムです。  
システムのコア判定ロジックには、組み込み向け Haskell 処理系 **MicroHs** を用いて C 言語へトランスパイルした純粋関数型ステートマシンを採用しています。

---

## 主な特徴

- **関数型リアクティブコア**: センサ側・受信機側の状態遷移ロジックを Haskell で記述。2.0秒ホールド遅延や4段階の感度プリセット判定を純粋関数として実装。
- **低遅延・高堅牢な通信**: ESP-NOW（暗号化対応）を用いたダイレクト通信。Wi-Fiルーター不要のピアツーピア接続。
- **動的 UI レイアウト**: LittleFS（SDカードフォールバック対応）上の `layout.txt` からUI座標や配色を動的にロード。
- **再現性の高いビルドパイプライン**: `Makefile` による Haskell トランスパイルから PlatformIO ファームウェアビルド、実機書き込み、ドキュメント自動生成までの一元管理。

---

## システムアーキテクチャ

```text
[ 振動検出子機 ]
  M5StickC Plus2 (IMU: MPU6886)
      │
      ▼ (トランスパイル)
  Haskell Core (Logic.hs -> Logic.c)
      │
      ▼ [ ESP-NOW (Encrypted) ]
      │
[ 集中監視親機 ]
  M5Core2 (TFT LCD / Touch / LittleFS)
      │
      ▼ (トランスパイル)
  Haskell Monitor (MonitorLogic.hs -> MonitorLogic.c)
```

---

## ディレクトリ構成

```text
.
├── Makefile                # ビルド自動化パイプライン
├── build_docs.sh           # 仕様書・APIドキュメント生成スクリプト
├── common/                 # 共通ヘッダおよびランタイム
│   ├── espnow/             # ESP-NOW 通信設定 (秘密鍵テンプレート)
│   └── microhs_runtime/    # マイコン向け修正適用済み MicroHs ランタイム (eval.c)
├── core2_receiver/         # 親機 (M5Core2) PlatformIO プロジェクト
├── m5stick_sensor/         # 子機 (M5StickC Plus2) PlatformIO プロジェクト
├── haskell/                # コア判定ロジック Haskell ソース
│   ├── Logic.hs            # センサ側ロジック
│   └── MonitorLogic.hs     # 親機側ロジック
└── docs/                   # システム仕様書および生成ドキュメント
```

---

## 前提条件

- **ホスト環境**: macOS / Linux (zsh or bash)
- **ビルドツール**:
  - [PlatformIO Core (CLI)](https://platformio.org/)
  - GNU Make
  - (任意) MicroHs (`mhs`) — ロジックを再編集して C を再生成する場合のみ
  - (任意) Pandoc, Haddock — 仕様書・API ドキュメントをビルドする場合

---

## クイックスタート

### 1. 通信設定ファイルの作成
セキュリティ保護のため、ESP-NOW の PMK/LMK および MAC アドレス設定はリポジトリから除外されています。テンプレートから実設定ファイルを作成してください。

```bash
cp common/espnow/espnow_config.h.example common/espnow/espnow_config.h
```
必要に応じて `common/espnow/espnow_config.h` を開き、暗号化キーや接続先 MAC アドレスを調整します。

### 2. ファームウェアのビルド
プロジェクトルートで `make all` を実行すると、両機のファームウェアが自動構築されます。

```bash
make all
```

### 3. 実機への書き込み

USB ケーブルで各端末を接続し、個別に書き込みを実行します。

```bash
# 子機 (M5StickC Plus2)
make upload-sensor

# 親機 (M5Core2) ファームウェア
make upload-receiver

# 親機 UI 設定ファイル (LittleFS)
make upload-fs
```

---

## 主な Makefile コマンド

| コマンド | 説明 |
|---|---|
| `make all` | 子機・親機両方のファームウェアをビルド |
| `make haskell` | `haskell/*.hs` を C コードへトランスパイル |
| `make sensor` | 子機ファームウェアのみビルド |
| `make receiver` | 親機ファームウェアのみビルド |
| `make upload-sensor` | 子機にファームウェアを書き込み |
| `make upload-receiver` | 親機にファームウェアを書き込み |
| `make upload-fs` | 親機の LittleFS (`data/layout.txt`) を書き込み |
| `make monitor-sensor` | 子機のシリアルモニタを起動 |
| `make monitor-receiver` | 親機のシリアルモニタを起動 |
| `make doc` | 仕様書 (HTML/PDF) および API ドキュメントを再生成 |
| `make clean` | PlatformIO キャッシュおよびトランスパイル中間物を削除 |

---

## ライセンス

本プロジェクトは MIT ライセンスの下で公開されています。詳細は [LICENSE](LICENSE) を参照してください。