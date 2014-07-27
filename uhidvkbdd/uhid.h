// Opens a uhid device, parses its input descriptor and reports using libusbhid,
// and presents the inputs to the user as usage/value pairs, regardless of how
// they are encoded in the reports.

#ifndef UHIDVKBDD_UHID_H
#define UHIDVKBDD_UHID_H

#include <stdbool.h>
#include <stdint.h>

enum uhid_flags {
    uhid_init = 1, // current state of device, not an event
    uhid_null = 2, // value outside of range
};

// User allocates this and sets the public fields.
struct uhid_device {
    const char* dev;
    int fd; // read-only to allow polling
    bool (*want)(uint32_t application, uint32_t usage_min, uint32_t usage_max);
    void (*report)(uint32_t usage, int value, int flags);
    struct _uhid_priv* _priv; // private
};

// Returns true if device was opened and may report any wanted inputs.
bool uhid_open(struct uhid_device*);

// Called in a loop, returns false when device is detached.
bool uhid_read(struct uhid_device*);

#endif
