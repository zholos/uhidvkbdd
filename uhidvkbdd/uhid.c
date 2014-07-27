#include "uhid.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <usbhid.h>
#include <dev/usb/usbhid.h>

struct item {
    struct item* next;
    hid_item_t h;
    union {
        int last;
        int* state;
    };
};

struct _uhid_priv {
    bool init;
    size_t size;
    unsigned char* buf;
    int* state_tmp;
    struct item* items;
};

static void*
checkp(void* p) {
    if (!p)
        errx(EX_OSERR, "out of memory");
    return p;
}

bool
uhid_open(struct uhid_device* d)
{
    const char* path = d->dev;
    char* path_ = NULL;
    if (!strchr(path, '/')) {
        asprintf(&path_, "/dev/%s", path);
        path = checkp(path_);
    }

    d->fd = open(path, O_RDONLY);
    if (d->fd == -1)
        err(errno == ENOENT ? EX_NOINPUT : EX_OSERR, "open(%s) failed", path);

    struct _uhid_priv* p = d->_priv = checkp(calloc(1, sizeof *p));
    p->init = true;

    report_desc_t rd = hid_get_report_desc(d->fd);
    if (!rd)
        errx(EX_OSERR, "hid_get_report_desc(%s) failed", d->dev);
    // collection and endcollection seem to be returned regardless of kindset
    int kindset = 1 << hid_input | 1 << hid_collection | 1 << hid_endcollection;
    hid_data_t s = hid_start_parse(rd, kindset, -1);
    if (!s)
        errx(EX_OSERR, "hid_start_parse(%s) failed", d->dev);

    int app_level = 0;
    uint32_t app_usage = 0;
    int state_tmp_size = 0;
    struct item** tail = &p->items;
    for (;;) {
        hid_item_t h;
        int r = hid_get_item(s, &h);
        if (r < 0)
            errx(EX_OSERR, "hid_get_item(%s) failed", d->dev);
        if (!r)
            break; // done parsing

        switch (h.kind) {
        case hid_collection:
            if (h.collection == 1) { // Application
                app_level = h.collevel;
                app_usage = h.usage;
            }
            break;
        case hid_endcollection:
            if (h.collevel + 1 == app_level)
                app_usage = 0;
            break;
        case hid_input:
            if (h.flags & HIO_CONST)
                break;
            bool want;
            int *state = NULL;
            if (h.flags & HIO_VARIABLE) {
                assert(h.report_count == 1); // lib breaks these up
                want = !d->want || d->want(app_usage, h.usage, h.usage);
            } else {
                // assume usage_minimum is always set for array
                want = !d->want || d->want(app_usage,
                                           h.usage_minimum, h.usage_maximum);
                if (want) {
                    state = checkp(calloc(h.report_count, sizeof *state));
                    if (state_tmp_size < h.report_count)
                        state_tmp_size = h.report_count;
                }
            }
            if (want) {
                struct item* item = checkp(calloc(1, sizeof *item));
                item->h = h;
                if (state)
                    item->state = state;
                *tail = item;
                tail = &item->next;
            }
        }
    }
    hid_end_parse(s);
    if (state_tmp_size)
        d->_priv->state_tmp = checkp(calloc(state_tmp_size,
                                            sizeof *d->_priv->state_tmp));

    p->size = hid_report_size(rd, hid_input, -1);
    p->buf = checkp(malloc(p->size));

    hid_dispose_report_desc(rd);

    return !!p->items;
}

static void
get_data(struct uhid_device* d, int flags)
{
    struct _uhid_priv* p = d->_priv;
    for (struct item* item = p->items; item; item = item->next) {
        if (item->h.report_ID != NO_REPORT_ID && item->h.report_ID != p->buf[0])
            continue;
        if (item->h.flags & HIO_VARIABLE) {
            int value = hid_get_data(p->buf, &item->h);
            if (item->last != value || (flags & uhid_init)) {
                item->last = value;
                if (d->report)
                    d->report(item->h.usage, value, flags |
                              uhid_null * (value < item->h.logical_minimum ||
                                           value > item->h.logical_maximum));
            }
        } else {
            hid_item_t h = item->h;
            int n = h.report_count;
            int* old = item->state;
            int* new = p->state_tmp;
            for (int i = 0; i < n; i++) {
                new[i] = hid_get_data(p->buf, &h);
                h.pos += h.report_size;
            }
            for (int i = 0; i < n; i++)
                if (old[i]) { // zero seems to indicate null
                    for (int j = 0; j < n; j++)
                        if (old[i] == new[j])
                            goto already_pressed;
                    if (d->report)
                        d->report(h.usage_minimum + old[i], 0, flags);
                already_pressed:;
                }
            for (int i = 0; i < n; i++)
                if (new[i]) {
                    for (int j = 0; j < n; j++)
                        if (new[i] == old[j])
                            goto still_pressed;
                    if (d->report)
                        d->report(h.usage_minimum + new[i], 1, flags);
                still_pressed:;
                }
            memcpy(old, new, sizeof *new * n);
        }
    }
}

bool
uhid_read(struct uhid_device* d)
{
    struct _uhid_priv* p = d->_priv;
    if (p->init) {
        p->init = 0;
        for (struct item* item = p->items; item; item = item->next) {
            for (struct item* seen = p->items; seen != item; seen = seen->next)
                if (seen->h.report_ID == item->h.report_ID)
                    goto next_id;
            if (item->h.report_ID != NO_REPORT_ID)
                p->buf[0] = item->h.report_ID;
            if (hid_get_report(d->fd, hid_input, p->buf, p->size) >= 0)
                get_data(d, uhid_init);
            // failed poll is not fatal
        next_id:;
        }
    } else {
        int r = read(d->fd, p->buf, p->size);
        if (r == -1)
            err(EX_IOERR, "read(%s) failed", d->dev);
        if (r == 0)
            return false;
        get_data(d, 0);
    }
    return true;
}
