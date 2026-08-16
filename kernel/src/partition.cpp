#include "kernel.h"

// === Partition table management ===
// ponytail: in-memory partition table, no flash write. Add flash-backed persistence when
// the SPI flash driver lands (system_plan S4).

#define PART_MAX    16
#define PART_NAME   24

struct partition_t {
    char name[PART_NAME];
    uint32_t offset;
    uint32_t size;
    int system; // 1 = system partition (protected)
};

static partition_t g_parts[PART_MAX];
static int g_part_cnt = 0;

// Default layout: boot | kernel | tools | user
static void part_init() {
    g_part_cnt = 0;
    // boot is always at flash offset 0
    k_strncpy(g_parts[0].name, "boot", PART_NAME - 1);
    g_parts[0].offset = 0x000000;
    g_parts[0].size    = 0x010000;  // 64KB
    g_parts[0].system  = 1;
    g_part_cnt++;

    k_strncpy(g_parts[1].name, "kernel", PART_NAME - 1);
    g_parts[1].offset = 0x010000;
    g_parts[1].size    = 0x100000;  // 1MB
    g_parts[1].system  = 1;
    g_part_cnt++;

    k_strncpy(g_parts[2].name, "tools", PART_NAME - 1);
    g_parts[2].offset = 0x110000;
    g_parts[2].size    = 0x080000;  // 512KB
    g_parts[2].system  = 0;
    g_part_cnt++;

    k_strncpy(g_parts[3].name, "user", PART_NAME - 1);
    g_parts[3].offset = 0x190000;
    g_parts[3].size    = 0x200000;  // 2MB
    g_parts[3].system  = 0;
    g_part_cnt++;
}

static int part_find(const char* name) {
    for (int i = 0; i < g_part_cnt; i++)
        if (k_strcmp(g_parts[i].name, name) == 0) return i;
    return -1;
}

// --- Command handler ---

