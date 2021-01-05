#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <driver/timer.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "BT"

void ble_store_config_init();
static int ble_gap_event(struct ble_gap_event *event, void *arg);

uint16_t gatt_rx_handle;

static void ble_advertise(void)
{
	struct ble_gap_adv_params adv_params;
	int rc;

	/* Begin advertising */
	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	//ble_gap_adv_set_data
	rc = ble_gap_adv_start(BLE_HCI_ADV_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
	if (rc != 0) {
		if(rc == BLE_HS_EALREADY)
			ESP_LOGW(TAG, "advertisement already enabled?");
		else
			ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);

		return;
	}
}

static void ble_on_reset(int reason)
{
}

int gatt_rxtx_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	if(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		return 0;

	} else if(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		struct os_mbuf *om1 = ble_hs_mbuf_from_flat("TEST\n", 5);
		if(om1)
			ble_gattc_notify_custom(conn_handle, gatt_rx_handle, om1);

		return 0;
	}

	return BLE_ATT_ERR_UNLIKELY;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
	//ESP_LOGI(TAG, "Enter gap evt %s", gap_events[event->type]);
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		ble_advertise();
		break;

	case BLE_GAP_EVENT_ADV_COMPLETE:
		ble_advertise();
		break;
	}

	return 0;
}

void ble_host_task(void *param)
{
	nimble_port_run();
	nimble_port_freertos_deinit();
}

const struct ble_gatt_svc_def gatt_svr_svcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID128_DECLARE(0xe9, 0x4f, 0x29, 0xbb, 0x1a, 0x11, 0x47, 0x17, 0x27, 0x37, 0x41, 0x5a, 0x6b, 0x79, 0x8f, 0x99),
		.characteristics = (struct ble_gatt_chr_def[]) { {
			.uuid = BLE_UUID16_DECLARE(0x03),
			.access_cb = gatt_rxtx_callback,
			.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
			.val_handle = &gatt_rx_handle,
		}, {
			.uuid = BLE_UUID16_DECLARE(0x02),
			.access_cb = gatt_rxtx_callback,
			.flags = BLE_GATT_CHR_F_WRITE,
		}, {
			0, /* No more characteristics in this service */
		}, }
	},

	{
		0, /* No more services */
	},
};

void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK( ret );

	esp_event_loop_create_default();
	ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());
	nimble_port_init();

	/* Initialize the NimBLE host configuration */
	ble_hs_cfg.sync_cb = ble_advertise;
	ble_hs_cfg.reset_cb = ble_on_reset;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
	ble_hs_cfg.sm_bonding = 0;
	ble_hs_cfg.sm_sc = 0;
	ble_hs_cfg.sm_our_key_dist = 0;
	ble_hs_cfg.sm_their_key_dist = 0;
	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

	ble_gatts_count_cfg(gatt_svr_svcs);
	ble_gatts_add_svcs(gatt_svr_svcs);

	ble_store_config_init();
	nimble_port_freertos_init(ble_host_task);
}
