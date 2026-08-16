#include "kernel.h"

// ponytail: RAM-only VFS — add flash-backed FatFS when driver phase starts

static vfs_node_t g_nodes[VFS_MAX_FILES];
static int g_node_count;
static int g_cwd;
static char g_pwd_buf[VFS_MAX_PATH];

const char* kerr_str(int err) {
    switch (err) {
        case E_OK:       return "OK";
        case E_NOENT:    return "No such file or directory (ENOENT)";
        case E_EXIST:    return "Already exists (EEXIST)";
        case E_ACCES:    return "Permission denied (EACCES)";
        case E_NOMEM:    return "Out of memory (ENOMEM)";
        case E_INVAL:    return "Invalid argument (EINVAL)";
        case E_NOSPC:    return "No space left (ENOSPC)";
        case E_IO:       return "I/O error (EIO)";
        case E_ISDIR:    return "Is a directory (EISDIR)";
        case E_NOTDIR:   return "Not a directory (ENOTDIR)";
        case E_NOTEMPTY: return "Directory not empty (ENOTEMPTY)";
        default:         return "Unknown error";
    }
}

static int find_child(int parent, const char* name) {
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent == parent && k_strcmp(g_nodes[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int resolve_path(const char* path) {
    if (!path || !*path) return g_cwd;
    int cur = (path[0] == '/') ? 0 : g_cwd;
    char comp[VFS_MAX_NAME];
    while (*path) {
        if (*path == '/') { path++; continue; }
        int len = 0;
        while (*path && *path != '/' && len < VFS_MAX_NAME - 1)
            comp[len++] = *path++;
        comp[len] = '\0';
        if (k_strcmp(comp, ".") == 0) continue;
        if (k_strcmp(comp, "..") == 0) {
            if (cur != 0) cur = g_nodes[cur].parent;
            continue;
        }
        int c = find_child(cur, comp);
        if (c < 0) return -1;
        cur = c;
    }
    return cur;
}

static void build_pwd(int node, char* buf, int maxlen) {
    if (node == 0) { k_strcpy(buf, "/"); return; }
    char tmp[VFS_MAX_PATH];
    int pos = maxlen - 1;
    buf[pos] = '\0';
    int cur = node;
    while (cur != 0) {
        int nlen = k_strlen(g_nodes[cur].name);
        pos -= nlen;
        if (pos < 1) { k_strcpy(buf, "/..."); return; }
        k_memcpy(buf + pos, g_nodes[cur].name, nlen);
        buf[--pos] = '/';
        cur = g_nodes[cur].parent;
    }
    int final_len = maxlen - 1 - pos;
    k_memcpy(tmp, buf + pos, final_len + 1);
    k_memcpy(buf, tmp, final_len + 1);
}

static int split_path(const char* path, int* out_parent, char* out_name) {
    const char* last_slash = 0;
    for (const char* p = path; *p; p++)
        if (*p == '/') last_slash = p;

    int parent;
    if (!last_slash || last_slash == path) {
        parent = (path[0] == '/') ? 0 : g_cwd;
        const char* n = last_slash ? last_slash + 1 : path;
        k_strncpy(out_name, n, VFS_MAX_NAME - 1);
        out_name[VFS_MAX_NAME - 1] = '\0';
    } else {
        char parent_path[VFS_MAX_PATH];
        int plen = last_slash - path;
        if (plen >= VFS_MAX_PATH) return E_INVAL;
        k_memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
        parent = resolve_path(parent_path);
        k_strncpy(out_name, last_slash + 1, VFS_MAX_NAME - 1);
        out_name[VFS_MAX_NAME - 1] = '\0';
    }

    if (parent < 0) { *out_parent = -1; return E_NOENT; }
    if (g_nodes[parent].type != VFS_TYPE_DIR) { *out_parent = -1; return E_NOTDIR; }
    *out_parent = parent;
    return E_OK;
}

static int create_node(const char* path, int type) {
    if (g_node_count >= VFS_MAX_FILES) return E_NOSPC;
    int parent;
    char name[VFS_MAX_NAME];
    int err = split_path(path, &parent, name);
    if (err != E_OK) return err;
    if (!name[0]) return E_INVAL;
    if (find_child(parent, name) >= 0) return E_EXIST;

    int idx = g_node_count++;
    k_strcpy(g_nodes[idx].name, name);
    g_nodes[idx].type = type;
    g_nodes[idx].parent = parent;
    g_nodes[idx].size = 0;
    g_nodes[idx].data = 0;
    return E_OK;
}

void vfs_init() {
    k_memset(g_nodes, 0, sizeof(g_nodes));
    k_strcpy(g_nodes[0].name, "/");
    g_nodes[0].type = VFS_TYPE_DIR;
    g_nodes[0].parent = 0;
    g_node_count = 1;
    g_cwd = 0;

    // Create standard directory structure
    vfs_mkdir("/system");
    vfs_mkdir("/user");
    vfs_mkdir("/user/root");
    vfs_mkdir("/user/root/desktop");
    vfs_mkdir("/user/root/download");
    vfs_mkdir("/app");
    vfs_mkdir("/virtual");
    vfs_mkdir("/drivers");
    vfs_mkdir("/cache");

    // Create passwd file
    vfs_mkfile("/system/passwd");
    vfs_write("/system/passwd", "root:root\n", 10);
}

int vfs_mkdir(const char* path)  { return create_node(path, VFS_TYPE_DIR); }
int vfs_mkfile(const char* path) { return create_node(path, VFS_TYPE_FILE); }

int vfs_rename(const char* oldpath, const char* newname) {
    int node = resolve_path(oldpath);
    if (node <= 0) return E_NOENT;
    if (find_child(g_nodes[node].parent, newname) >= 0) return E_EXIST;
    k_strncpy(g_nodes[node].name, newname, VFS_MAX_NAME - 1);
    g_nodes[node].name[VFS_MAX_NAME - 1] = '\0';
    return E_OK;
}

int vfs_delete(const char* path) {
    int node = resolve_path(path);
    if (node <= 0) return E_NOENT;

    // Check if directory is empty
    if (g_nodes[node].type == VFS_TYPE_DIR) {
        for (int i = 0; i < g_node_count; i++) {
            if (g_nodes[i].parent == node && i != node)
                return E_NOTEMPTY;
        }
    }

    // Free file data
    if (g_nodes[node].data) {
        mm_free(g_nodes[node].data);
        g_nodes[node].data = 0;
    }

    // Shift nodes down (compact)
    for (int i = node; i < g_node_count - 1; i++)
        g_nodes[i] = g_nodes[i + 1];
    g_node_count--;

    // Fix parent references
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent > node)
            g_nodes[i].parent--;
        else if (g_nodes[i].parent == node && i != 0)
            g_nodes[i].parent = 0; // orphaned, reparent to root
    }

    // Fix cwd: the compaction above shifts every node after `node` down by one,
    // so a cwd index past the deleted node now points at the wrong node. If the
    // cwd itself was deleted, fall back to root.
    if (g_cwd == node) g_cwd = 0;
    else if (g_cwd > node) g_cwd--;
    if (g_cwd >= g_node_count) g_cwd = 0;

    return E_OK;
}

int vfs_write(const char* path, const char* data, int len) {
    int node = resolve_path(path);
    if (node < 0) return E_NOENT;
    if (g_nodes[node].type != VFS_TYPE_FILE) return E_ISDIR;

    // Fix 3: allocate first, then free old data — avoids data loss on alloc failure
    char* buf = (char*)mm_alloc(len + 1);
    if (!buf) return E_NOMEM;
    if (g_nodes[node].data) mm_free(g_nodes[node].data);

    k_memcpy(buf, data, len);
    buf[len] = '\0';
    g_nodes[node].data = buf;
    g_nodes[node].size = len;
    return E_OK;
}

int vfs_read(const char* path, char* buf, int maxlen) {
    int node = resolve_path(path);
    if (node < 0) return E_NOENT;
    if (g_nodes[node].type != VFS_TYPE_FILE) return E_ISDIR;

    int copylen = g_nodes[node].size;
    if (copylen > maxlen) copylen = maxlen;
    if (g_nodes[node].data && copylen > 0)
        k_memcpy(buf, g_nodes[node].data, copylen);
    return copylen;
}

int vfs_list(const char* path, void (*cb)(const char* name, int type, int size)) {
    int dir = resolve_path(path);
    if (dir < 0) return E_NOENT;
    if (g_nodes[dir].type != VFS_TYPE_DIR) return E_NOTDIR;
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent == dir && i != dir)
            cb(g_nodes[i].name, g_nodes[i].type, g_nodes[i].size);
    }
    return E_OK;
}

int vfs_chdir(const char* path) {
    int node = resolve_path(path);
    if (node < 0) return E_NOENT;
    if (g_nodes[node].type != VFS_TYPE_DIR) return E_NOTDIR;
    g_cwd = node;
    build_pwd(g_cwd, g_pwd_buf, VFS_MAX_PATH);
    return E_OK;
}

const char* vfs_pwd() {
    build_pwd(g_cwd, g_pwd_buf, VFS_MAX_PATH);
    return g_pwd_buf;
}

int vfs_exists(const char* path) {
    return resolve_path(path) >= 0 ? 1 : 0;
}

// === Node iteration (public accessors for `find` and other tree walkers) ===
int vfs_node_count() { return g_node_count; }

const vfs_node_t* vfs_node_get(int idx) {
    if (idx < 0 || idx >= g_node_count) return 0;
    return &g_nodes[idx];
}

void vfs_path_of(int idx, char* buf, int maxlen) {
    if (idx < 0 || idx >= g_node_count || maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    build_pwd(idx, buf, maxlen);
}
