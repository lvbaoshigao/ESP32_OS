#include "kernel.h"

// === Spinlock primitives (cooperative scheduler, no lock contention yet) ===
// ponytail: single global spinlock via atomic swap. Upgrade to per-resource locks
// when preemptive scheduler lands (system_plan S3).
typedef volatile uint32_t spinlock_t;

static inline void spin_lock(spinlock_t* lock) {
    while (1) {
        uint32_t r = 1;
        __asm__ volatile("amoswap.w.aq %0, %1, (%2)" : "=r"(r) : "r"(r), "r"(lock) : "memory");
        if (r == 0) return;
    }
}

static inline void spin_unlock(spinlock_t* lock) {
    __asm__ volatile("amoswap.w.rl x0, x0, (%0)" :: "r"(lock) : "memory");
}

// === Task table ===
static task_t g_tasks[TASK_MAX];
static int g_task_count = 0;
static int g_current = -1;

// Idle task stack
static char g_idle_stack[256];

// Forward declarations
static void idle_task();

// === Task stack setup for first run ===
// Initial stack frame (layout matches ctx_switch save):
//   sp+48: ra (entry point)
//   sp+44: s0
//   ...
//   sp+0:  s11
// On first ctx_switch to this task, ctx_switch restores ra,
// then returns — which jumps to the entry point.

static int task_setup_stack(task_t* t, void (*entry)()) {
    uint32_t* stack_top = (uint32_t*)((uint32_t)t->stack_base + t->stack_size);
    uint32_t* sp = stack_top - 13;  // 13 callee-saved regs

    // Zero the frame
    for (int i = 0; i < 13; i++) sp[i] = 0;

    // Set ra so the first ctx_switch return goes to entry
    sp[12] = (uint32_t)entry;  // ra at sp+48

    t->sp = (uint32_t)sp;
    return 0;
}

// === Init ===
void task_init() {
    g_task_count = 0;
    g_current = -1;

    // Create idle task (index 0)
    task_t* idle = &g_tasks[g_task_count];
    int i;
    for (i = 0; i < (int)sizeof(g_idle_stack); i++)
        g_idle_stack[i] = 0;
    idle->stack_base = g_idle_stack;
    idle->stack_size = sizeof(g_idle_stack);
    idle->state = TASK_READY;
    k_strncpy(idle->name, "idle", TASK_NAME_LEN - 1);
    idle->name[TASK_NAME_LEN - 1] = '\0';
    task_setup_stack(idle, idle_task);
    g_task_count++;
}

// === Create a task ===
int task_create(const char* name, void (*entry)(), uint32_t stack_size) {
    if (g_task_count >= TASK_MAX) return -1;
    if (stack_size < 128) stack_size = TASK_STACK_DEF;

    char* stack = (char*)mm_alloc(stack_size);
    if (!stack) return -1;

    // Zero the stack (helps debugging)
    for (uint32_t i = 0; i < stack_size; i++) stack[i] = 0;

    task_t* t = &g_tasks[g_task_count];
    t->stack_base = stack;
    t->stack_size = stack_size;
    t->state = TASK_READY;
    k_strncpy(t->name, name, TASK_NAME_LEN - 1);
    t->name[TASK_NAME_LEN - 1] = '\0';

    task_setup_stack(t, entry);
    g_task_count++;
    return g_task_count - 1;
}

// === Find next ready task (round-robin) ===
static int next_ready() {
    if (g_task_count <= 0) return -1;
    int start = g_current;
    for (int i = 1; i <= g_task_count; i++) {
        int idx = (start + i) % g_task_count;
        if (g_tasks[idx].state == TASK_READY)
            return idx;
    }
    return -1;
}

// === Yield ===
void task_yield() {
    if (g_current < 0) return;  // guard: no current task (Fix 5)
    if (g_task_count < 2) return;  // nothing to switch to

    int prev = g_current;
    g_tasks[prev].state = TASK_READY;

    int next = next_ready();
    if (next < 0) {
        g_tasks[prev].state = TASK_RUNNING;
        return;
    }

    g_current = next;
    g_tasks[next].state = TASK_RUNNING;

    ctx_switch(&g_tasks[prev].sp, g_tasks[next].sp);
}

