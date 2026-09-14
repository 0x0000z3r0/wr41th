#include "msg.h"

#define _GNU_SOURCE

#include <link.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern struct r_debug _r_debug;

int main(void)
{
    int detected = 0;

    info("r_version : %d", _r_debug.r_version);
    info("r_map     : %p", (void *)_r_debug.r_map);
    info("r_brk     : %p", (void *)_r_debug.r_brk);
    info("r_state   : %d", _r_debug.r_state);

    // r_state should normally be RT_CONSISTENT
    if (_r_debug.r_state != RT_CONSISTENT)
    {
        bad("linker state not consistent");
        detected = 1;
    }

    // no breakpoint callback
    if (!_r_debug.r_brk)
    {
        bad("r_brk missing");
        detected = 1;
    }

    // no link_map
    if (!_r_debug.r_map)
    {
        bad("link_map missing");
        detected = 1;
    }

    struct link_map *map = _r_debug.r_map;

    int count = 0;
    int found_ld = 0;

    while (map)
    {
        info(
            "module %-3d base=%p name=%s",
            count,
            (void *)map->l_addr,
            map->l_name ? map->l_name : "<null>");

        // suspicious empty names
        if (!map->l_name)
        {
            bad("NULL l_name");
            detected = 1;
        }

        // runtime linker must usually exist
        if (map->l_name &&
            strstr(map->l_name, "ld-linux"))
        {
            found_ld = 1;
        }

        // common instrumentation libs
        if (map->l_name)
        {
            if (strstr(map->l_name, "frida")     ||
                strstr(map->l_name, "pin")       ||
                strstr(map->l_name, "dynamorio") ||
                strstr(map->l_name, "valgrind"))
            {
                bad("instrumentation library: %s",
                       map->l_name);
                detected = 1;
            }
        }

        // excessive module count
        if (++count > 4096)
        {
            bad("corrupted link_map chain");
            detected = 1;
            break;
        }

        // self-loop detection
        if (map->l_next == map)
        {
            bad("self-referencing link_map");
            detected = 1;
            break;
        }

        map = map->l_next;
    }

    if (!found_ld)
    {
        bad("ld-linux missing from link_map");
        detected = 1;
    }

    // look for duplicate module names
    for (struct link_map *a = _r_debug.r_map;
         a;
         a = a->l_next)
    {
        if (!a->l_name || !*a->l_name)
            continue;

        for (struct link_map *b = a->l_next;
             b;
             b = b->l_next)
        {
            if (!b->l_name)
                continue;

            if (!strcmp(a->l_name, b->l_name))
            {
                bad("duplicate module: %s",
                       a->l_name);
                detected = 1;
            }
        }
    }

    if (detected)
    {
        return fail("suspicious _r_debug state detected");
    }

    return ok();
}