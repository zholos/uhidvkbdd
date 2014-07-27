#include "uhid.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

// The are many layers of translation from scancodes to X11 keysyms.
// This table took some experimentation.

// See:
//   http://www.usb.org/developers/devclass_docs/Hut1_11.pdf
//   http://svnweb.freebsd.org/base/head/share/misc/usb_hid_usages?view=co
//   http://www.computer-engineering.org/ps2keyboard/scancodes1.html
//   http://cgit.freedesktop.org/xorg/driver/xf86-input-keyboard/tree/src/at_scancode.c
//   http://svnweb.freebsd.org/ports/head/x11-drivers/xf86-input-keyboard/files/patch-at_scancode.c?view=co
//   http://cgit.freedesktop.org/xkeyboard-config/tree/keycodes/xfree86
//   http://cgit.freedesktop.org/xkeyboard-config/tree/symbols/inet
//   "USB HID to PS/2 Scan Code Translation Table - Microsoft"

// The default keyboard model is pc105 which adds inet(pc105) symbols.
// These keys should work without additional configuration (except those
// remapped by the FreeBSD patch to x11-drivers/xf86-input-keyboard).

struct button {
    uint32_t usage;
    unsigned int make[4], break_[4];
} buttons[] = {
    { 0xc0040, { 0x79 },       { 0xf9 }       }, // XF86AudioMedia
    { 0xc00b6, { 0xe0, 0x10 }, { 0xe0, 0x90 } }, // XF86AudioPrev
    { 0xc00b5, { 0xe0, 0x19 }, { 0xe0, 0x99 } }, // XF86AudioNext
    { 0xc00e2, { 0xe0, 0x20 }, { 0xe0, 0xa0 } }, // XF86AudioMute
    { 0xc00cd, { 0xe0, 0x22 }, { 0xe0, 0xa2 } }, // XF86AudioPlay/XF86AudioPause
    { 0xc00cc, { 0xe0, 0x24 }, { 0xe0, 0xa4 } }, // XF86AudioStop/XF86Eject
    { 0xc00ea, { 0xe0, 0x2e }, { 0xe0, 0xae } }, // XF86AudioLowerVolume
    { 0xc00e9, { 0xe0, 0x30 }, { 0xe0, 0xb0 } }, // XF86AudioRaiseVolume
    { 0xc00b8, { 0x5a },       { 0xda }       }, // XF86Eject
    { 0xc0192, { 0xe0, 0x21 }, { 0xe0, 0xa1 } }, // XF86Calculator
    { 0xc0196, { 0xe0, 0x32 }, { 0xe0, 0xb2 } }, // XF86WWW
    { 0xc0221, { 0xe0, 0x65 }, { 0xe0, 0xe5 } }, // XF86Search
    { 0xc022a, { 0xe0, 0x66 }, { 0xe0, 0xe6 } }, // XF86Favorites
    { 0xc0227, { 0xe0, 0x67 }, { 0xe0, 0xe7 } }, // XF86Reload
    { 0xc0226, { 0xe0, 0x68 }, { 0xe0, 0xe8 } }, // XF86Stop
    { 0xc0225, { 0xe0, 0x69 }, { 0xe0, 0xe9 } }, // XF86Forward
    { 0xc0224, { 0xe0, 0x6a }, { 0xe0, 0xea } }, // XF86Back
    { 0xc0194, { 0xe0, 0x6b }, { 0xe0, 0xeb } }, // XF86MyComputer
    { 0xc018a, { 0xe0, 0x6c }, { 0xe0, 0xec } }, // XF86Mail
    { 0xc0183, { 0xe0, 0x6d }, { 0xe0, 0xed } }, // XF86AudioMedia
};

int dflag;

int vkbd_fd;
char* vkbd_dev;

void
vkbd_init()
{
    for (int i = 0;; i++) {
        asprintf(&vkbd_dev, "/dev/vkbdctl%d", i);
        if (!vkbd_dev)
            errx(EX_OSERR, "out of memory");
        vkbd_fd = open(vkbd_dev, O_WRONLY);
        if (vkbd_fd == -1) {
            if (errno == EBUSY) { // one control per virtual keyboard
                free(vkbd_dev);
                continue;
            } else if (dflag)
                printf("open(%s) failed, won't emit scancodes\n", vkbd_dev);
            else if (errno == ENOENT)
                errx(EX_UNAVAILABLE, "kldload vkbd");
            else
                err(EX_OSERR, "open(%s) failed", vkbd_dev);
        }
        break;
    }
}

bool
want(uint32_t application, uint32_t usage_min, uint32_t usage_max)
{
    if (dflag) {
        printf("report: application %x, usage %x", application, usage_min);
        if (usage_min != usage_max)
            printf("-%x", usage_max);
    }
    // try to only attach to relevant devices
    bool want = false;
    if (usage_min == usage_max || application == 0xc0001)
        for (int i = 0; i < sizeof buttons / sizeof *buttons; i++)
            if (usage_min <= buttons[i].usage && buttons[i].usage <= usage_max)
                want = true;
    if (dflag)
        printf(" - %s\n", want ? "mapped" : "ignored");
    return want;
}

void
report(uint32_t usage, int value, int flags)
{
    if (flags & uhid_init)
        return;
    if (dflag)
        printf("input: usage %x = %d\n", usage, value);
    for (int i = 0; i < sizeof buttons / sizeof *buttons; i++)
        if (usage == buttons[i].usage) {
            unsigned int* codes = value ? buttons[i].make : buttons[i].break_;
            int n = 0;
            if (dflag)
                printf("scancodes:");
            for (; n < 4 && codes[n]; n++)
                if (dflag)
                    printf(" %02x", codes[n]);
            if (dflag)
                printf("\n");
            if (vkbd_fd != -1) {
                int w = write(vkbd_fd, codes, sizeof *codes * n);
                if (w == -1)
                    err(EX_IOERR, "write(%s)", vkbd_dev);
                if (w == 0)
                    errx(EX_IOERR, "write(%s)", vkbd_dev);
            }
        }
}

void
usage()
{
    errx(EX_USAGE, "usage: uhidvkbdd [-d] uhid0");
}

int
main(int argc, char* argv[]) {
    int ch;
    while ((ch = getopt(argc, argv, "d")) != -1)
        switch (ch) {
        case 'd':
            dflag = 1;
            break;
        default:
            usage();
        }
    argc -= optind;
    argv += optind;
    if (argc != 1)
        usage();

    struct uhid_device d = {
        .dev = argv[0],
        .want = want,
        .report = report
    };

    if (!uhid_open(&d))
        errx(EX_OK, "%s: no mapped consumer inputs found", d.dev);

    vkbd_init();

    if (!dflag)
        daemon(0, 0);

    while (uhid_read(&d));
}
