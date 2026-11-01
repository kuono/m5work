---
title: "M5-MicroHs 遠隔振動監視システム 仕様書"
subtitle: "M5StickC Plus2 (センサ) × M5Core2 (集中モニタ)"
date: "2026-11-01"
author: "info@kuono.net"
toc: true
numbersections: true
---

## システム概要

本システムは、工作機械や産業装置の稼働状況を遠隔監視するための小型組込システムである。

子機（M5StickC Plus2）が内蔵 6 軸 IMU から振動を検知・判定し、暗号化 ESP-NOW 通信を用いて親機（M5Core2）へパケットを送信する。親機は状態機械に基づいてディスプレイ表示およびブザー警告を行う。

ロジック層とハードウェア層を厳密に分離するため、**Haskell（MicroHs）による純粋状態機械** と **C++ による薄いハードウェア抽象化レイヤー（HAL）** の協調アーキテクチャを採用している。

## システム構成

| 区分 | 機種 | 役割 | 主要コンポーネント | 
| ----- | ----- | ----- | ----- | 
| **子機 (Sensor)** | M5StickC Plus2 | 振動計測・しきい値判定・送信 | MPU6886 (IMU), ESP-NOW (送信), MicroHs, NVS (Preferences) | 
| **親機 (Receiver)** | M5Core2 | 監視・UI表示・音響警告 | 320x240 LCD, ESP-NOW (受信), MicroHs, LittleFS/SD | 

## 通信仕様 (ESP-NOW)

* **プロトコル**: ESP-NOW (Wi-Fi STA モード, チャンネル 0)
* **暗号化**: PMK / LMK による AES-128 ビット暗号化
* **送信周期**: 20 Hz (50 ms 間隔)
* **タイムアウト判定**: 3000 ms 受信途絶で `DISCONNECTED` 状態へ遷移

### パケット構造 (`PacketData` / 12 バイト)

```c
typedef struct {
    uint8_t status;    // 0: STOPPED, 1: RUNNING
    float delta;       // 振動ノルム変動量 (単位: G)
    int8_t battery;    // バッテリ残量 (0..100%, 負値は給電中)
} PacketData;
```

## 子機（Sensor）機能仕様

### 振動検知とオフディレイ保持 (`Logic.hs`)

* **IMU 差分ノルム計算**: 3軸合成加速度ベクトルの前回値との差分絶対値 $\Delta G = \vert{}\Vert{}\mathbf{a}_t\Vert{} - \Vert{}\mathbf{a}_{t-1}\Vert{}\vert{}$ を算出。
* **オフディレイタイマー**: 振動ノルムがしきい値を超えた瞬間から **2.0秒間（2000 ms）** は `RUNNING` 状態をホールド。機械動作に伴う間欠的な振動の谷間による判定バタつきを防止。

### 現場しきい値切替機能 (子機 BtnB)

子機側面ボタン（BtnB）をクリックするたびに感度プリセットが順次切り替わり、ESP32 内蔵 NVS（Preferences）に自動保存される（電源切断後も保持）。

| レベル | 表示ラベル | しきい値 ($\Delta G$) | 想定機器・用途 | 
| ----- | ----- | ----- | ----- | 
| **Lv.1** | `Lv.1 High` | $0.10\,\text{G}$ | 精密機器、小型弱振動モーター | 
| **Lv.2** | `Lv.2 Mid` | $0.25\,\text{G}$ | 一般工作機械（旋盤・フライス等、デフォルト） | 
| **Lv.3** | `Lv.3 Low` | $0.50\,\text{G}$ | プレス機・大型コンプレッサー等 | 
| **Lv.4** | `Lv.4 Min` | $1.00\,\text{G}$ | 激しい振動を伴う破砕機・衝撃検知 | 

## 親機（Receiver）機能仕様

### 状態機械コアロジック (`MonitorLogic.hs`)

```text
                 子機パケット受信 (status = 1)
       ┌──────────────────────────────────────────────┐
       │                                              ▼
[DISCONNECTED] ──(パケット受信: status = 0)──> [STOPPED]
       ▲                                         │   ▲
       │                                         │   │ 振動停止 (2s後)
       │ 3000ms 受信なし                         ▼   │
       └───────────────────────────────────── [RUNNING]
```

* **STOPPED**: 機械停止。ミュートモードが `ModeNormal` の場合、断続警告音（1500Hz）を吹鳴。
* **RUNNING**: 機械稼働中。正常動作。
* **DISCONNECTED**: 通信途絶。復旧・断絶時に単発の通知音を出力。

### ミュートモード制御 (親機 BtnB)

親機下部の中央タッチボタン（BtnB）により、ミュート状態が順次トグルする。

1. **`Persistent Mute` (永続ミュート)**: 停止警告音を無期限で消音。**※机上テスト時の騒音防止のため、起動時初期値に設定。**
2. **`Active` (通常モード)**: 停止時に警告音を吹鳴。
3. **`Mute (1x)` (1回限定ミュート)**: 現在の停止警告音を消音。機械が一度稼働（RUNNING）に戻ると自動的に `Active` へ復帰。

### 現場調整用テンプレート分離 (`layout.txt`)

UI の文言、座標、フォントサイズ、配色を外部テキストファイルから読み込む。Haskell 側を再コンパイルすることなく画面レイアウトの調整が可能。

* **探索優先度**: microSDカード (`/layout.txt`) ➔ 内蔵フラッシュ LittleFS (`/layout.txt`) ➔ 内蔵デフォルト設定
* **動的スロット設計**:
  * `Slot 1`: ミュート状態文字列 (`Buzzer: %s`)
  * `Slot 2`: 振動値 (`Vibe: %.2f G`)
  * `Slot 3`: バッテリ情報 (`Stick:%3d%% | Core2:%3d%%`)

### 保守情報ポップアップ (BtnA)

親機左下のタッチボタン（BtnA）で「SYSTEM INFO」オーバーレイを表示・非表示。

* **表示項目**:
  * ホスト MAC アドレス (`esp_read_mac` による eFuse 固有値)
  * 相手機 (子機) MAC アドレス
  * システム連続稼働時間 (Uptime)
  * 累積受信パケット数 (Rx Packets)
* **描画設計**: 初回開閉時のみ枠を描画し、数値変化時のみ部分上書きすることでフリッカー（画面のちらつき）を完全に抑止。

## ビルド＆デプロイ (`Makefile`)

プロジェクトルート直下の `Makefile` により、トランスパイル・ランタイム同期・ファームウェアビルドを一括管理。

```bash
# ヘルプ一覧
make help

# ビルド
make haskell         # Logic.hs / MonitorLogic.hs の C コード生成
make sensor          # 子機 (M5StickC Plus2) ビルド
make receiver        # 親機 (M5Core2) ビルド
make all             # 全体ビルド

# 実機書き込み
make upload-sensor   # 子機ファームウェア書き込み
make upload-receiver # 親機ファームウェア書き込み
make upload-fs       # 親機 LittleFS (layout.txt) 書き込み

# 仕様書生成 / クリーン
make doc             # ドキュメント再生成 (build_docs.sh 実行)
make clean           # 中間成果物・キャッシュ破棄
```