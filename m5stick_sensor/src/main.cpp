#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <cmath>
#include "espnow_config.h"

// --- 拡張パケット構造体 (12バイト) ---
typedef struct {
    uint8_t status;
    float delta;
    int8_t battery;
} PacketData;

// --- 感度しきい値プリセット定義 ---
struct SensitivityPreset {
    const char* label;
    float threshold;
};

static const SensitivityPreset PRESETS[] = {
    { "Lv.1 High ", 0.10f }, // 精密機器向け
    { "Lv.2 Mid  ", 0.25f }, // 標準
    { "Lv.3 Low  ", 0.50f }, // 振動大
    { "Lv.4 Min  ", 1.00f }  // 強い衝撃のみ
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

static int currentLevel = 1; // デフォルト: Lv.2 (0.25G)
static Preferences prefs;
static portMUX_TYPE sensorMux = portMUX_INITIALIZER_UNLOCKED;

// --- IMU計測値 ---
static float latestDelta = 0.0f;
static float prevNorm = 1.0f;

// =============================================================================
// Haskell FFI 境界
// =============================================================================

static volatile int latestJudgedStatus = 0;

extern "C" {
    int mhs_main(int argc, char **argv);

    int c_get_millis(void) {
        return (int)millis();
    }

    float c_get_accel_delta(void) {
        portENTER_CRITICAL(&sensorMux);
        float d = latestDelta;
        portEXIT_CRITICAL(&sensorMux);
        return d;
    }

    float c_get_threshold(void) {
        portENTER_CRITICAL(&sensorMux);
        float th = PRESETS[currentLevel].threshold;
        portEXIT_CRITICAL(&sensorMux);
        return th;
    }

    void c_send_packet(int status, float delta) {
        portENTER_CRITICAL(&sensorMux);
        latestJudgedStatus = status; // Haskell の判定ステータスを保存
        portEXIT_CRITICAL(&sensorMux);

        PacketData packet;
        packet.status  = (uint8_t)status;
        packet.delta   = delta;
        packet.battery = (int8_t)M5.Power.getBatteryLevel();

        esp_now_send(CORE2_MAC, (uint8_t *)&packet, sizeof(PacketData));
    }

    void c_delay_ms(int ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
// =============================================================================
// タスク & 初期化
// =============================================================================

void haskellTask(void *pvParameters) {
    Serial.println("[MicroHs Sensor Task] Starting Logic loop...");
    char arg0[] = "mhs";
    char *dummy_argv[] = { arg0, NULL };

    mhs_main(1, dummy_argv);

    Serial.println("[MicroHs Sensor Task] Finished unexpectedly.");
    vTaskDelete(NULL);
}

// 子機 LCD の情報描画 (フリッカーフリー)
void updateSensorDisplay(int status, float delta) {
    static int lastLevel = -1;
    static int lastStatus = -1;
    static unsigned long lastSec = 0;

    unsigned long nowSec = millis() / 1000;
    bool levelChanged = (currentLevel != lastLevel);
    bool statusChanged = (status != lastStatus);

    if (levelChanged || statusChanged) {
        lastLevel = currentLevel;
        lastStatus = status;

        uint16_t bg = (status == 1) ? TFT_DARKGREEN : TFT_MAROON;
        M5.Display.fillScreen(bg);

        M5.Display.setTextColor(TFT_WHITE, bg);
        M5.Display.setTextDatum(top_center);
        M5.Display.setTextSize(3);
        M5.Display.drawString((status == 1) ? "RUN" : "STOP", 120, 10);

        // 側面ボタン (BtnB) の案内
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_YELLOW, bg);
        M5.Display.drawString("[Press Side Btn to Change TH]", 120, 115);
    }

    // 動的テキスト（振動値、感度設定、バッテリ）
    if (nowSec != lastSec || levelChanged) {
        lastSec = nowSec;
        uint16_t bg = (status == 1) ? TFT_DARKGREEN : TFT_MAROON;

        M5.Display.setTextColor(TFT_WHITE, bg);
        M5.Display.setTextSize(2);
        M5.Display.setTextDatum(top_left);

        char buf[32];
        snprintf(buf, sizeof(buf), "dG: %.2f G   ", delta);
        M5.Display.drawString(buf, 20, 50);

        snprintf(buf, sizeof(buf), "TH: %.2fG (%s)", PRESETS[currentLevel].threshold, PRESETS[currentLevel].label);
        M5.Display.drawString(buf, 20, 75);

        snprintf(buf, sizeof(buf), "Bat: %3d%%    ", (int)M5.Power.getBatteryLevel());
        M5.Display.drawString(buf, 20, 95);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SYSTEM] Booting M5StickC Plus2 Sensor with NVS Threshold...");

    M5.Display.setRotation(1); // 横向き (240x135)
    M5.Display.fillScreen(TFT_BLACK);

    // NVS から前回の感度レベルを復帰
    prefs.begin("sensor_cfg", false);
    currentLevel = prefs.getInt("th_level", 1);
    if (currentLevel < 0 || currentLevel >= PRESET_COUNT) currentLevel = 1;
    Serial.printf("[NVS] Loaded Sensitivity: %s (%.2f G)\n",
                  PRESETS[currentLevel].label, PRESETS[currentLevel].threshold);

    M5.Imu.begin();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW Init Failed");
        return;
    }

    esp_now_set_pmk((uint8_t *)ESPNOW_PMK);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CORE2_MAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = true;
    memcpy(peerInfo.lmk, ESPNOW_LMK, 16);
    esp_now_add_peer(&peerInfo);

    xTaskCreatePinnedToCore(
        haskellTask,
        "SensorTask",
        32768,
        NULL,
        1,
        NULL,
        1
    );
}

void loop() {
    M5.update();

    // 1. IMU 差分ノルム計算
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
        float currentNorm = std::sqrt(ax * ax + ay * ay + az * az);
        float delta = std::abs(currentNorm - prevNorm);
        prevNorm = currentNorm;

        portENTER_CRITICAL(&sensorMux);
        latestDelta = delta;
        portEXIT_CRITICAL(&sensorMux);
    }

    // 2. 側面ボタン (BtnB) でしきい値切替
    if (M5.BtnB.wasPressed()) {
        portENTER_CRITICAL(&sensorMux);
        currentLevel = (currentLevel + 1) % PRESET_COUNT;
        portEXIT_CRITICAL(&sensorMux);

        prefs.putInt("th_level", currentLevel);
    }

    // 3. 子機画面更新 (Haskell が判定したホールド付きステータスを描画)
    float currentDelta;
    int currentStatus;
    portENTER_CRITICAL(&sensorMux);
    currentDelta  = latestDelta;
    currentStatus = latestJudgedStatus;
    portEXIT_CRITICAL(&sensorMux);

    updateSensorDisplay(currentStatus, currentDelta);

    delay(20);
}