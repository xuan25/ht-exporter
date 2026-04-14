#include "cli.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "soc/soc_caps.h"
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
#include "driver/uart_vfs.h"
#endif
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "nvs.h"
#include "argtable3/argtable3.h"

#include "sensor.h"
#include "wifi_manager.h"

static const char *TAG = "cli";
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
#define UART_VFS_USE_DRIVER uart_vfs_dev_use_driver
#else
#define UART_VFS_USE_DRIVER esp_vfs_dev_uart_use_driver
#endif

typedef enum {
    CLI_BACKEND_UART0 = 0,
    CLI_BACKEND_USB_SERIAL_JTAG,
} cli_backend_t;

static cli_backend_t s_cli_backend = CLI_BACKEND_UART0;

static int cli_read_char(TickType_t ticks_to_wait)
{
    uint8_t ch = 0;

#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_cli_backend == CLI_BACKEND_USB_SERIAL_JTAG) {
        int n = usb_serial_jtag_read_bytes((char *)&ch, 1, ticks_to_wait);
        return (n == 1) ? (int)ch : -1;
    }
#endif

    int n = uart_read_bytes(UART_NUM_0, &ch, 1, ticks_to_wait);
    return (n == 1) ? (int)ch : -1;
}

static struct {
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_end *end;
} wifi_set_args;

static struct {
    struct arg_str *ip;
    struct arg_str *gw;
    struct arg_str *mask;
    struct arg_end *end;
} ip_set_args;

static int wifi_set_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_set_args.end, argv[0]);
        return 1;
    }

    const char *ssid = wifi_set_args.ssid->sval[0];
    const char *pass = wifi_set_args.pass->sval[0];

    esp_err_t err = wifi_manager_set_credentials(ssid, pass);
    if (err != ESP_OK) {
        printf("ERR: failed to save Wi-Fi credentials (%s)\n", esp_err_to_name(err));
        return 1;
    }

    err = wifi_manager_apply_credentials();
    if (err == ESP_OK) {
        printf("OK: saved and applied\n");
    } else {
        printf("OK: saved (apply on next start)\n");
    }

    return 0;
}

static int wifi_show_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char ssid[33] = {0};
    char pass[65] = {0};

    esp_err_t err = wifi_manager_get_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err != ESP_OK) {
        printf("ERR: no stored credentials\n");
        return 1;
    }

    size_t pass_len = strlen(pass);
    memset(pass, '*', pass_len);
    pass[pass_len] = '\0';

    printf("SSID: %s\n", ssid);
    printf("PASS: %s\n", pass);
    return 0;
}

static int wifi_clear_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t stop_err = wifi_manager_stop();
    if (stop_err != ESP_OK) {
        printf("ERR: failed to stop Wi-Fi (%s)\n", esp_err_to_name(stop_err));
        return 1;
    }

    esp_err_t err = wifi_manager_clear_credentials();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        printf("ERR: failed to clear Wi-Fi credentials (%s)\n", esp_err_to_name(err));
        return 1;
    }

    err = wifi_manager_apply_credentials();
    if (err == ESP_OK) {
        printf("OK: cleared and applied defaults\n");
    } else {
        printf("OK: cleared\n");
    }

    return 0;
}

static int ip_set_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ip_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ip_set_args.end, argv[0]);
        return 1;
    }

    const char *ip = ip_set_args.ip->sval[0];
    const char *gw = ip_set_args.gw->sval[0];
    const char *mask = ip_set_args.mask->sval[0];

    esp_err_t err = wifi_manager_set_static_ip(ip, gw, mask);
    if (err != ESP_OK) {
        printf("ERR: failed to set static IP (%s)\n", esp_err_to_name(err));
        return 1;
    }

    printf("OK: static IP saved\n");
    return 0;
}

static int ip_clear_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t err = wifi_manager_clear_static_ip();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        printf("ERR: failed to clear static IP (%s)\n", esp_err_to_name(err));
        return 1;
    }

    printf("OK: static IP cleared (DHCP)\n");
    return 0;
}

