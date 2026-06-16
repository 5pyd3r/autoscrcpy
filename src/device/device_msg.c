#include "device_msg.h"
#include "../adb/binary.h"
#include "../platform/log.h"
#include <stdlib.h>
#include <string.h>

int device_msg_deserialize(const uint8_t *data, uint32_t len, struct device_msg *msg) {
    if (len < 1) {
        log_error("Device message too short");
        return -1;
    }

    msg->type = data[0];

    switch (msg->type) {
        case DEVICE_MSG_TYPE_CLIPBOARD: {
            if (len < 9) {
                log_error("Clipboard message too short");
                return -1;
            }
            msg->clipboard.sequence = *(uint64_t *)(data + 1);
            msg->clipboard.len = len - 9;
            msg->clipboard.text = malloc(msg->clipboard.len + 1);
            if (!msg->clipboard.text) {
                log_error("Failed to allocate clipboard text");
                return -1;
            }
            memcpy(msg->clipboard.text, data + 9, msg->clipboard.len);
            msg->clipboard.text[msg->clipboard.len] = '\0';
            break;
        }
        case DEVICE_MSG_TYPE_ACK_CLIPBOARD: {
            if (len < 9) {
                log_error("Ack clipboard message too short");
                return -1;
            }
            msg->ack_clipboard.sequence = *(uint64_t *)(data + 1);
            break;
        }
        case DEVICE_MSG_TYPE_UHID_OUTPUT: {
            if (len < 5) {
                log_error("UHID output message too short");
                return -1;
            }
            msg->uhid_output.id = *(uint16_t *)(data + 1);
            msg->uhid_output.len = *(uint16_t *)(data + 3);
            if (len < 5 + msg->uhid_output.len) {
                log_error("UHID output message data too short");
                return -1;
            }
            msg->uhid_output.data = malloc(msg->uhid_output.len);
            if (!msg->uhid_output.data) {
                log_error("Failed to allocate UHID output data");
                return -1;
            }
            memcpy(msg->uhid_output.data, data + 5, msg->uhid_output.len);
            break;
        }
        default:
            log_error("Unknown device message type: %d", msg->type);
            return -1;
    }

    return 0;
}

void device_msg_destroy(struct device_msg *msg) {
    switch (msg->type) {
        case DEVICE_MSG_TYPE_CLIPBOARD:
            free(msg->clipboard.text);
            break;
        case DEVICE_MSG_TYPE_UHID_OUTPUT:
            free(msg->uhid_output.data);
            break;
        default:
            break;
    }
}

int device_msg_serialize_clipboard(const char *text, uint32_t len,
                                   uint64_t sequence, uint8_t *buf, uint32_t buf_size) {
    uint32_t total = 1 + 8 + 4 + len;
    if (total > buf_size) return -1;

    buf[0] = DEVICE_MSG_TYPE_CLIPBOARD;
    write64be(&buf[1], sequence);
    write32be(&buf[9], len);
    memcpy(&buf[13], text, len);
    return (int)total;
}

int device_msg_serialize_ack_clipboard(uint64_t sequence,
                                        uint8_t *buf, uint32_t buf_size) {
    if (buf_size < 9) return -1;

    buf[0] = DEVICE_MSG_TYPE_ACK_CLIPBOARD;
    write64be(&buf[1], sequence);
    return 9;
}
