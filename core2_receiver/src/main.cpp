#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <LittleFS.h>
#include <SD.h>
#include <vector>
#include "espnow_config.h"

// --- 拡張パケット構造体 ---
typedef struct {
    uint8_t status;
    float delta;
    int8_t battery;
} PacketData;

static PacketData latestPacket;
static volatile bool newPacketReceived = false;
static volatile bool btnBPressed = false;
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

// --- 保守情報用統計 ---
static uint32_t totalPackets = 0;
static bool showInfoOverlay = false;
static char myMacStr[18];
static char peerMacStr[18];

// =============================================================================
// 軽量テンプレートエンジン
// =============================================================================

enum Align { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

struct TextElem {
    int x, y;
    uint8_t size;
    Align align;
    uint16_t color;
    char text[32];
};

struct LineElem {
    int x1, y1, x2, y2;
    uint16_t color;
};

struct SlotElem {
    uint8_t slotId;
    int x, y;
    uint8_t size;
    Align align;
    uint16_t color;
    char fmt[48];
};

struct LayoutState {
    uint16_t bg = TFT_BLACK;
    std::vector<TextElem> texts;
    std::vector<LineElem> lines;
    std::vector<SlotElem> slots;
};

static LayoutState layouts[3];

static uint16_t parseColor(const String& s) {
    if (s == "red")       return TFT_RED;
    if (s == "darkgreen") return TFT_DARKGREEN;
    if (s == "orange")    return TFT_ORANGE;
    if (s == "white")     return TFT_WHITE;
    if (s == "yellow")    return TFT_YELLOW;
    if (s == "blue")      return TFT_BLUE;
    return TFT_BLACK;
}

static Align parseAlign(const String& s) {
    if (s == "center") return ALIGN_CENTER;
    if (s == "right")  return ALIGN_RIGHT;
    return ALIGN_LEFT;
}

void loadLayoutFile(Stream& f) {
    for (int i = 0; i < 3; i++) {
        layouts[i].texts.clear();
        layouts[i].lines.clear();
        layouts[i].slots.clear();
    }

    int currentIdx = -1;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        if (line == "[STOPPED]")      { currentIdx = 0; continue; }
        if (line == "[RUNNING]")      { currentIdx = 1; continue; }
        if (line == "[DISCONNECTED]") { currentIdx = 2; continue; }
        if (currentIdx < 0) continue;

        LayoutState& ls = layouts[currentIdx];

        if (line.startsWith("bg:")) {
            ls.bg = parseColor(line.substring(3));
        } else if (line.startsWith("text:")) {
            TextElem e;
            char alignBuf[16], colBuf[16], textBuf[32];
            int sz;
            if (sscanf(line.c_str(), "text: (%d, %d), %d, %15[^,], %15[^,], \"%31[^\"]\"",
                       &e.x, &e.y, &sz, alignBuf, colBuf, textBuf) == 6) {
                e.size = sz;
                e.align = parseAlign(String(alignBuf));
                e.color = parseColor(String(colBuf));
                strncpy(e.text, textBuf, sizeof(e.text));
                ls.texts.push_back(e);
            }
        } else if (line.startsWith("line:")) {
            LineElem e;
            char colBuf[16];
            if (sscanf(line.c_str(), "line: (%d, %d), (%d, %d), %d, %15s",
                       &e.x1, &e.y1, &e.x2, &e.y2, colBuf) == 5) {
                e.color = parseColor(String(colBuf));
                ls.lines.push_back(e);
            }
        } else if (line.startsWith("slot:")) {
            SlotElem e;
            char alignBuf[16], colBuf[16], fmtBuf[48];
            int id, sz;
            if (sscanf(line.c_str(), "slot: %d, (%d, %d), %d, %15[^,], %15[^,], \"%47[^\"]\"",
                       &id, &e.x, &e.y, &sz, alignBuf, colBuf, fmtBuf) == 7) {
                e.slotId = id;
                e.size = sz;
                e.align = parseAlign(String(alignBuf));
                e.color = parseColor(String(colBuf));
                strncpy(e.fmt, fmtBuf, sizeof(e.fmt));
                ls.slots.push_back(e);
            }
        }
    }
}