static int status_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char status[32] = {0};
    float humidity = 0.0f;
    float temperature = 0.0f;
    uint32_t last_read_ms = 0;

    bool ok = sensor_get_status(status, sizeof(status), &humidity, &temperature, &last_read_ms);
    if (!ok) {
        printf("SENSOR: %s\n", status);
        return 0;
    }

    printf("SENSOR: %s\n", status);
    printf("HUMIDITY: %.1f%%\n", humidity);
    printf("TEMP: %.1fC\n", temperature);
    if (last_read_ms > 0) {
        printf("LAST_READ_MS: %u\n", (unsigned)last_read_ms);
    }
    return 0;
}

static void register_cli_commands(void)
{
    wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", "Wi-Fi SSID");
    wifi_set_args.pass = arg_str1(NULL, NULL, "<pass>", "Wi-Fi password");
    wifi_set_args.end = arg_end(2);

    ip_set_args.ip = arg_str1(NULL, NULL, "<ip>", "Static IP (e.g. 192.168.1.50)");
    ip_set_args.gw = arg_str1(NULL, NULL, "<gw>", "Gateway (e.g. 192.168.1.1)");
    ip_set_args.mask = arg_str1(NULL, NULL, "<mask>", "Netmask (e.g. 255.255.255.0)");
    ip_set_args.end = arg_end(3);

    const esp_console_cmd_t wifi_set = {
        .command = "wifi_set",
        .help = "Set Wi-Fi SSID and password",
        .hint = NULL,
        .func = &wifi_set_cmd,
        .argtable = &wifi_set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_set));

    const esp_console_cmd_t wifi_show = {
        .command = "wifi_show",
        .help = "Show stored Wi-Fi credentials",
        .hint = NULL,
        .func = &wifi_show_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_show));

    const esp_console_cmd_t wifi_clear = {
        .command = "wifi_clear",
        .help = "Clear stored Wi-Fi credentials",
        .hint = NULL,
        .func = &wifi_clear_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_clear));

    const esp_console_cmd_t ip_set = {
        .command = "ip_set",
        .help = "Set static IP: ip_set <ip> <gw> <mask>",
        .hint = NULL,
        .func = &ip_set_cmd,
        .argtable = &ip_set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ip_set));

    const esp_console_cmd_t ip_clear = {
        .command = "ip_clear",
        .help = "Clear static IP (use DHCP)",
        .hint = NULL,
        .func = &ip_clear_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ip_clear));

    const esp_console_cmd_t status = {
        .command = "status",
        .help = "Show sensor task status and latest reading",
        .hint = NULL,
        .func = &status_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status));
}

static void cli_task(void *pvParameter)
{
    (void)pvParameter;

#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();
    s_cli_backend = CLI_BACKEND_USB_SERIAL_JTAG;
    ESP_LOGI(TAG, "CLI over USB Serial/JTAG");
#else
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    UART_VFS_USE_DRIVER(UART_NUM_0);
    s_cli_backend = CLI_BACKEND_UART0;
    ESP_LOGI(TAG, "CLI over UART0");
#endif

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    ESP_ERROR_CHECK(esp_console_register_help_command());

    register_cli_commands();

    ESP_LOGI(TAG, "Serial CLI ready (type 'help')");

    while (1) {
        char line[256];
        size_t len = 0;

        while (1) {
            int ch = cli_read_char(pdMS_TO_TICKS(200));
            if (ch < 0) {
                continue;
            }

            if (ch == '\r') {
                // ignore
                continue;
            }

            if (ch == '\n') {
                putchar('\n');
                fflush(stdout);
                break;
            }

            if (ch == '\b' || ch == 0x7f) {
                if (len > 0) {
                    len--;
                    printf("\b \b");
                    fflush(stdout);
                }
                continue;
            }

            if (len < sizeof(line) - 1) {
                line[len++] = (char)ch;
                putchar(ch);
                fflush(stdout);
            }
        }

        line[len] = '\0';
        if (len > 0) {
            int ret = 0;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unrecognized command\n");
            } else if (err == ESP_ERR_INVALID_ARG) {
                printf("Invalid arguments\n");
            } else if (err != ESP_OK) {
                printf("Command error: %s\n", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cli_start_task(UBaseType_t uxPriority)
{
    xTaskCreate(&cli_task, "cli_task", 4096, NULL, uxPriority, NULL);
}
