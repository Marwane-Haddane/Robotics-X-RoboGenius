// SPDX-License-Identifier: Apache-2.0
#include "cyd_display.h"

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
// ESP32-2432S028: LCD uses HSPI; SD and touch have different wiring.
constexpr int kMosi = 13, kMiso = 12, kClock = 14, kCs = 15, kDc = 2;
constexpr int kBacklight = 21, kBoot = 0;
constexpr int kWidth = 320, kHeight = 240;
constexpr size_t kPixels = kWidth * 24;
// Match the user's existing CYD password-manager screen configuration.
constexpr uint8_t kDefaultPanel = 1;  // 0: original ILI9341; 1: ST7789 revision

constexpr uint16_t rgb(unsigned r, unsigned g, unsigned b) {
    return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
}
constexpr uint16_t kBackground = rgb(10, 17, 30);
constexpr uint16_t kCard = rgb(23, 35, 53);
constexpr uint16_t kBorder = rgb(47, 64, 85);
constexpr uint16_t kWhite = rgb(236, 244, 255);
constexpr uint16_t kMuted = rgb(145, 162, 185);
constexpr uint16_t kCyan = rgb(55, 190, 255);
constexpr uint16_t kGreen = rgb(54, 218, 159);
constexpr uint16_t kAmber = rgb(255, 191, 74);
constexpr uint16_t kFaceColors[] = {
    kCyan, rgb(255, 99, 106), rgb(240, 129, 219), kGreen
};
constexpr const char* kLabels[] = {
    "X", "O", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2",
    "UP", "DOWN", "LEFT", "RIGHT", "L3", "R3", "SHARE", "OPTIONS", "PS"
};

esp_lcd_panel_io_handle_t io = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t transferred = nullptr;
uint16_t* pixels = nullptr;
Preferences preferences;
bool preferencesReady = false, available = false;
uint8_t panelKind = kDefaultPanel;
bool connected = false, inputReady = false;
uint32_t buttons = 0, drawnButtons = 0;
struct GameView {
    MemoryGame::Stage stage = MemoryGame::Stage::Menu;
    MemoryGame::Failure failure = MemoryGame::Failure::None;
    bool hard = false, waitingForRelease = false;
    uint8_t round = 0, length = 0, progress = 0, shown = 0;
    uint16_t seconds = 0;
};
GameView view, drawnView;
bool drawnConnected = false, drawnReady = false, sceneDirty = true;
int areaX = 0, areaY = 0, areaWidth = 0, areaHeight = 0;

// Original compact uppercase bitmap font: each byte is one five-pixel row.
constexpr uint8_t kLetters[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
constexpr uint8_t kDigits[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};
constexpr uint8_t kBlank[7] = {}, kColon[7] = {0,4,4,0,4,4,0};
constexpr uint8_t kSlash[7] = {1,2,2,4,8,8,16}, kDash[7] = {0,0,0,31,0,0,0};
constexpr uint8_t kPlus[7] = {0,4,4,31,4,4,0};

uint16_t wireColor(uint16_t color) {
    // The panel's RGB565 pixel stream is big endian; the ESP32 is little endian.
    return (color << 8) | (color >> 8);
}

void rectangle(int x, int y, int w, int h, uint16_t color) {
    const int left = std::max(x, areaX), top = std::max(y, areaY);
    const int right = std::min(x + w, areaX + areaWidth);
    const int bottom = std::min(y + h, areaY + areaHeight);
    if (left >= right || top >= bottom)
        return;
    for (int row = top; row < bottom; ++row)
        std::fill_n(pixels + (row - areaY) * areaWidth + left - areaX,
                    right - left, wireColor(color));
}

void outline(int x, int y, int w, int h, uint16_t color) {
    rectangle(x, y, w, 1, color);
    rectangle(x, y + h - 1, w, 1, color);
    rectangle(x, y, 1, h, color);
    rectangle(x + w - 1, y, 1, h, color);
}

void line(int x0, int y0, int x1, int y1, uint16_t color) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        rectangle(x0, y0, 2, 2, color);
        if (x0 == x1 && y0 == y1)
            break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int radius, uint16_t color) {
    int x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        rectangle(cx+x,cy+y,2,2,color); rectangle(cx+y,cy+x,2,2,color);
        rectangle(cx-y,cy+x,2,2,color); rectangle(cx-x,cy+y,2,2,color);
        rectangle(cx-x,cy-y,2,2,color); rectangle(cx-y,cy-x,2,2,color);
        rectangle(cx+y,cy-x,2,2,color); rectangle(cx+x,cy-y,2,2,color);
        ++y;
        if (error < 0) error += 2*y+1;
        else { --x; error += 2*(y-x)+1; }
    }
}

