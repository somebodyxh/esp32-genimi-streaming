#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "driver/uart.h"
//宏定义和全局变量
#define DEVICE_NAME "ESP32_Gemini"
static uint32_t spp_handle = 0;

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
//  初始化
    if (event == ESP_SPP_INIT_EVT) {
        esp_bt_gap_set_device_name(DEVICE_NAME);
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVER");//启动服务
// 连接成功逻辑
    } else if (event == ESP_SPP_SRV_OPEN_EVT) {
        spp_handle = param->srv_open.handle;
// 这里这个 ESP_SPP_DATA_IND_EVT是接受数据的逻辑 就是用户向esp32发送 
    } else if (event == ESP_SPP_DATA_IND_EVT) {
        uint8_t *data = param->data_ind.data;
        uint16_t len = param->data_ind.len;

        // 检查末尾是否包含校验码 
        // 如果数据包完整包含校验码，则进行校验，否则直接通过
        if (len > 3 && data[len-3] == '*') {
            unsigned int received_sum;
            sscanf((char*)&data[len-2], "%02X", &received_sum);
            
            unsigned int calc_sum = 0;
            for (int i = 0; i < len - 3; i++) {
                calc_sum = (calc_sum + data[i]) % 256;
            }
            
            if (calc_sum == received_sum) {
                uart_write_bytes(UART_NUM_0, "[CMD]", 5);
                uart_write_bytes(UART_NUM_0, (char*)data, len - 3); // 去掉校验码再发给串口
                uart_write_bytes(UART_NUM_0, "\r\n", 2);
            } else {
                const char *err = "[ERROR] Checksum Mismatch!\r\n";
                uart_write_bytes(UART_NUM_0, err, strlen(err));
            }
        } else {
            // 普通数据或不含校验的指令，直接传
            uart_write_bytes(UART_NUM_0, "[CMD]", 5);
            uart_write_bytes(UART_NUM_0, (char*)data, len);
            uart_write_bytes(UART_NUM_0, "\r\n", 2);
        }
    }
}
//以下是串口接收逻辑 要确保py脚本运行
void usb_to_bt_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(2048); 
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, data, 2048, pdMS_TO_TICKS(50));
        if (len > 0 && spp_handle != 0) {
            esp_spp_write(spp_handle, len, data);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}
//主函数
void app_main(void) {
    nvs_flash_init();//初始化存储
    uart_config_t uart_cfg = { 
        .baud_rate = 115200, //在这里调波特率
        .data_bits = UART_DATA_8_BITS, 
        .parity = UART_PARITY_DISABLE, 
        .stop_bits = UART_STOP_BITS_1 
    };
    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0);//初始化串口 注意：第一个条件是初始化串口号 一般不需要动 第二个条件是接收缓冲区大小 第三个条件是发送缓冲区大小 大小默认都是4kb 后面不需要管

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    esp_bluedroid_init();
    esp_bluedroid_enable();
    
    // 功率拉满 (P9)
    esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);//你阔一自己调功率渥

    esp_spp_register_callback(esp_spp_cb);//注册了下回调
    esp_spp_cfg_t spp_cfg = { .mode = ESP_SPP_MODE_CB, .enable_l2cap_ertm = true };
    esp_spp_enhanced_init(&spp_cfg);

    xTaskCreate(usb_to_bt_task, "usb_to_bt", 4096, NULL, 10, NULL);
}