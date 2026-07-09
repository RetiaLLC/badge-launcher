// Badge Launcher — pick a firmware from flash or SD, install into ota_0, boot it.
//
// Boot contract (settles SPEC open question #3):
//   - otadata normally points at ota_0, so the bootloader boots the app directly
//     with zero launcher latency.
//   - Holding UP (GPIO4) ~1s through a reset triggers the bootloader's
//     factory-reset path: otadata is cleared (nothing else — the erase list is
//     empty) and the factory partition (this launcher) boots.
//   - The launcher therefore always shows the menu when it runs; picking an app
//     writes otadata via esp_ota_set_boot_partition and reboots.
//
// Shared-bus hygiene: all five CS lines are parked high before anything touches
// SPI; the LCD driver and SD host then manage their own CS.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "lcd.h"

static const char *TAG = "launcher";

#define PIN_SD_CS   10
#define BTN_LEFT    3
#define BTN_UP      4
#define BTN_DOWN    5
#define BTN_RIGHT   6
#define BTN_B       7
#define BTN_A       8

#define SD_MOUNT    "/sd"
#define FW_DIR      SD_MOUNT "/firmware"
#define WAD_DIR     FW_DIR "/data"

#define MAX_ENTRIES 24
#define VISIBLE_ROWS 10
#define COPY_BUF (32 * 1024)

typedef enum { ENTRY_BOOT_OTA0, ENTRY_INSTALL_BIN } entry_kind_t;

typedef struct {
    entry_kind_t kind;
    char label[32];
    char path[280]; // FW_DIR + longest FAT LFN

} entry_t;

static entry_t s_entries[MAX_ENTRIES];
static int s_num_entries;
static bool s_sd_ok;
static sdmmc_card_t *s_card;

// Bench remote control (serial_push.c): pending menu selection, -1 = none.
volatile int g_remote_select = -1;

void menu_list_serial(void)
{
    printf("MENU %d\n", s_num_entries);
    for (int i = 0; i < s_num_entries; i++) {
        printf("  %d: %s\n", i, s_entries[i].label);
    }
    fflush(stdout);
}

// ---------------------------------------------------------------- buttons

static void buttons_init(void)
{
    const int pins[] = {BTN_LEFT, BTN_UP, BTN_DOWN, BTN_RIGHT, BTN_B, BTN_A};
    for (int i = 0; i < 6; i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY); // external 10k too
    }
}

// Returns a bitmask of newly-pressed buttons (active low), with hold-repeat
// for UP/DOWN. Bit index = GPIO number.
static uint32_t buttons_poll(void)
{
    static uint32_t prev;
    uint32_t now = 0, events = 0;
    const int pins[] = {BTN_LEFT, BTN_UP, BTN_DOWN, BTN_RIGHT, BTN_B, BTN_A};
    int64_t t = esp_timer_get_time() / 1000;

    for (int i = 0; i < 6; i++) {
        int p = pins[i];
        if (gpio_get_level(p) == 0) {
            now |= 1u << p;
            if (!(prev & (1u << p))) {
                events |= 1u << p;
            }
        }
    }
    // hold-repeat for UP/DOWN
    static int64_t up_t, down_t;
    if (now & (1u << BTN_UP)) {
        if (events & (1u << BTN_UP)) up_t = t + 400;
        else if (t >= up_t) { events |= 1u << BTN_UP; up_t = t + 120; }
    }
    if (now & (1u << BTN_DOWN)) {
        if (events & (1u << BTN_DOWN)) down_t = t + 400;
        else if (t >= down_t) { events |= 1u << BTN_DOWN; down_t = t + 120; }
    }
    prev = now;
    return events;
}

static uint32_t wait_button(void)
{
    while (1) {
        uint32_t ev = buttons_poll();
        if (ev) return ev;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---------------------------------------------------------------- SD card

bool sd_mount_public(void); // for serial_push.c

static bool sd_mount(void)
{
    if (s_sd_ok) return true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST; // bus already initialized by lcd_init

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false, // never destroy a user's card
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return false;
    }
    s_sd_ok = true;
    return true;
}

bool sd_mount_public(void)
{
    return sd_mount();
}

static void sd_unmount(void)
{
    if (!s_sd_ok) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT, s_card);
    s_sd_ok = false;
}

// ---------------------------------------------------------------- NVS name

// Remembers which SD file was last installed, purely for menu display.
// Never erases nvs on init failure — Meshtastic's settings live there too.

