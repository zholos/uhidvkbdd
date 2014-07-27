#include "uhid.h"

#include <err.h>
#include <stdio.h>
#include <sysexits.h>

bool
want(uint32_t application, uint32_t usage_min, uint32_t usage_max)
{
    printf("report: application %x, usage %x", application, usage_min);
    if (usage_min != usage_max)
        printf("-%x", usage_max);
    printf("\n");
    return true;
}

void
report(uint32_t usage, int value, int flags)
{
    printf("input: usage %x = %d", usage, value);
    if (flags & uhid_init)
        printf(" [init]");
    if (flags & uhid_null)
        printf(" [null]");
    printf("\n");
}


int
main(int argc, char* argv[])
{
    if (argc != 2)
        errx(EX_USAGE, "dump uhid0");

    struct uhid_device d = {
        .dev = argv[1],
        .want = want,
        .report = report
    };

    if (uhid_open(&d))
        while (uhid_read(&d));
}
