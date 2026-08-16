#ifndef KERNEL_H
#define KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int uint32_t;
typedef int int32_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned int size_t;

// === UART0 registers [TRM 27] ===
#define UART0_BASE          0x60000000u
#define UART_FIFO_REG       0x0000
#define UART_STATUS_REG     0x001C
#define UART_CONF0_REG      0x0020
#define UART_CLK_CONF_REG   0x0088
#define UART_TXFIFO_CNT_SHIFT 16
#define UART_TXFIFO_CNT_MASK  0xFF
#define UART_RXFIFO_CNT_MASK  0xFF
#define UART_TX_FIFO_SIZE   128
#define UART_RX_SCLK_EN     (1 << 24)
#define UART_TX_SCLK_EN     (1 << 25)

// === Timer Group 0 (WDT) registers [TRM 14.2] ===
#define TIMG0_BASE          0x60008000u     // [TRM 14.2.1] TIMG0 base address
#define TIMG_WDTCONFIG0     0x0048          // [TRM 14.2.2.1] WDT config reg 0
#define TIMG_WDTCONFIG1     0x004C          // [TRM 14.2.2.2] WDT config reg 1 (stage 0 timeout)
#define TIMG_WDTCONFIG2     0x0050          // [TRM 14.2.2.3] WDT config reg 2 (stage 1 timeout)
#define TIMG_WDTWPROTECT    0x0064          // [TRM 14.2.2.5] WDT write protection
#define WDT_UNLOCK_KEY      0x50D83AA1      // [TRM 14.2.2.5] unlock value for WDTWPROTECT
#define WDT_CONF_UPDATE     (1u << 21)      // [TRM 14.2.2.1] bit 21: config update
#define WDT_STAGE0_SYSRST   (3u << 29)      // [TRM 14.2.2.1] stage 0 action = system reset
#define WDT_ENABLE          (1u << 31)      // [TRM 14.2.2.1] bit 31: WDT enable

// === System reset via WDT ===
static inline void system_reset() {
    volatile uint32_t* tg0 = (volatile uint32_t*)TIMG0_BASE;
    tg0[TIMG_WDTWPROTECT / 4] = WDT_UNLOCK_KEY;
    tg0[TIMG_WDTCONFIG1 / 4] = 1;          // stage 0 timeout = 1 cycle
    tg0[TIMG_WDTCONFIG2 / 4] = 1;          // stage 1 timeout = 1 cycle
    tg0[TIMG_WDTCONFIG0 / 4] = WDT_ENABLE | WDT_STAGE0_SYSRST | WDT_CONF_UPDATE;
    while (1) asm volatile("wfi");
}

#define REG32(addr) (*(volatile uint32_t*)(addr))

// === Boot parameters (matches boot_api.inc exactly) ===
#define BP_MAGIC_VALUE      0xC6B00101u
#define BP_API_VER_1_0      0x00010000u

struct boot_params_t {
    uint32_t magic;
    uint32_t api_version;
    uint32_t boot_mode;
    uint32_t boot_source;
    uint32_t kern_entry;
    uint32_t kern_load;
    uint32_t kern_size;
    uint32_t hw_crystal;
    uint32_t hw_sram_size;
    uint32_t hw_chip_rev;
    uint32_t retry_count;
    uint32_t flags;
    uint32_t reserved[4];
};

// === Mailbox (matches boot_api.inc exactly) ===
#define MAILBOX_ADDR        0x4087F800u
#define MB_MAGIC_VALUE      0xC6AABB01u

struct mailbox_t {
    uint32_t magic;
    uint32_t status;
    uint32_t request;
    uint32_t retry;
    uint32_t flags;
    uint32_t reserved[11];
};

#define KERN_STATUS_OK      0x00
#define KERN_STATUS_FAIL    0x01
#define KERN_STATUS_PANIC   0x02
#define KERN_STATUS_REBOOT  0x03

#define BOOT_MODE_NORMAL    0x00
#define BOOT_MODE_SAFE      0x01
#define BOOT_MODE_RECOVERY  0x02
#define BOOT_MODE_FACTORY   0x03

#define BOOT_SRC_MAIN_FLASH 0x00
#define BOOT_SRC_BACKUP     0x01
#define BOOT_SRC_EXTERNAL   0x02