const uint8_t* glyph(char c) {
    if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c == ':') return kColon;
    if (c == '/') return kSlash;
    if (c == '-') return kDash;
    if (c == '+') return kPlus;
    return kBlank;
}

void text(const char* value, int x, int y, uint16_t color, int scale = 1) {
    for (; *value; ++value, x += 6 * scale) {
        const auto* rows = glyph(*value);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1 << (4 - col)))
                    rectangle(x + col * scale, y + row * scale, scale, scale, color);
    }
}

void centered(const char* value, int x, int y, int width, uint16_t color, int scale = 1) {
    const int textWidth = std::strlen(value) * 6 * scale - scale;
    text(value, x + (width - textWidth) / 2, y, color, scale);
}

bool transferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transferred, &wake);
    return wake == pdTRUE;
}

template <typename Painter>
void paint(int x, int y, int w, int h, Painter painter) {
    if (!available)
        return;
    const int stripHeight = kPixels / w;
    for (int row = 0; row < h; row += stripHeight) {
        areaX = x; areaY = y + row; areaWidth = w;
        areaHeight = std::min(stripHeight, h - row);
        std::fill_n(pixels, w * areaHeight, wireColor(kBackground));
        painter();
        const esp_err_t error = esp_lcd_panel_draw_bitmap(panel, x, areaY, x + w,
                                                         areaY + areaHeight, pixels);
        if (error != ESP_OK || xSemaphoreTake(transferred, pdMS_TO_TICKS(250)) != pdTRUE) {
            // Never reuse a buffer whose DMA transfer has not completed.
            available = false;
            printf("CYD display transfer failed; Bluetooth remains available.\n");
            return;
        }
    }
}

void drawStatus() {
    using MemoryGame::Stage;
    rectangle(12, 50, 296, 42, kCard);
    rectangle(12, 50, 4, 42, connected ? kGreen : kAmber);
    const char* title = "CHOOSE YOUR GAME";
    if (!connected) title = "WAITING FOR PS4";
    else if (!inputReady) title = "PS4 CONNECTED";
    else if (view.stage == Stage::Ready) title = "GET READY";
    else if (view.stage == Stage::Watch) title = "WATCH AND REMEMBER";
    else if (view.stage == Stage::Repeat) title = "YOUR TURN";
    else title = view.stage == Stage::Menu ? "CHOOSE YOUR GAME" : "ROUND RESULT";
    text(title, 24, 56, connected ? kGreen : kAmber, 2);
    char detail[48];
    if (!connected) std::snprintf(detail, sizeof(detail), "HOLD SHARE + PS TO PAIR");
    else if (!inputReady) std::snprintf(detail, sizeof(detail), "WAITING FOR BUTTON DATA");
    else if (view.stage == Stage::Menu) std::snprintf(detail, sizeof(detail), "PS4 READY - D-PAD TO SELECT");
    else if (view.stage == Stage::Watch)
        std::snprintf(detail, sizeof(detail), "%s  ROUND %u  WATCH %u/%u", view.hard ? "HARD" : "EASY", view.round, view.shown + 1, view.length);
    else if (view.stage == Stage::Repeat && view.hard)
        std::snprintf(detail, sizeof(detail), "ROUND %u  INPUT %u/%u  TIME %u S", view.round, view.progress, view.length, view.seconds);
    else std::snprintf(detail, sizeof(detail), "%s  ROUND %u  INPUT %u/%u", view.hard ? "HARD" : "EASY", view.round, view.progress, view.length);
    text(detail, 24, 80, kMuted);
}

void buttonBounds(unsigned i, int& x, int& y, int& w, int& h) {
    if (i < 4) { x = 12 + i * 76; y = 102; w = 68; h = 68; }
    else { x = 12 + (i - 4) * 76; y = 178; w = 68; h = 28; }
}

void drawButton(unsigned i) {
    int x, y, w, h;
    buttonBounds(i, x, y, w, h);
    const bool pressed = buttons & (1UL << i);
    const uint16_t accent = i < 4 ? kFaceColors[i] : kCyan;
    rectangle(x, y, w, h, pressed ? accent : kCard);
    outline(x, y, w, h, pressed ? kWhite : kBorder);
    const uint16_t ink = pressed ? kBackground : accent;
    if (i < 4) {
        const int cx = x + w / 2, cy = y + 23;
        if (i == 0) { line(cx-11,cy-11,cx+11,cy+11,ink); line(cx+11,cy-11,cx-11,cy+11,ink); }
        if (i == 1) circle(cx,cy,13,ink);
        if (i == 2) { outline(cx-12,cy-12,26,26,ink); outline(cx-11,cy-11,24,24,ink); }
        if (i == 3) { line(cx,cy-13,cx-14,cy+12,ink); line(cx-14,cy+12,cx+14,cy+12,ink); line(cx+14,cy+12,cx,cy-13,ink); }
        centered(kLabels[i], x, y+55, w, pressed ? kBackground : kMuted);
    } else {
        centered(kLabels[i], x, y+(h-14)/2, w, pressed ? kBackground : kWhite, 2);
    }
}

