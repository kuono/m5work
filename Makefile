# ==============================================================================
# M5-MicroHs 遠隔振動監視システム ビルド自動化 Makefile
# ==============================================================================

MHS      := $(shell which mhs 2>/dev/null || echo "tools/MicroHs/bin/mhs")
PIO      := pio
SHELL    := /bin/zsh

TOOLS_DIR    := tools/MicroHs
HS_SRC_DIR   := haskell
SENSOR_DIR   := m5stick_sensor
RECEIVER_DIR := core2_receiver

# 修正済みマイコン用ランタイムの正本
EVAL_SRC     := common/microhs_runtime/eval.c

SENSOR_EVAL  := $(SENSOR_DIR)/src/microhs/eval.c
RECEIVER_EVAL:= $(RECEIVER_DIR)/src/microhs/eval.c

SENSOR_HS    := $(HS_SRC_DIR)/Logic.hs
SENSOR_C     := $(SENSOR_DIR)/src/microhs/Logic.c

RECEIVER_HS  := $(HS_SRC_DIR)/MonitorLogic.hs
RECEIVER_C   := $(RECEIVER_DIR)/src/microhs/MonitorLogic.c

.PHONY: all help haskell sensor receiver upload-sensor upload-receiver upload-fs monitor-sensor monitor-receiver doc clean

help:
	@echo "============================================================"
	@echo " M5-MicroHs ビルド自動化タスク一覧"
	@echo "============================================================"
	@echo " make haskell         : HaskellロジックをCコードへトランスパイル"
	@echo " make sensor          : 子機 (M5StickC Plus2) のビルド"
	@echo " make receiver        : 親機 (M5Core2) のビルド"
	@echo " make all             : 全体ビルド (Haskell -> 両機ファームウェア)"
	@echo " make upload-sensor   : 子機へのファームウェア書き込み"
	@echo " make upload-receiver : 親機へのファームウェア書き込み"
	@echo " make upload-fs       : 親機 LittleFS (layout.txt) 書き込み"
	@echo " make monitor-sensor  : 子機のシリアルモニタ起動"
	@echo " make monitor-receiver: 親機のシリアルモニタ起動"
	@echo " make doc             : 仕様書ドキュメントの再生成"
	@echo " make clean           : 中間成果物とキャッシュの破棄"

# 1. MicroHs 評価器 (eval.c) の同期 (正本 common/ から各機へコピー)
$(SENSOR_EVAL): $(EVAL_SRC)
	@mkdir -p $(dir $@)
	cp $< $@

$(RECEIVER_EVAL): $(EVAL_SRC)
	@mkdir -p $(dir $@)
	cp $< $@

# 2. Haskell -> C コード生成
haskell: $(SENSOR_C) $(RECEIVER_C)

$(SENSOR_C): $(SENSOR_HS) $(SENSOR_EVAL)
	@echo "[MHS] Compiling Sensor Logic ($< -> $@)..."
	@mkdir -p $(dir $@)
	$(MHS) $(SENSOR_HS) -o $(SENSOR_DIR)/src/microhs/Logic.c

$(RECEIVER_C): $(RECEIVER_HS) $(RECEIVER_EVAL)
	@echo "[MHS] Compiling Receiver Logic ($< -> $@)..."
	@mkdir -p $(dir $@)
	$(MHS) $(RECEIVER_HS) -o $(RECEIVER_DIR)/src/microhs/MonitorLogic.c

# 3. PlatformIO ファームウェアビルド
sensor: $(SENSOR_C)
	@echo "[PIO] Building M5StickC Plus2 firmware..."
	cd $(SENSOR_DIR) && $(PIO) run

receiver: $(RECEIVER_C)
	@echo "[PIO] Building M5Core2 Monitor firmware..."
	cd $(RECEIVER_DIR) && $(PIO) run

all: sensor receiver

# 4. 実機書き込み & モニタ
upload-sensor: $(SENSOR_C)
	cd $(SENSOR_DIR) && $(PIO) run --target upload

upload-receiver: $(RECEIVER_C)
	cd $(RECEIVER_DIR) && $(PIO) run --target upload

upload-fs:
	cd $(RECEIVER_DIR) && $(PIO) run --target uploadfs

monitor-sensor:
	cd $(SENSOR_DIR) && $(PIO) device monitor

monitor-receiver:
	cd $(RECEIVER_DIR) && $(PIO) device monitor

# 5. ドキュメント生成 & クリーン
doc:
	./build_docs.sh

clean:
	@echo "[CLEAN] Cleaning build artifacts..."
	rm -rf $(SENSOR_DIR)/.pio $(RECEIVER_DIR)/.pio
	rm -f $(SENSOR_C) $(RECEIVER_C)