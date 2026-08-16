#include "kernel.h"

// ponytail: PKI over RAM VFS — real ELF loading deferred to scheduler phase

void cmd_pki(const char* arg) {
    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("PKI: Package Installer\r\n"
                  "Usage:\r\n"
                  "  PKI -i <package.espapp>  install package\r\n"
                  "  PKI -r <packagename>     remove package\r\n"
                  "  PKI -m <directory>       package directory into .espapp\r\n"
                  "  PKI -help                show this help\r\n");
        return;
    }

    char sub[8], name[64];
    const char* p = arg;
    // parse sub-command
    int si = 0;
    while (*p && *p != ' ' && *p != '\t' && si < 7) sub[si++] = *p++;
    sub[si] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    k_strncpy(name, p, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    if (k_strcmp(sub, "-i") == 0) {
        if (!user_check_root()) {
            uart_puts("PKI: Permission denied (EACCES) — root required\r\n");
            return;
        }
        if (!name[0]) { uart_puts("PKI: missing package filename\r\n"); return; }

        // Read .espapp from VFS
        char buf[512];
        int len = vfs_read(name, buf, sizeof(buf));
        if (len < (int)sizeof(espapp_hdr_t)) {
            kprintf("PKI: cannot read '%s' or file too small\r\n", name);
            return;
        }

        espapp_hdr_t* hdr = (espapp_hdr_t*)buf;
        if (hdr->magic != ESPAPP_MAGIC) {
            uart_puts("PKI: invalid .espapp format (bad magic)\r\n");
            return;
        }

        // Create /app/<name>/ directory
        char appdir[VFS_MAX_PATH];
        k_strcpy(appdir, "/app/");
        k_memcpy(appdir + 5, hdr->name, k_strlen(hdr->name) + 1);

        if (vfs_exists(appdir)) {
            kprintf("PKI: package '%s' already installed\r\n", hdr->name);
            return;
        }

        int r = vfs_mkdir(appdir);
        if (r != E_OK) {
            kprintf("PKI: failed to create %s: %s\r\n", appdir, kerr_str(r));
            return;
        }

        kprintf("PKI: installed '%s' v%u to %s\r\n", hdr->name, hdr->version, appdir);
        kprintf("PKI: note — exec loading deferred (no scheduler yet)\r\n");

    } else if (k_strcmp(sub, "-r") == 0) {
        if (!user_check_root()) {
            uart_puts("PKI: Permission denied (EACCES) — root required\r\n");
            return;
        }
        if (!name[0]) { uart_puts("PKI: missing package name\r\n"); return; }

        char appdir[VFS_MAX_PATH];
        k_strcpy(appdir, "/app/");
        k_memcpy(appdir + 5, name, k_strlen(name) + 1);

        if (!vfs_exists(appdir)) {
            kprintf("PKI: package '%s' not found\r\n", name);
            return;
        }

        int r = vfs_delete(appdir);
        if (r != E_OK) {
            kprintf("PKI: failed to remove %s: %s\r\n", appdir, kerr_str(r));
            return;
        }
        kprintf("PKI: removed '%s'\r\n", name);

    } else if (k_strcmp(sub, "-m") == 0) {
        if (!name[0]) { uart_puts("PKI: missing directory path\r\n"); return; }
        if (!vfs_exists(name)) {
            kprintf("PKI: directory '%s' not found\r\n", name);
            return;
        }

        // Build a minimal .espapp header and write to VFS
        espapp_hdr_t hdr;
        hdr.magic = ESPAPP_MAGIC;
        k_memset(hdr.name, 0, sizeof(hdr.name));
        // Extract dir name for package name
        const char* dname = name;
        for (const char* q = name; *q; q++)
            if (*q == '/') dname = q + 1;
        k_strncpy(hdr.name, dname, 31);
        hdr.version = 1;
        hdr.entry_offset = 0;
        hdr.file_count = 0;
        hdr.data_size = 0;

        char outpath[VFS_MAX_PATH];
        k_strcpy(outpath, hdr.name);
        int nlen = k_strlen(outpath);
        k_strcpy(outpath + nlen, ".espapp");

        vfs_mkfile(outpath);
        vfs_write(outpath, (const char*)&hdr, sizeof(hdr));
        kprintf("PKI: packaged '%s' -> %s (%u bytes)\r\n", name, outpath, (uint32_t)sizeof(hdr));

    } else {
        kprintf("PKI: unknown option '%s'. Use PKI -help\r\n", sub);
    }
}