void drawFooter() {
    using MemoryGame::Stage;
    const char* tip = "OPTIONS: BACK TO MENU";
    if (!connected) tip = "UNPLUG CONTROLLER USB - SHARE + PS";
    else if (!inputReady) tip = "WAIT FOR THE CONTROLLER INPUT REPORT";
    else if (view.stage == Stage::Menu) tip = "D-PAD: CHOOSE   X: START";
    else if (view.stage == Stage::Watch || view.stage == Stage::Ready) tip = "WATCH FIRST - OPTIONS: MENU";
    else if (view.stage == Stage::Repeat && view.waitingForRelease) tip = "RELEASE BUTTONS BEFORE THE NEXT PRESS";
    else if (view.stage == Stage::Repeat) tip = "REPEAT THE ORDER - OPTIONS: MENU";
    centered(tip, 0, 225, kWidth, kMuted);
}

void drawScene() {
    using MemoryGame::Stage;
    rectangle(0, 0, kWidth, 44, kCard);
    text("MEMORY COMBO", 12, 9, kWhite, 3);
    text("CYD / PS4", 12, 34, kCyan);
    drawStatus();
    if (view.stage == Stage::Menu) {
        for (unsigned i = 0; i < 2; ++i) {
            const int x = 12 + i * 152;
            const bool selected = view.hard == bool(i);
            const uint16_t accent = i ? kAmber : kGreen;
            rectangle(x, 102, 144, 104, kCard);
            outline(x, 102, 144, 104, selected ? accent : kBorder);
            if (selected) rectangle(x, 102, 144, 4, accent);
            centered(i ? "HARD" : "EASY", x, 117, 144, accent, 3);
            centered(i ? "8 BUTTONS" : "4 FACE BUTTONS", x, 147, 144, kWhite);
            centered(i ? "FAST FLASHES" : "SLOW FLASHES", x, 163, 144, kMuted);
            centered(i ? "5 S PER PRESS" : "NO TIME LIMIT", x, 179, 144, kMuted);
            centered(selected ? "SELECTED" : "", x, 195, 144, accent);
        }
    } else if (view.stage == Stage::Won || view.stage == Stage::Lost || view.stage == Stage::Complete) {
        const bool failed = view.stage == Stage::Lost;
        const char* result = failed ? "TRY AGAIN" : view.stage == Stage::Won ? "ROUND CLEAR" : "YOU WIN";
        centered(result, 0, 111, kWidth, failed ? kAmber : kGreen, 3);
        const char* reason = "ALL 12 BUTTONS REMEMBERED";
        if (view.stage == Stage::Won) reason = "NEXT ROUND ADDS ONE BUTTON";
        if (failed) reason = view.failure == MemoryGame::Failure::Timeout ? "TIME RAN OUT" :
                             view.failure == MemoryGame::Failure::Chord ? "PRESS ONE BUTTON AT A TIME" : "THAT WAS THE WRONG BUTTON";
        centered(reason, 0, 148, kWidth, kWhite);
        centered(view.stage == Stage::Won ? "X: NEXT ROUND" : "X: NEW GAME", 0, 176, kWidth, kCyan, 2);
    } else {
        for (unsigned i = 0; i < (view.hard ? 8u : 4u); ++i) drawButton(i);
        if (!view.hard) centered("X  O  SQUARE  TRIANGLE", 0, 186, kWidth, kMuted);
    }
    drawFooter();
}

esp_err_t configurePanel() {
    available = false;
    gpio_set_level(static_cast<gpio_num_t>(kBacklight), 0);
    if (panel) {
        esp_lcd_panel_del(panel);
        panel = nullptr;
    }
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = -1;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    config.bits_per_pixel = 16;
    esp_err_t result = panelKind == 0 ? esp_lcd_new_panel_ili9341(io, &config, &panel) :
                                       esp_lcd_new_panel_st7789(io, &config, &panel);
    if (result != ESP_OK) return result;
    if ((result = esp_lcd_panel_reset(panel)) != ESP_OK) return result;
    if ((result = esp_lcd_panel_init(panel)) != ESP_OK) return result;
    if ((result = esp_lcd_panel_invert_color(panel, false)) != ESP_OK) return result;
    // Landscape, 320 x 240; touch input is not enabled in this stage.
    if ((result = esp_lcd_panel_swap_xy(panel, true)) != ESP_OK) return result;
    if ((result = esp_lcd_panel_mirror(panel, true, false)) != ESP_OK) return result;
    if ((result = esp_lcd_panel_disp_on_off(panel, true)) != ESP_OK) return result;
    // Panel commands have drained previous transfers, including a late completion
    // after an earlier timeout. Start drawing with an empty completion semaphore.
    while (xSemaphoreTake(transferred, 0) == pdTRUE) {}
    available = true;
    paint(0, 0, kWidth, kHeight, drawScene);
    if (!available) return ESP_FAIL;
    drawnConnected = connected; drawnReady = inputReady; drawnButtons = buttons;
    drawnView = view; sceneDirty = false;
    gpio_set_level(static_cast<gpio_num_t>(kBacklight), 1);
    printf("CYD display ready: %s, 320x240. Hold BOOT for 1 second to change screen driver.\n",
           panelKind == 0 ? "ILI9341" : "ST7789");
    return ESP_OK;
}
}  // namespace

