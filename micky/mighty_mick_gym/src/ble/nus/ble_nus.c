/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ble_nus.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <bluetooth/services/nus.h>

LOG_MODULE_REGISTER(ble_nus, LOG_LEVEL_INF);

#define SLOW_LOG_INF(...)		LOG_INF(__VA_ARGS__); k_msleep(20);

// sample blocks
#define MAX_PAYLOAD (bt_nus_get_mtu(nus_conn) - 3)   // runtime value

typedef struct burst_send_ctx {
    uint8_t sample_block[IMU_BD_MAX_BLOCK_LEN * 6 * sizeof(int16_t)];
    size_t total_len;
    size_t offset;
    size_t sending;
    // optional: semaphore or atomic for completion
}burst_send_ctx_t;




static struct bt_conn *nus_conn;
static bool nus_send_enabled;

static struct k_sem burst_chunk_sent;

static const struct bt_data nus_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data nus_sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void nus_send_enabled_cb(enum bt_nus_send_status status)
{
	nus_send_enabled = (status == BT_NUS_SEND_STATUS_ENABLED);
}

static void nus_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("NUS connection failed to %s (%u)", addr, err);
		return;
	}

	if (!nus_conn) {
		nus_conn = bt_conn_ref(conn);
	}

	ble_common_set_connected(true);
	LOG_INF("NUS connected %s", addr);
}

static void nus_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("NUS disconnected from %s (reason 0x%02x)", addr, reason);

	if (nus_conn == conn) {
		bt_conn_unref(nus_conn);
		nus_conn = NULL;
	}

	nus_send_enabled = false;

	ble_common_set_connected(false);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, nus_ad, ARRAY_SIZE(nus_ad),
			     nus_sd, ARRAY_SIZE(nus_sd));
	if (err) {
		LOG_ERR("NUS Advertising failed to start (err %d)", err);
	}
}
void ble_nus_sent_notify_complete(struct bt_conn *conn);

static struct bt_nus_cb nus_cb = {
	.send_enabled = nus_send_enabled_cb,
	.sent = ble_nus_sent_notify_complete,
};

static struct bt_conn_cb nus_conn_callbacks = {
	.connected = nus_connected,
	.disconnected = nus_disconnected,
};

static burst_send_ctx_t burst_context;

int ble_nus_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth initialized");

	k_sem_init(&burst_chunk_sent, 0, 1);
	burst_context.total_len = 0;

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("NUS init failed (err %d)", err);
		return err;
	}

	bt_conn_cb_register(&nus_conn_callbacks);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, nus_ad, ARRAY_SIZE(nus_ad),
			     nus_sd, ARRAY_SIZE(nus_sd));
	if (err) {
		LOG_ERR("NUS advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("NUS Advertising successfully started");


	return 0;
}
int ble_nus_send_array(const uint8_t * data, uint16_t len)
{
	// LOG_INF("ASND %u bytes", len);
	return bt_nus_send(nus_conn, data, len);
}
int ble_nus_send(const int16_t *input_data)
{
	char buffer[64];
	int len;
	uint32_t mtu;

	if (input_data == NULL) {
		return -EINVAL;
	}

	if (!nus_conn || !nus_send_enabled) {
		return -ENOTCONN;
	}

	static uint32_t id;

	id++;

	if (IS_ENABLED(CONFIG_BLE_MODE_NUS_BINARY)) {
		len = 6 * sizeof(int16_t);
		memcpy(buffer, input_data, len);
	} else {
		len = snprintf(buffer, sizeof(buffer), "%u %d,%d,%d,%d,%d,%d\r\n", id, input_data[0],
				   input_data[1], input_data[2], input_data[3], input_data[4], input_data[5]);
		if ((len <= 0) || (len >= (int)sizeof(buffer))) {
			return -EINVAL;
		}

		mtu = bt_nus_get_mtu(nus_conn);
		if ((uint32_t)len > mtu) {
			return -EMSGSIZE;
		}
	}

	return bt_nus_send(nus_conn, (const uint8_t *)buffer, (uint16_t)len);
}






void ble_nus_sent_notify_complete(struct bt_conn *conn)
{
    if (burst_context.offset >= burst_context.total_len) {
        // done
        return;
    }
	burst_context.offset += burst_context.sending;
	// everytime we're done, we notify through this sem
	k_sem_give(&burst_chunk_sent);

}

int ble_nus_send_burst(const uint8_t * data, uint16_t len) {

	if (!nus_conn || !nus_send_enabled) {
		LOG_INF("No one to send to");
		return -ENOTCONN;
	}
	// LOG_INF("Want to copy %u bytes", len);

	memcpy((void*)burst_context.sample_block, data, len);
	burst_context.total_len = len;
	burst_context.offset = 0;
	size_t sendnowlen = MIN(len, MAX_PAYLOAD);

	LOG_INF("starting send of %u (chunk len %u)", burst_context.total_len, sendnowlen);
	burst_context.sending = sendnowlen;
	int err = ble_nus_send_array(&burst_context.sample_block[burst_context.offset], sendnowlen);
	if (err != 0) {
		LOG_ERR("Problem sending 1st array");
		return err;
	}
	// burst_chunk_sent is given once the array has been transmitted
	k_sem_take(&burst_chunk_sent, K_FOREVER);
	while(burst_context.offset < burst_context.total_len) {
		sendnowlen = MIN(burst_context.total_len - burst_context.offset, MAX_PAYLOAD);
		burst_context.sending = sendnowlen;
		if (sendnowlen) {
			// send next chunk of data
			err = ble_nus_send_array(&burst_context.sample_block[burst_context.offset], sendnowlen);
			if (err != 0) {
				LOG_ERR("Problem sending residual array");
				return err;
			}
			// wait until the tx is done
			k_sem_take(&burst_chunk_sent, K_FOREVER);
		}

	}
	return 0;


}