static void remember_installed(const char *name)
{
    nvs_handle_t h;
    if (nvs_open("launcher", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "app_name", name);
    nvs_commit(h);
    nvs_close(h);
}

static bool recall_installed(char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open("launcher", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    bool ok = nvs_get_str(h, "app_name", out, &len) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------------------------------------------------------------- menu model

static void build_menu(void)
{
    s_num_entries = 0;

    const esp_partition_t *ota0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_app_desc_t desc;
    if (ota0 && esp_ota_get_partition_description(ota0, &desc) == ESP_OK) {
        entry_t *e = &s_entries[s_num_entries++];
        e->kind = ENTRY_BOOT_OTA0;
        char name[24];
        if (recall_installed(name, sizeof(name))) {
            snprintf(e->label, sizeof(e->label), "Boot %.24s", name);
        } else {
            snprintf(e->label, sizeof(e->label), "Boot %.14s %.10s", desc.project_name, desc.version);
        }
    }

    if (sd_mount()) {
        DIR *dir = opendir(FW_DIR);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) && s_num_entries < MAX_ENTRIES) {
                size_t len = strlen(de->d_name);
                if (len < 5 || strcasecmp(de->d_name + len - 4, ".bin") != 0) continue;
                entry_t *e = &s_entries[s_num_entries++];
                e->kind = ENTRY_INSTALL_BIN;
                snprintf(e->label, sizeof(e->label), "%.28s", de->d_name);
                snprintf(e->path, sizeof(e->path), FW_DIR "/%s", de->d_name);
            }
            closedir(dir);
        }
    }
}

static void draw_menu(int sel)
{
    lcd_clear(C_BLACK);
    lcd_text(0, 0, "  BADGE  LAUNCHER", C_TITLE, C_BLACK);
    lcd_fill_rect(0, 22, LCD_W, 2, C_TITLE);

    if (s_num_entries == 0) {
        lcd_text(0, 4, s_sd_ok ? "No firmware found" : "No SD card", C_ERR, C_BLACK);
        lcd_text(0, 6, s_sd_ok ? "Put .bin files in" : "Insert card and", C_DIM, C_BLACK);
        lcd_text(0, 7, s_sd_ok ? "SD:/firmware/" : "press B to rescan", C_DIM, C_BLACK);
    }

    int top = sel - (VISIBLE_ROWS - 1) > 0 ? sel - (VISIBLE_ROWS - 1) : 0;
    for (int i = 0; i < VISIBLE_ROWS && top + i < s_num_entries; i++) {
        int idx = top + i;
        bool hot = idx == sel;
        if (hot) lcd_fill_rect(0, (2 + i) * 16, LCD_W, 16, C_HILITE);
        char line[TEXT_COLS + 1];
        snprintf(line, sizeof(line), "%c%s%.17s",
                 hot ? '>' : ' ',
                 s_entries[idx].kind == ENTRY_INSTALL_BIN ? "+" : " ",
                 s_entries[idx].label);
        lcd_text(0, 2 + i, line, hot ? C_BLACK : C_WHITE, hot ? C_HILITE : C_BLACK);
    }

    lcd_text(0, 13, s_sd_ok ? "SD ok" : "No SD", s_sd_ok ? C_OK : C_DIM, C_BLACK);
    lcd_text(0, 14, "^v:move A:go B:scan", C_DIM, C_BLACK);
    lcd_flush();
}

// ---------------------------------------------------------------- screens

static void screen_message(const char *l1, const char *l2, uint16_t color)
{
    lcd_clear(C_BLACK);
    lcd_text(0, 5, l1, color, C_BLACK);
    if (l2) lcd_text(0, 7, l2, C_WHITE, C_BLACK);
    lcd_text(0, 14, "B:back", C_DIM, C_BLACK);
    lcd_flush();
    while (!(wait_button() & (1u << BTN_B))) {}
}

static void draw_progress(const char *title, const char *what, int permille, uint16_t color)
{
    lcd_clear(C_BLACK);
    lcd_text(0, 2, title, C_TITLE, C_BLACK);
    lcd_text(0, 5, what, C_WHITE, C_BLACK);
    int w = (LCD_W - 40) * permille / 1000;
    lcd_fill_rect(20, 130, LCD_W - 40, 24, C_DIM);
    lcd_fill_rect(20, 130, w, 24, color);
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", permille / 10);
    lcd_text(8, 10, pct, C_WHITE, C_BLACK);
    lcd_flush();
}

// ---------------------------------------------------------------- install