// === ESPSYS call numbers ===
enum {
    SYS_PUTC = 0,
    SYS_PUTS,
    SYS_GETC,
    SYS_UPTIME,
    SYS_REBOOT,
    SYS_VERSION,
    SYS_DISPLAY_MODE,
    SYS_DRIVER_IO,
    SYS_MAX
};

// === Display modes ===
enum display_mode_t {
    DISPLAY_SERIAL = 0,
    DISPLAY_NETWORK,
    DISPLAY_VIDEO,
    DISPLAY_MAX
};

// === Driver interface ===
struct driver_t {
    const char* name;
    int (*init)();
    int (*read)(void* buf, int len);
    int (*write)(const void* buf, int len);
    int (*ioctl)(int cmd, void* arg);
    int active;
};

#define MAX_DRIVERS 8

// === Boot log ring buffer ===
#define BOOT_LOG_SIZE 2048

// === String utilities ===
int  k_strlen(const char* s);
int  k_strcmp(const char* a, const char* b);
int  k_strncmp(const char* a, const char* b, int n);
char* k_strcpy(char* dst, const char* src);
char* k_strncpy(char* dst, const char* src, int n);
void* k_memset(void* p, int c, int n);
void* k_memcpy(void* dst, const void* src, int n);

// === Console ===
void uart_putc(char c);
void uart_puts(const char* s);
int  uart_getc();
int  uart_avail();
void kprintf(const char* fmt, ...);
void console_init();
void console_prompt();
int  console_readline(char* buf, int maxlen);
void console_dispatch(const char* line);

// === Shell user/host name ===
#define MAX_NAME_LEN 32
extern char g_username[MAX_NAME_LEN];
extern char g_hostname[MAX_NAME_LEN];

// === ESPSYS ===
void espsys_init();
int  espsys_call(int nr, void* arg);

// === Drivers ===
int  driver_register(driver_t* drv);
driver_t* driver_find(const char* name);
void drivers_init();

// === Display ===
int  display_set_mode(int mode);
int  display_get_mode();

// === Time ===
extern "C" uint32_t get_mcycle();
uint32_t uptime_us();

// CPU runs at 40MHz XTAL (Direct Boot, no PLL)
#define CYCLES_PER_US 40

// === Error codes ===
enum kerr_t {
    E_OK = 0,
    E_NOENT = -1,   // no such file or directory
    E_EXIST = -2,   // already exists
    E_ACCES = -3,   // permission denied
    E_NOMEM = -4,   // out of memory
    E_INVAL = -5,   // invalid argument
    E_NOSPC = -6,   // no space left
    E_IO    = -7,   // I/O error
    E_ISDIR = -8,   // is a directory
    E_NOTDIR = -9,  // not a directory
    E_NOTEMPTY = -10 // directory not empty
};

const char* kerr_str(int err);

// === Memory manager ===
void mm_init();
void* mm_alloc(uint32_t size);  // ponytail: needs spin_lock when preemptive
void  mm_free(void* ptr);       // ponytail: needs spin_lock when preemptive
uint32_t mm_free_bytes();
uint32_t mm_total_bytes();
uint32_t mm_heap_start();
uint32_t mm_heap_end();
void mm_swap_init();
void mm_zram_init();
void mm_swap_info(uint32_t* total, uint32_t* used);

// === VFS ===
#define VFS_MAX_PATH  128
#define VFS_MAX_NAME  32
#define VFS_MAX_FILES 128
#define VFS_MAX_DATA  4096

#define VFS_TYPE_DIR  1
#define VFS_TYPE_FILE 2

struct vfs_node_t {
    char name[VFS_MAX_NAME];
    int type;
    int parent;
    int size;
    char* data;
};

void vfs_init();
int  vfs_mkdir(const char* path);
int  vfs_mkfile(const char* path);
int  vfs_rename(const char* oldpath, const char* newname);
int  vfs_delete(const char* path);
int  vfs_write(const char* path, const char* data, int len);
int  vfs_read(const char* path, char* buf, int maxlen);
int  vfs_list(const char* path, void (*cb)(const char* name, int type, int size));
int  vfs_chdir(const char* path);
const char* vfs_pwd();
int  vfs_exists(const char* path);
// Node iteration (for tree walkers such as `find`)
int  vfs_node_count();
const vfs_node_t* vfs_node_get(int idx);
void vfs_path_of(int idx, char* buf, int maxlen);

