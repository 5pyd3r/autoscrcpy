#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdint.h>
#include <stdbool.h>

bool clipboard_init(void);
bool clipboard_get_text(char **text, uint32_t *len);
bool clipboard_set_text(const char *text, uint32_t len);
void clipboard_destroy(void);

#endif /* CLIPBOARD_H */