// === Exit ===
// Fix 1: don't free stack here — ctx_switch uses it before returning to next task.
// Mark as TASK_ZOMBIE; idle task reclaims the stack.
void task_exit() {
    if (g_current < 0 || g_current >= g_task_count) {
        while (1) asm volatile("wfi");
    }

    g_tasks[g_current].state = TASK_ZOMBIE;

    // Find next ready task
    int next = next_ready();
    if (next < 0) {
        while (1) asm volatile("wfi");  // nothing left
    }

    int prev = g_current;
    g_current = next;
    g_tasks[next].state = TASK_RUNNING;

    ctx_switch(&g_tasks[prev].sp, g_tasks[next].sp);
    // Never returns here
}

// === Message queue IPC ===
int msgq_init(msgq_t* q) {
    q->head = q->tail = q->count = 0;
    q->full = 0;
    return 0;
}

int msgq_send(msgq_t* q, const char* data, int len) {
    if (len >= MSGQ_SLOT_LEN) len = MSGQ_SLOT_LEN - 1;
    if (q->count >= MSGQ_MAX) return -1;  // full

    int i;
    for (i = 0; i < len; i++)
        q->slots[q->head][i] = data[i];
    q->slots[q->head][i] = '\0';
    q->head = (q->head + 1) % MSGQ_MAX;
    q->count++;
    return len;
}

int msgq_recv(msgq_t* q, char* buf, int maxlen) {
    if (q->count <= 0) return -1;  // empty

    int i;
    for (i = 0; i < maxlen - 1 && q->slots[q->tail][i]; i++)
        buf[i] = q->slots[q->tail][i];
    buf[i] = '\0';
    q->tail = (q->tail + 1) % MSGQ_MAX;
    q->count--;
    return i;
}

int msgq_avail(msgq_t* q) { return q->count; }

// === Idle task ===
static void idle_task() {
    while (1) {
        // Reclaim zombie task stacks (safe: idle runs on g_idle_stack)
        for (int i = 0; i < g_task_count; i++) {
            if (g_tasks[i].state == TASK_ZOMBIE) {
                if (i != 0 && g_tasks[i].stack_base && g_tasks[i].stack_base != g_idle_stack) {
                    mm_free(g_tasks[i].stack_base);
                }
                g_tasks[i].stack_base = 0;
                g_tasks[i].state = TASK_EXIT;
            }
        }
        // ponytail: busy-wait instead of wfi — wfi gates UART clock on ESP32-C6,
        // preventing UART RX from receiving bytes [TRM 8.2] (power management).
        // Replace with wfi and UART clock-lock when PLL is configured (160MHz).
        for (volatile int i = 0; i < 10000; i++);
        task_yield();
    }
}

// === Query ===
int task_count() { return g_task_count; }
task_t* task_get(int idx) { return (idx >= 0 && idx < g_task_count) ? &g_tasks[idx] : 0; }
int task_current_id() { return g_current; }

// === Start scheduler: pick first task and run it ===
// Called once from kernel_main to hand control to the scheduler
void task_start() {
    if (g_task_count < 2) return;  // only idle task, nothing to schedule

    // Pick first non-idle, non-exit task
    int next = -1;
    for (int i = 1; i < g_task_count; i++) {
        if (g_tasks[i].state == TASK_READY) {
            next = i;
            break;
        }
    }
    if (next < 0) return;

    // Switch into the first task. Save kernel_main's context to a throwaway slot:
    // it must NOT go into g_tasks[0].sp, which already holds idle_task's prepared
    // frame. Overwriting it makes "idle" resume as kernel_main, which returns into
    // entry.S _kern_halt (wfi) and gates the UART clock — killing console RX.
    // kernel_main never resumes; its stack is abandoned by design.
    uint32_t discard_sp = 0;
    g_current = next;
    g_tasks[next].state = TASK_RUNNING;
    ctx_switch(&discard_sp, g_tasks[next].sp);
}