// === Environment variables ===
#define ENV_MAX      16
#define ENV_NAME_LEN 32
#define ENV_VAL_LEN  64

struct env_var_t {
    char name[ENV_NAME_LEN];
    char value[ENV_VAL_LEN];
};

void env_init();
const char* env_get(const char* name);
int  env_set(const char* name, const char* value);
int  env_count();
env_var_t* env_entry(int idx);

// === User management ===
#define USER_LEVEL_ROOT 0
#define USER_LEVEL_USER 1

extern int g_user_level;

int  user_add(const char* name);
int  user_del(const char* name);
int  user_set_perm(const char* name, int level);
int  user_switch(const char* name);
int  user_check_root();

// === PKI package manager ===
#define ESPAPP_MAGIC 0x45505041u

struct espapp_hdr_t {
    uint32_t magic;
    char name[32];
    uint32_t version;
    uint32_t entry_offset;
    uint32_t file_count;
    uint32_t data_size;
};

void cmd_pki(const char* arg);

// === Pipe ===
#define PIPE_BUF_SIZE 4096

extern char* g_pipe_buf;
extern int   g_pipe_pos;
extern int   g_pipe_active;

void pipe_putc(char c);

// === Network (SLIP over UART) ===
void net_activate(void);
void net_deactivate(void);
int  net_is_active(void);
void net_poll(void);

// === Software commands (prompt_en.MD) ===
void cmd_ping(const char* arg);
void soft_wifisearch(const char* arg);
void soft_wifiinfo(const char* arg);
void cmd_dhcp(const char* arg);
void cmd_track(const char* arg);
void cmd_curl(const char* arg);
void cmd_desktop(const char* arg);
void cmd_edit(const char* arg);

// === WiFi/BT driver externs (used by espsys.cpp) ===
extern driver_t g_wifi_driver;
extern driver_t g_bt_driver;

// === Cooperative scheduler (system_plan S2) ===
#define TASK_MAX       16
#define TASK_NAME_LEN  24
#define TASK_STACK_DEF 2048

enum task_state_t { TASK_READY = 0, TASK_RUNNING, TASK_BLOCKED, TASK_EXIT, TASK_ZOMBIE };

struct task_t {
    uint32_t sp;                    // saved stack pointer (callee-saved regs)
    int state;                      // task_state_t
    char name[TASK_NAME_LEN];
    char* stack_base;               // base of allocated stack
    uint32_t stack_size;
};

void task_init();                   // init scheduler, create idle task
int  task_create(const char* name, void (*entry)(), uint32_t stack_size);
void task_yield();                  // cooperative: switch to next ready task
void task_exit();                   // terminate current task
void task_start();                  // start scheduler (hand control to tasks)
int  task_count();                  // number of alive tasks
task_t* task_get(int idx);          // get task by index (for ps command)
int  task_current_id();             // return current task index

// Context switch primitive (assembly, called from C)
extern "C" void ctx_switch(uint32_t* save_sp, uint32_t load_sp);
extern "C" void ctx_switch_full(uint32_t* save_sp, uint32_t load_sp);

// === Message queue IPC (system_plan S2) ===
#define MSGQ_MAX      16
#define MSGQ_SLOT_LEN 64

struct msgq_t {
    char slots[MSGQ_MAX][MSGQ_SLOT_LEN];
    int head, tail, count;
    int full;
};

int  msgq_init(msgq_t* q);
int  msgq_send(msgq_t* q, const char* data, int len);
int  msgq_recv(msgq_t* q, char* buf, int maxlen);
int  msgq_avail(msgq_t* q);

// === Globals ===
extern boot_params_t* g_boot_params;
extern char g_boot_log[];
extern int  g_boot_log_pos;

void klog(const char* msg);

// === Shared tokenizer (used by console.cpp and partition.cpp) ===
// Returns pointer to next unconsumed character after extracting one token.
const char* tok_next(const char* s, char* out, int maxlen);

#ifdef __cplusplus
}
#endif

#endif
