// Bench tool: receive files onto the SD card over the USB-Serial/JTAG console.
// Protocol (host -> badge, line oriented):
//   PING                          -> PONG
//   PUT <abs_path> <size_bytes>   -> READY, then base64 lines, then END -> OK <n> | ERR <msg>
// Intermediate directories are created. Used by resources/push_file.py; lets an
// agent load firmware bins onto the badge's card without touching the hardware.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "mbedtls/base64.h"
#include "esp_log.h"

static const char *TAG = "push";

bool sd_mount_public(void); // provided by main.c

static void mkdirs_for(const char *path)
{
    char tmp[280];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0775); // EEXIST is fine
            *p = '/';
        }
    }
}

static void handle_put(char *args)
{
    char path[280];
    long size;
    if (sscanf(args, "%279s %ld", path, &size) != 2) {
        printf("ERR usage: PUT <path> <size>\n");
        return;
    }
    if (!sd_mount_public()) {
        printf("ERR no sd card\n");
        return;
    }
    mkdirs_for(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("ERR open %d\n", errno);
        return;
    }
    printf("READY\n");
    fflush(stdout);

    // Flow control: the usb_serial_jtag driver discards bytes when its RX ring
    // fills, so the host sends 8 lines then waits for our "K" ack.
    static char line[700];
    static unsigned char bin[520];
    long written = 0;
    int nlines = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (strcmp(line, "END") == 0) break;
        if (len == 0) continue;
        size_t out = 0;
        if (mbedtls_base64_decode(bin, sizeof(bin), &out, (unsigned char *)line, len) != 0) {
            ok = false;
            break;
        }
        if (fwrite(bin, 1, out, f) != out) {
            ok = false;
            break;
        }
        written += out;
        if (++nlines % 8 == 0) {
            printf("K\n");
            fflush(stdout);
        }
    }
    fclose(f);
    if (ok && written == size) {
        printf("OK %ld\n", written);
        ESP_LOGI(TAG, "received %s (%ld bytes)", path, written);
    } else {
        remove(path);
        printf("ERR got %ld of %ld\n", written, size);
    }
    fflush(stdout);
}

static void serial_task(void *arg)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 8192;
    cfg.tx_buffer_size = 2048;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();

    static char line[600];
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (strncmp(line, "PING", 4) == 0) {
            printf("PONG\n");
            fflush(stdout);
        } else if (strncmp(line, "PUT ", 4) == 0) {
            handle_put(line + 4);
        } else if (strncmp(line, "LIST", 4) == 0) {
            void menu_list_serial(void);
            menu_list_serial();
        } else if (strncmp(line, "DEL ", 4) == 0) {
            char path[280];
            if (sscanf(line + 4, "%279s", path) == 1 && sd_mount_public() && remove(path) == 0) {
                printf("OK\n");
            } else {
                printf("ERR\n");
            }
            fflush(stdout);
        } else if (strncmp(line, "RUN ", 4) == 0) {
            extern volatile int g_remote_select;
            g_remote_select = atoi(line + 4);
            printf("RUNNING %d\n", (int)g_remote_select);
            fflush(stdout);
        }
    }
}

void serial_push_start(void)
{
    xTaskCreate(serial_task, "serial_push", 6144, NULL, 3, NULL);
}