void initLayout() {
    bool loaded = false;
    if (SD.begin(4)) {
        if (SD.exists("/layout.txt")) {
            File f = SD.open("/layout.txt", "r");
            if (f) {
                loadLayoutFile(f);
                f.close();
                loaded = true;
                Serial.println("[UI] Loaded layout.txt from SD card.");
            }
        }
    }
    if (!loaded && LittleFS.begin(true)) {
        if (LittleFS.exists("/layout.txt")) {
            File f = LittleFS.open("/layout.txt", "r");
            if (f) {
                loadLayoutFile(f);
                f.close();
                loaded = true;
                Serial.println("[UI] Loaded layout.txt from LittleFS.");
            }
        }
    }

    if (!loaded) {
        Serial.println("[UI WARN] layout.txt not found. Using built-in defaults.");
        for (int i = 0; i < 3; i++) {
            layouts[i].bg = (i == 1) ? TFT_DARKGREEN : (i == 0) ? TFT_RED : TFT_ORANGE;
            const char* title = (i == 1) ? "RUNNING" : (i == 0) ? "STOPPED" : "DISCONNECTED";
            layouts[i].texts.push_back({160, 35, (uint8_t)(i == 2 ? 3 : 4), ALIGN_CENTER, TFT_WHITE, ""});
            strncpy(layouts[i].texts.back().text, title, 31);
            
            // 枠線ではなく水平区切り線に変更
            layouts[i].lines.push_back({40, 175, 280, 175, TFT_WHITE});
            
            // スロット (MAC常時表示 Slot 4 は廃止)
            layouts[i].slots.push_back({1, 160, 190, 2, ALIGN_CENTER, TFT_WHITE, "Buzzer: %s"});
            layouts[i].slots.push_back({2, 20, 95,  2, ALIGN_LEFT,   TFT_WHITE, "Vibe: %.2f G   "});
            layouts[i].slots.push_back({3, 20, 125, 2, ALIGN_LEFT,   TFT_WHITE, "Stick: %3d%% | Core2: %3d%%  "});
        }
    }
}

// 保守オーバーレイウィンドウ描画 (フリッカーフリー設計)
static void drawInfoOverlay(bool firstDraw) {
    static uint32_t lastSec = 0xFFFFFFFF;
    static uint32_t lastPkts = 0xFFFFFFFF;

    uint32_t sec = millis() / 1000;

    // 1. 初回オープン時のみ、背景矩形と静的ラベルを描画
    if (firstDraw) {
        M5.Display.startWrite();
        M5.Display.fillRoundRect(20, 30, 280, 180, 10, TFT_NAVY);
        M5.Display.drawRoundRect(20, 30, 280, 180, 10, TFT_WHITE);

        M5.Display.setTextColor(TFT_YELLOW, TFT_NAVY);
        M5.Display.setTextDatum(top_center);
        M5.Display.setTextSize(2);
        M5.Display.drawString("--- SYSTEM INFO ---", 160, 40);

        M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
        M5.Display.setTextDatum(top_left);
        M5.Display.setTextSize(1);

        M5.Display.drawString("Host MAC  :", 35, 75);
        M5.Display.drawString(myMacStr, 115, 75);

        M5.Display.drawString("Sensor MAC:", 35, 95);
        M5.Display.drawString(peerMacStr, 115, 95);

        M5.Display.drawString("Uptime    :", 35, 115);
        M5.Display.drawString("Rx Packets:", 35, 135);

        M5.Display.setTextColor(TFT_GREEN, TFT_NAVY);
        M5.Display.setTextDatum(top_center);
        M5.Display.drawString("[Press BtnA to Close]", 160, 175);
        M5.Display.endWrite();

        lastSec = 0xFFFFFFFF;
        lastPkts = 0xFFFFFFFF;
    }

    // 2. 動的数値（秒数・受信パケット数）のみ変化時に部分上書き
    if (sec != lastSec || totalPackets != lastPkts) {
        lastSec = sec;
        lastPkts = totalPackets;
        uint32_t m = sec / 60;
        uint32_t s = sec % 60;

        M5.Display.startWrite();
        M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
        M5.Display.setTextDatum(top_left);
        M5.Display.setTextSize(1);

        char upBuf[32];
        snprintf(upBuf, sizeof(upBuf), "%lum %02lus    ", (unsigned long)m, (unsigned long)s);
        M5.Display.drawString(upBuf, 115, 115);

        char pktBuf[32];
        snprintf(pktBuf, sizeof(pktBuf), "%lu pkts    ", (unsigned long)totalPackets);
        M5.Display.drawString(pktBuf, 115, 135);
        M5.Display.endWrite();
    }
}