void cmd_part(const char* arg) {
    // Initialize on first call (lazy)
    if (g_part_cnt == 0) part_init();

    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("part: flash partition management\r\n"
                  "Usage:\r\n"
                  "  part -list              list all partitions\r\n"
                  "  part -add <name>        add a new partition at end\r\n"
                  "  part -add <name> -f <a> -b <b>  add between partitions\r\n"
                  "  part -del <name>        delete a partition\r\n"
                  "  part -help              show this help\r\n"
                  "System partitions (boot, kernel) require root and double-confirm to delete.\r\n");
        return;
    }

    char sub[16], name[PART_NAME];
    const char* p = tok_next(arg, sub, sizeof(sub));

    if (k_strcmp(sub, "-list") == 0) {
        if (g_part_cnt == 0) { uart_puts("No partitions defined.\r\n"); return; }
        uart_puts("Partitions:\r\n");
        uart_puts("  NAME       OFFSET    SIZE      FLAGS\r\n");
        for (int i = 0; i < g_part_cnt; i++) {
            kprintf("  %-8s  %08x  %08x  %s\r\n",
                    g_parts[i].name, g_parts[i].offset, g_parts[i].size,
                    g_parts[i].system ? "[system]" : "[user]");
        }
        kprintf("Total: %d partitions\r\n", g_part_cnt);

    } else if (k_strcmp(sub, "-add") == 0) {
        // Parse name
        p = tok_next(p, name, sizeof(name));
        if (!name[0]) { uart_puts("part -add: missing partition name\r\n"); return; }

        if (part_find(name) >= 0) {
            kprintf("part: partition '%s' already exists\r\n", name);
            return;
        }

        if (g_part_cnt >= PART_MAX) {
            uart_puts("part: partition table full\r\n");
            return;
        }

        // Parse optional -f <name> and -b <name> flags (order-independent)
        char flag[4], before[PART_NAME], after[PART_NAME];
        int has_f = 0, has_b = 0;
        before[0] = '\0'; after[0] = '\0';
        // Parse up to 2 flags from remaining args
        for (int pass = 0; pass < 2; pass++) {
            // Skip leading whitespace/delimiter
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            if (*p != '-') break;  // not a flag
            p = tok_next(p, flag, sizeof(flag));
            if (k_strcmp(flag, "-f") == 0 && !has_f) {
                p = tok_next(p, before, sizeof(before));
                if (before[0]) has_f = 1;
            } else if (k_strcmp(flag, "-b") == 0 && !has_b) {
                p = tok_next(p, after, sizeof(after));
                if (after[0]) has_b = 1;
            }
        }

        // Validate reference partitions exist
        if (has_f && part_find(before) < 0) {
            kprintf("part: reference partition '%s' not found\r\n", before);
            return;
        }
        if (has_b && part_find(after) < 0) {
            kprintf("part: reference partition '%s' not found\r\n", after);
            return;
        }

        // Build the new partition in a local first, decide the insertion index,
        // THEN shift and place it. Writing offset/size into a live array slot
        // before the shift (as an earlier version did) corrupted the displaced
        // neighbour's offset/size.
        partition_t new_part;
        k_memset(&new_part, 0, sizeof(new_part));
        int ins;

        if (has_f && has_b) {
            // Insert between before and after
            int fi = part_find(before);
            int bi = part_find(after);
            if (fi < 0 || bi < 0) { uart_puts("part: invalid reference\r\n"); return; }
            int lo = (fi < bi) ? fi : bi;
            int hi = (fi < bi) ? bi : fi;
            uint32_t end_lo = g_parts[lo].offset + g_parts[lo].size;
            uint32_t start_hi = g_parts[hi].offset;
            uint32_t gap = (end_lo < start_hi) ? (start_hi - end_lo) : 0;
            new_part.offset = end_lo;
            new_part.size = (gap > 0) ? gap : 0x010000; // fit the gap, else default 64KB
            ins = lo + 1;
        } else if (has_f) {
            int fi = part_find(before);
            new_part.offset = g_parts[fi].offset + g_parts[fi].size;
            new_part.size = 0x010000;
            ins = fi + 1;
        } else if (has_b) {
            int bi = part_find(after);
            // Insert directly before bi: abuts the previous partition.
            new_part.offset = (bi > 0) ? (g_parts[bi - 1].offset + g_parts[bi - 1].size) : 0;
            new_part.size = 0x010000;
            ins = bi;
        } else {
            // Append at end
            new_part.offset = g_parts[g_part_cnt - 1].offset + g_parts[g_part_cnt - 1].size;
            new_part.size = 0x010000;
            ins = g_part_cnt;
        }

        // Shift [ins .. end] right by one, then drop the new partition in.
        for (int i = g_part_cnt; i > ins; i--)
            g_parts[i] = g_parts[i - 1];
        k_strncpy(new_part.name, name, PART_NAME - 1);
        new_part.name[PART_NAME - 1] = '\0';
        new_part.system = 0;
        g_parts[ins] = new_part;
        g_part_cnt++;
        kprintf("part: added '%s' at 0x%08x (size 0x%x)\r\n",
                name, g_parts[ins].offset, g_parts[ins].size);
    } else if (k_strcmp(sub, "-del") == 0) {
        p = tok_next(p, name, sizeof(name));
        if (!name[0]) { uart_puts("part -del: missing partition name\r\n"); return; }

        int idx = part_find(name);
        if (idx < 0) { kprintf("part: partition '%s' not found\r\n", name); return; }

        // System partition protection
        if (g_parts[idx].system && !user_check_root()) {
            uart_puts("part: Permission denied — system partitions require root\r\n");
            return;
        }

        // ponytail: double-confirm for system partitions
        if (g_parts[idx].system) {
            uart_puts("WARNING: deleting system partition 'boot' or 'kernel' may brick the device!\r\n");
            uart_puts("Type 'Y' to confirm: ");
            char c1 = 0, c2 = 0;
            while (!uart_avail()) task_yield();
            c1 = uart_getc(); uart_putc(c1); uart_puts("\r\n");
            if (c1 != 'Y' && c1 != 'y') { uart_puts("Cancelled.\r\n"); return; }

            uart_puts("Confirm again (Y): ");
            while (!uart_avail()) task_yield();
            c2 = uart_getc(); uart_putc(c2); uart_puts("\r\n");
            if (c2 != 'Y' && c2 != 'y') { uart_puts("Cancelled.\r\n"); return; }
        }

        // Remove by shifting
        for (int i = idx; i < g_part_cnt - 1; i++)
            g_parts[i] = g_parts[i + 1];
        g_part_cnt--;
        kprintf("part: deleted '%s'\r\n", name);

    } else {
        kprintf("part: unknown option '%s'. Use 'part -help'\r\n", sub);
    }
}