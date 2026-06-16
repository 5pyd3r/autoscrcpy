#ifndef DEVICE_MSG_H
#define DEVICE_MSG_H

#include <stdint.h>

enum device_msg_type {
    DEVICE_MSG_TYPE_CLIPBOARD,
    DEVICE_MSG_TYPE_ACK_CLIPBOARD,
    DEVICE_MSG_TYPE_UHID_OUTPUT,
};

struct device_msg {
    enum device_msg_type type;
    union {
        struct {
            char *text;
            uint32_t len;
            uint64_t sequence;
        } clipboard;
        struct {
            uint64_t sequence;
        } ack_clipboard;
        struct {
            uint16_t id;
            uint8_t *data;
            uint16_t len;
        } uhid_output;
    };
};

int device_msg_deserialize(const uint8_t *data, uint32_t len, struct device_msg *msg);
void device_msg_destroy(struct device_msg *msg);

int device_msg_serialize_clipboard(const char *text, uint32_t len,
                                   uint64_t sequence, uint8_t *buf, uint32_t buf_size);
int device_msg_serialize_ack_clipboard(uint64_t sequence,
                                        uint8_t *buf, uint32_t buf_size);

#endif /* DEVICE_MSG_H */