static bool install_bin(const entry_t *e)
{
    const esp_partition_t *ota0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) {
        screen_message("No ota_0 partition!", NULL, C_ERR);
        return false;
    }

    struct stat st;
    if (stat(e->path, &st) != 0) {
        screen_message("Can't read file", e->label, C_ERR);
        return false;
    }
    if (st.st_size > ota0->size) {
        screen_message("Image too big", "Max 2.4MB app slot", C_ERR);
        return false;
    }

    FILE *f = fopen(e->path, "rb");
    if (!f) {
        screen_message("Can't open file", e->label, C_ERR);
        return false;
    }

    ESP_LOGI(TAG, "Installing %s (%ld bytes) to ota_0", e->path, (long)st.st_size);
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(ota0, st.st_size, &ota);
    if (err != ESP_OK) {
        fclose(f);
        screen_message("esp_ota_begin failed", esp_err_to_name(err), C_ERR);
        return false;
    }

    char *buf = malloc(COPY_BUF);
    assert(buf);
    long done = 0;
    draw_progress("Installing", e->label, 0, C_OK);
    while (done < st.st_size) {
        size_t n = fread(buf, 1, COPY_BUF, f);
        if (n == 0) break;
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) break;
        done += n;
        if ((done % (COPY_BUF * 8)) == 0 || done == st.st_size) {
            draw_progress("Installing", e->label, (int)(done * 1000 / st.st_size), C_OK);
        }
    }
    free(buf);
    fclose(f);

    if (err != ESP_OK || done != st.st_size) {
        esp_ota_abort(ota);
        screen_message("Write failed", esp_err_to_name(err), C_ERR);
        return false;
    }
    err = esp_ota_end(ota); // validates the image
    if (err != ESP_OK) {
        screen_message("Not a valid app image", esp_err_to_name(err), C_ERR);
        return false;
    }
    ESP_LOGI(TAG, "Install OK");
    remember_installed(e->label);
    return true;
}

// ---------------------------------------------------------------- WAD sync

// Compares file head+tail (4K each) against the partition; copies if different.
// Returns false only on a hard error worth telling the user about.
static bool wad_sync_one(const esp_partition_t *part, const char *path, const char *name)
{
    struct stat st;
    if (stat(path, &st) != 0 || (size_t)st.st_size > part->size) {
        ESP_LOGW(TAG, "%s missing on SD or oversized; leaving partition as-is", path);
        return true;
    }
    size_t fsize = st.st_size;
    size_t probe = fsize < 4096 ? fsize : 4096;

    char *fbuf = malloc(COPY_BUF), *pbuf = malloc(4096);
    assert(fbuf && pbuf);
    FILE *f = fopen(path, "rb");
    if (!f) { free(fbuf); free(pbuf); return true; }

    bool same = true;
    fread(fbuf, 1, probe, f);
    esp_partition_read(part, 0, pbuf, probe);
    if (memcmp(fbuf, pbuf, probe) != 0) same = false;
    if (same && fsize > 8192) {
        fseek(f, fsize - 4096, SEEK_SET);
        fread(fbuf, 1, 4096, f);
        esp_partition_read(part, fsize - 4096, pbuf, 4096);
        if (memcmp(fbuf, pbuf, 4096) != 0) same = false;
    }

    if (same) {
        ESP_LOGI(TAG, "%s already resident, skipping", name);
        fclose(f); free(fbuf); free(pbuf);
        return true;
    }

    ESP_LOGI(TAG, "Copying %s (%u bytes) to flash", name, (unsigned)fsize);
    size_t erase_len = (fsize + 4095) & ~(size_t)4095;
    esp_err_t err = esp_partition_erase_range(part, 0, erase_len);
    fseek(f, 0, SEEK_SET);
    size_t done = 0;
    while (err == ESP_OK && done < fsize) {
        size_t n = fread(fbuf, 1, COPY_BUF, f);
        if (n == 0) break;
        err = esp_partition_write(part, done, fbuf, n);
        done += n;
        if ((done % (COPY_BUF * 8)) == 0 || done == fsize) {
            draw_progress("Copying game data", name, (int)(done * 1000 / fsize), C_TITLE);
        }
    }
    fclose(f); free(fbuf); free(pbuf);

    if (err != ESP_OK || done != fsize) {
        screen_message("WAD copy failed", esp_err_to_name(err), C_ERR);
        return false;
    }
    return true;
}

static bool wads_sync(void)
{
    const esp_partition_t *iwad = esp_partition_find_first(66, 6, NULL);
    const esp_partition_t *pwad = esp_partition_find_first(66, 7, NULL);
    if (iwad && !wad_sync_one(iwad, WAD_DIR "/doom1.wad", "doom1.wad")) return false;
    if (pwad && !wad_sync_one(pwad, WAD_DIR "/prboom-plus.wad", "prboom-plus.wad")) return false;
    return true;
}