// =============================================================================
// Haskell FFI 境界
// =============================================================================

extern "C" {
    int mhs_main(int argc, char **argv);

    int c_get_millis(void) { return (int)millis(); }

    int c_poll_has_packet(void) {
        portENTER_CRITICAL(&dataMux);
        bool has = newPacketReceived;
        newPacketReceived = false;
        portEXIT_CRITICAL(&dataMux);
        return has ? 1 : 0;
    }

    int c_get_packet_status(void) {
        portENTER_CRITICAL(&dataMux);
        int s = latestPacket.status;
        portEXIT_CRITICAL(&dataMux);
        return s;
    }

    float c_get_packet_delta(void) {
        portENTER_CRITICAL(&dataMux);
        float d = latestPacket.delta;
        portEXIT_CRITICAL(&dataMux);
        return d;
    }

    int c_get_packet_battery(void) {
        portENTER_CRITICAL(&dataMux);
        int b = latestPacket.battery;
        portEXIT_CRITICAL(&dataMux);
        return b;
    }

    int c_get_core2_battery(void) {
        return (int)M5.Power.getBatteryLevel();
    }

    int c_poll_btn_b_pressed(void) {
        portENTER_CRITICAL(&dataMux);
        bool pressed = btnBPressed;
        btnBPressed = false;
        portEXIT_CRITICAL(&dataMux);
        return pressed ? 1 : 0;
    }

    void c_render_template(int status, int muteMode, float delta, int stickBat, int core2Bat) {
        static int lastStatus = -1;
        static int lastMute = -1;
        static bool lastOverlay = false;

        bool overlayToggled = (showInfoOverlay != lastOverlay);
        if (overlayToggled) {
            lastOverlay = showInfoOverlay;
            lastStatus = -1; // オーバーレイを閉じた時に監視画面を再描画
        }

        if (showInfoOverlay) {
            drawInfoOverlay(overlayToggled);
            return;
        }
        
        if (status < 0 || status > 2) status = 2;
        const LayoutState& ls = layouts[status];

        if (status != lastStatus || muteMode != lastMute) {
            lastStatus = status;
            lastMute = muteMode;
            M5.Display.fillScreen(ls.bg);

            for (const auto& l : ls.lines) {
                M5.Display.drawLine(l.x1, l.y1, l.x2, l.y2, l.color);
            }
            for (const auto& t : ls.texts) {
                M5.Display.setTextColor(t.color, ls.bg);
                M5.Display.setTextSize(t.size);
                M5.Display.setTextDatum(t.align == ALIGN_CENTER ? top_center : (t.align == ALIGN_RIGHT ? top_right : top_left));
                M5.Display.drawString(t.text, t.x, t.y);
            }
        }

        char buf[64];
        const char* muteLabels[] = { "[Normal]", "[Mute (1x)]", "[Persistent Mute]" };

        for (const auto& slot : ls.slots) {
            buf[0] = '\0';
            if (slot.slotId == 1) {
                const char* mStr = (muteMode >= 0 && muteMode <= 2) ? muteLabels[muteMode] : "";
                snprintf(buf, sizeof(buf), slot.fmt, mStr);
            } else if (slot.slotId == 2) {
                snprintf(buf, sizeof(buf), slot.fmt, delta);
            } else if (slot.slotId == 3) {
                if (stickBat < 0) {
                    snprintf(buf, sizeof(buf), "Stick: CHG | Core2: %3d%%  ", core2Bat);
                } else {
                    snprintf(buf, sizeof(buf), slot.fmt, stickBat, core2Bat);
                }
            } else if (slot.slotId == 4) {
                // MAC アドレス常時表示用 (自機・子機)
                snprintf(buf, sizeof(buf), slot.fmt, myMacStr, peerMacStr);
            }

            M5.Display.setTextColor(slot.color, ls.bg);
            M5.Display.setTextSize(slot.size);
            M5.Display.setTextDatum(slot.align == ALIGN_CENTER ? top_center : (slot.align == ALIGN_RIGHT ? top_right : top_left));
            M5.Display.drawString(buf, slot.x, slot.y);
        }
    }

    void c_set_buzzer(int buzzerCmd) {
        static unsigned long lastTone = 0;
        static bool toneToggle = false;

        if (buzzerCmd == 1) {
            if (millis() - lastTone > 250) {
                lastTone = millis();
                toneToggle = !toneToggle;
                if (toneToggle) M5.Speaker.tone(1500, 200);
            }
        } else if (buzzerCmd == 2) {
            M5.Speaker.tone(2400, 150);
        } else if (buzzerCmd == 3) {
            M5.Speaker.tone(800, 200);
        } else {
            if (M5.Speaker.isPlaying()) M5.Speaker.stop();
        }
    }

    void c_delay_ms(int ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

// -----------------------------------------------------------------------------
// コールバック・タスク・初期化
// -----------------------------------------------------------------------------

void onDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
    if (data_len == sizeof(PacketData)) {
        portENTER_CRITICAL_ISR(&dataMux);
        memcpy(&latestPacket, data, sizeof(PacketData));
        newPacketReceived = true;
        totalPackets++;
        portEXIT_CRITICAL_ISR(&dataMux);
    }
}

void haskellTask(void *pvParameters) {
    Serial.println("[MicroHs Monitor Task] Starting MonitorLogic loop...");
    char arg0[] = "mhs";
    char *dummy_argv[] = { arg0, NULL };

    mhs_main(1, dummy_argv);

    Serial.println("[MicroHs Monitor Task] Finished unexpectedly.");
    vTaskDelete(NULL);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SYSTEM] Booting M5Core2 Monitor with Maintenance Overlay...");

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // esp_read_mac を用いて eFuse から確実に実 MAC を取得
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(myMacStr, sizeof(myMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(peerMacStr, sizeof(peerMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             STICKC_MAC[0], STICKC_MAC[1], STICKC_MAC[2], STICKC_MAC[3], STICKC_MAC[4], STICKC_MAC[5]);
             
    initLayout();

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW Init Failed");
        return;
    }

    esp_now_set_pmk((uint8_t *)ESPNOW_PMK);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, STICKC_MAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = true;
    memcpy(peerInfo.lmk, ESPNOW_LMK, 16);
    esp_now_add_peer(&peerInfo);

    xTaskCreatePinnedToCore(
        haskellTask,
        "MonitorTask",
        32768,
        NULL,
        1,
        NULL,
        1
    );
}

void loop() {
    M5.update();

    // BtnA: 保守情報オーバーレイの表示切り替え
    if (M5.BtnA.wasPressed()) {
        showInfoOverlay = !showInfoOverlay;
    }

    // BtnB: ミュートモード切り替え (Haskellへ通知)
    if (M5.BtnB.wasPressed()) {
        portENTER_CRITICAL(&dataMux);
        btnBPressed = true;
        portEXIT_CRITICAL(&dataMux);
    }

    delay(10);
}