namespace CydDisplay {
bool begin() {
    pinMode(kBoot, INPUT_PULLUP);
    gpio_set_direction(static_cast<gpio_num_t>(kBacklight), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(kBacklight), 0);
    preferencesReady = preferences.begin("cyd-screen", false);
    if (preferencesReady) panelKind = preferences.getUChar("panel", kDefaultPanel) & 1;
    transferred = xSemaphoreCreateBinary();
    pixels = static_cast<uint16_t*>(heap_caps_malloc(kPixels * sizeof(uint16_t), MALLOC_CAP_DMA));
    if (!transferred || !pixels) {
        printf("CYD display: insufficient buffer memory.\n");
        return false;
    }
    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi; bus.miso_io_num = kMiso; bus.sclk_io_num = kClock;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kPixels * sizeof(uint16_t);
    esp_err_t result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) { printf("CYD SPI setup failed: %s\n", esp_err_to_name(result)); return false; }
    esp_lcd_panel_io_spi_config_t config = {};
    config.cs_gpio_num = kCs; config.dc_gpio_num = kDc;
    config.pclk_hz = 20000000;
    config.trans_queue_depth = 1;
    config.on_color_trans_done = transferDone;
    config.lcd_cmd_bits = 8; config.lcd_param_bits = 8;
    result = esp_lcd_new_panel_io_spi(SPI2_HOST, &config, &io);
    if (result != ESP_OK) { printf("CYD display IO failed: %s\n", esp_err_to_name(result)); return false; }
    result = configurePanel();
    if (result != ESP_OK) printf("CYD screen setup failed: %s\n", esp_err_to_name(result));
    return available;
}

void setGame(const MemoryGame::Game& game, bool isConnected, bool hasInput,
             uint32_t pressed, uint32_t now) {
    connected = isConnected; inputReady = hasInput;
    buttons = isConnected ? game.lights(pressed) : 0;
    view = {game.stage, game.failure, game.hard, game.waitingForRelease,
            game.round, game.length, game.progress, game.shown, game.secondsLeft(now)};
}

void update() {
    // A saved driver choice lets both common CYD revisions use the same firmware.
    // Only react to a sustained press after startup, never to a held boot strap.
    static bool bootReleased = false, holding = false, switched = false;
    static uint32_t heldSince = 0;
    if (digitalRead(kBoot) == HIGH) { bootReleased = true; holding = false; switched = false; }
    else if (bootReleased) {
        if (!holding) { holding = true; heldSince = millis(); }
        if (!switched && millis() - heldSince >= 1000 && io && pixels && transferred) {
            switched = true;
            panelKind ^= 1;
            if (configurePanel() == ESP_OK && preferencesReady) preferences.putUChar("panel", panelKind);
        }
    }
    if (!available) return;
    if (sceneDirty || drawnConnected != connected || drawnReady != inputReady ||
        drawnView.stage != view.stage || drawnView.hard != view.hard ||
        drawnView.round != view.round || drawnView.length != view.length) {
        paint(0, 0, kWidth, kHeight, drawScene);
        drawnConnected = connected; drawnReady = inputReady;
        drawnView = view; drawnButtons = buttons; sceneDirty = false;
        return;
    }
    if (drawnView.progress != view.progress || drawnView.shown != view.shown || drawnView.seconds != view.seconds)
        paint(12, 50, 296, 42, drawStatus);
    const uint32_t changed = drawnButtons ^ buttons;
    for (unsigned i = 0; i < (view.hard ? 8u : 4u); ++i) {
        if (!(changed & (1UL << i))) continue;
        int x, y, w, h;
        buttonBounds(i, x, y, w, h);
        paint(x, y, w, h, [i] { drawButton(i); });
    }
    drawnButtons = buttons;
    if (drawnView.waitingForRelease != view.waitingForRelease)
        paint(0, 220, kWidth, 16, drawFooter);
    drawnView = view;
}
}  // namespace CydDisplay