// ---------------------------------------------------------------- boot

static void boot_ota0(void)
{
    const esp_partition_t *ota0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_err_t err = esp_ota_set_boot_partition(ota0);
    if (err != ESP_OK) {
        screen_message("Can't set boot app", esp_err_to_name(err), C_ERR);
        return;
    }
    lcd_clear(C_BLACK);
    lcd_text(0, 6, "Booting...", C_OK, C_BLACK);
    lcd_text(0, 8, "Hold UP at reset", C_DIM, C_BLACK);
    lcd_text(0, 9, "for this menu", C_DIM, C_BLACK);
    lcd_flush();
    sd_unmount();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

// ---------------------------------------------------------------- splash

// Cat by Felix Lee (asciiart.website/art/325), picked by Kody.
static const char *SPLASH_CAT[] = {
    "   _.---.._             _.---...__",
    ".-'   /\\   \\          .'  /\\     /",
    "`.   (  )   \\        /   (  )   /",
    "  `.  \\/   .'\\      /`.   \\/  .'",
    "    ``---''   )    (   ``---''",
    "            .';.--.;`.",
    "          .' /_...._\\ `.",
    "        .'   `.a  a.'   `.",
    "       (        \\/        )",
    "        `.___..-'`-..___.'",
    "           \\          /",
    "            `-.____.-'",
};

static void draw_splash(void)
{
    lcd_clear(C_BLACK);
    for (int i = 0; i < sizeof(SPLASH_CAT) / sizeof(SPLASH_CAT[0]); i++) {
        lcd_text_small(2, 3 + i, SPLASH_CAT[i], C_HILITE, C_BLACK);
    }
    lcd_text(3, 11, "BADGE LAUNCHER", C_TITLE, C_BLACK);
    lcd_flush();
}

// ---------------------------------------------------------------- main

void app_main(void)
{
    // Park every CS on the shared bus before anything drives SCK/MOSI.
    const gpio_num_t cs_park[] = {GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_39, GPIO_NUM_37,
                                  GPIO_NUM_14, GPIO_NUM_10};
    for (int i = 0; i < sizeof(cs_park) / sizeof(cs_park[0]); i++) {
        gpio_reset_pin(cs_park[i]);
        gpio_set_direction(cs_park[i], GPIO_MODE_OUTPUT);
        gpio_set_level(cs_park[i], 1);
    }

    buttons_init();
    lcd_init();
    draw_splash();
    int64_t splash_t0 = esp_timer_get_time();

    nvs_flash_init(); // best effort; on failure the menu just lacks the name

    void serial_push_start(void);
    serial_push_start();

    build_menu(); // SD mount happens here, behind the splash

    // Keep the cat on screen for at least a second.
    int64_t elapsed_ms = (esp_timer_get_time() - splash_t0) / 1000;
    if (elapsed_ms < 1000) {
        vTaskDelay(pdMS_TO_TICKS(1000 - elapsed_ms));
    }
    ESP_LOGI(TAG, "menu: %d entries, sd=%d", s_num_entries, s_sd_ok);
    for (int i = 0; i < s_num_entries; i++) {
        ESP_LOGI(TAG, "  [%d] %s%s", i, s_entries[i].kind == ENTRY_INSTALL_BIN ? "SD: " : "",
                 s_entries[i].label);
    }

    int sel = 0;
    draw_menu(sel);

    while (1) {
        uint32_t ev = buttons_poll();
        if (g_remote_select >= 0 && g_remote_select < s_num_entries) {
            sel = g_remote_select;
            g_remote_select = -1;
            ev |= 1u << BTN_A;
        } else if (g_remote_select >= s_num_entries) {
            g_remote_select = -1;
        }
        if (!ev) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ev & (1u << BTN_UP) && sel > 0) sel--;
        if (ev & (1u << BTN_DOWN) && sel < s_num_entries - 1) sel++;
        if (ev & (1u << BTN_B)) {
            sd_unmount();
            build_menu();
            if (sel >= s_num_entries) sel = s_num_entries ? s_num_entries - 1 : 0;
        }
        if (ev & (1u << BTN_A) && s_num_entries > 0) {
            entry_t *e = &s_entries[sel];
            if (e->kind == ENTRY_BOOT_OTA0) {
                boot_ota0();
            } else {
                bool ok = install_bin(e);
                // Doom needs its WADs resident; match by filename.
                if (ok && strcasestr(e->label, "doom")) {
                    ok = wads_sync();
                }
                if (ok) boot_ota0();
                build_menu();
                if (sel >= s_num_entries) sel = 0;
            }
        }
        draw_menu(sel);
    }
}
