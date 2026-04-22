#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "filesystem_new.h"

#define COLOR_BLACK         0x00
#define COLOR_BLUE          0x01
#define COLOR_GREEN         0x02
#define COLOR_CYAN          0x03
#define COLOR_RED           0x04
#define COLOR_MAGENTA       0x05
#define COLOR_BROWN         0x06
#define COLOR_LIGHT_GRAY    0x07
#define COLOR_DARK_GRAY     0x08
#define COLOR_LIGHT_BLUE    0x09
#define COLOR_LIGHT_GREEN   0x0A
#define COLOR_LIGHT_CYAN    0x0B
#define COLOR_LIGHT_RED     0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW        0x0E
#define COLOR_WHITE         0x0F

// Legacy filesystem disk region used by filesystem.c.
// Keep it away from the new block-based FS region to avoid on-disk overlap.
#define FS_DISK_SECTOR 18400
#define FS_SECTOR_COUNT 320

// --- Common Definitions ---
#define MAX_PATH_LENGTH 128
#define MAX_NAME_LENGTH 32
#define MAX_CHILDREN 32
#define MAX_NODES 32
// --- User Authentication ---
#define MAX_USERS 8

#define MAX_GROUPS 8

typedef struct {
    char name[MAX_NAME_LENGTH];
    unsigned int bitmask; // 1, 2, 4, 8, ...
    int used;
} Group;

extern Group group_table[MAX_GROUPS];
extern int group_count;
#define HASH_SIZE 32 // 256-bit hash

#define IS_EFFECTIVE_ADMIN(idx) (user_table[idx].is_admin || (user_table[idx].groups & group_table[0].bitmask))

typedef struct {
    char username[MAX_NAME_LENGTH];
    unsigned char password_hash[HASH_SIZE];
    int is_admin;
    unsigned int groups; // bitmask for group membership
} User;

// Group definitions
#define GROUP_ADMIN   0x01
#define GROUP_USERS   0x02
#define GROUP_GUESTS  0x04
#define GROUP_NET     0x08
#define GROUP_DEV     0x10

// Hashing
void hash_password(const char* password, unsigned char* out_hash);

extern User user_table[MAX_USERS];
extern int user_count;
extern int current_user_idx; // -1 means no user logged in

// --- Filesystem Configuration ---
#define MAX_FILE_CONTENT 2048
#define MAX_FILE_NAME 32
#define MAX_CMD_BUFFER 2048

// --- Process Management ---
#define MAX_PROCESSES 64
#define MAX_PROCESS_MMAP_REGIONS 16

typedef struct {
    unsigned int addr;
    unsigned int size;
    unsigned int order;
    int in_use;
} ProcessMMapRegion;

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_EXITED
} ProcessState;

typedef struct {
    int pid;
    ProcessState state;
    char name[16];
    unsigned int page_directory;
    unsigned int esp;
    unsigned int eip;
    unsigned int stack_guard_base;
    unsigned int stack_base;
    unsigned int stack_size;
    unsigned int user_stack_base;
    unsigned int user_stack_size;
    unsigned int run_ticks;
    unsigned int regs[8];
    unsigned int pending_signals;
    unsigned int last_irq_eip;
    unsigned int last_irq_cs;
    unsigned int last_irq_eflags;
    unsigned int last_irq_esp;
    unsigned int last_irq_ss;
    int parent_pid;
    unsigned int brk_base;
    unsigned int brk_current;
    unsigned int brk_limit;
    ProcessMMapRegion mmap_regions[MAX_PROCESS_MMAP_REGIONS];
    int fork_context_valid;
    unsigned int fork_edi;
    unsigned int fork_esi;
    unsigned int fork_ebp;
    unsigned int fork_ebx;
    unsigned int fork_edx;
    unsigned int fork_ecx;
    unsigned int fork_eax;
    unsigned int fork_eip;
    unsigned int fork_cs;
    unsigned int fork_eflags;
    unsigned int fork_user_esp;
    unsigned int fork_user_ss;
} PCB;

typedef struct {
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp_dummy;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
    unsigned int user_esp;
    unsigned int user_ss;
} IRQContext;

extern PCB process_table[MAX_PROCESSES];
extern int current_process;
void init_process_table(void);

// Create a new process
int process_create(unsigned int entry_point);

// Switch context between two processes
void context_switch(int from_pid, int to_pid);

// Simple round-robin scheduler
void schedule(void);

// Terminate current process
void process_exit(void);

// Voluntarily yield CPU
void process_yield(void);

// Spawn a demo background process
int process_spawn_demo(void);

// Spawn demo process with custom work ticks (0 = unlimited)
int process_spawn_demo_with_work(unsigned int work_ticks);

// Spawn a user-mode (ring-3) Linux-ABI smoke-test process
int process_spawn_ring3_demo(void);

// Spawn a ring-3 process that intentionally faults on kernel-memory access.
int process_spawn_ring3_fault_demo(void);
void process_fork_resume_entry(void);

// Kill process by pid
int process_kill(int pid);

// Enable/disable auto-respawn for demo process
void process_set_demo_autorespawn(int enabled);
int process_get_demo_autorespawn(void);

// Periodic maintenance tasks for process subsystem
void process_maintenance_tick(void);

// Human-readable process state
const char* process_state_name(ProcessState state);
int process_send_signal(int pid, int signal_number);
void process_deliver_pending_signals(IRQContext* ctx);
void process_record_irq_context(const IRQContext* ctx);

// --- Interrupt Definitions ---
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

struct IDT_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct IDT_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

// --- Global Variables ---
extern volatile int ticks;
extern char last_key;
extern int just_saved;
extern int skip_next_prompt;
extern char history[10][64];
extern int history_count;
extern int line_start;
extern int cmd_len;
extern int cmd_cursor;
extern int history_position;
extern int tab_completion_active;
extern int tab_completion_position;
extern int tab_match_count;
extern char tab_matches[32][32];

typedef struct {
    int col;
    int row;
    int wheel_delta;
} MouseState;

typedef struct {
    int found;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t io_base;
    uint8_t irq_line;
} PciRtl8139Info;

typedef struct {
    int present;
    int initialized;
    uint32_t io_base;
    uint8_t irq_line;
    uint8_t mac[6];
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t last_rx_length;
} Rtl8139Status;

typedef struct {
    uint32_t frames_polled;
    uint32_t ipv4_parsed;
    uint32_t non_ipv4_frames;
    uint32_t bad_version;
    uint32_t bad_ihl;
    uint32_t bad_total_length;
    uint32_t bad_checksum;
    uint8_t last_src_ip[4];
    uint8_t last_dst_ip[4];
    uint8_t last_protocol;
    uint8_t last_ttl;
    uint16_t last_total_length;
} IPv4Stats;

typedef struct {
    uint32_t frames_polled;
    uint32_t icmp_seen;
    uint32_t echo_requests;
    uint32_t echo_replies_sent;
    uint32_t echo_replies_received;
    uint32_t parse_errors;
} ICMPStats;

typedef struct {
    uint32_t frames_polled;
    uint32_t udp_seen;
    uint32_t non_udp_ipv4;
    uint32_t parse_errors;
    uint32_t sent_packets;
    uint32_t recv_queued;
    uint32_t recv_dropped;
    uint8_t last_src_ip[4];
    uint8_t last_dst_ip[4];
    uint16_t last_src_port;
    uint16_t last_dst_port;
    uint16_t last_payload_length;
} UDPStats;

typedef struct {
    uint32_t frames_polled;
    uint32_t tcp_seen;
    uint32_t parse_errors;
    uint32_t syn_received;
    uint32_t synack_sent;
    uint32_t established;
    uint32_t ack_sent;
    uint32_t rst_seen;
} TCPStats;

typedef struct {
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t state;
} TCPConnInfo;

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

#define LOGGER_MESSAGE_MAX 96

typedef struct {
    uint32_t tick;
    uint8_t level;
    char message[LOGGER_MESSAGE_MAX];
} LogEntry;

// --- Function Declarations ---

// Memory management
void init_paging(uint32_t mb_magic, uint32_t mb_info_addr);
#define PAGE_FLAG_PRESENT 0x001u
#define PAGE_FLAG_RW      0x002u
#define PAGE_FLAG_USER    0x004u
#define PAGE_FLAG_COW     0x200u

void* alloc_page(void);
void* alloc_pages(unsigned int order);
void free_page(void* addr);
void free_pages(void* addr, unsigned int order);
int memory_smoke_test(void);
void* kmalloc(unsigned int size);
void kfree(void* ptr);
unsigned int paging_get_kernel_directory(void);
unsigned int paging_create_process_directory(unsigned int user_code_addr,
                                             unsigned int user_stack_base,
                                             unsigned int user_stack_size);
void paging_destroy_process_directory(unsigned int page_directory);
void paging_switch_directory(unsigned int page_directory);
int paging_mark_user_range(unsigned int page_directory, unsigned int start, unsigned int size);
int paging_get_mapping(unsigned int page_directory,
                       unsigned int vaddr,
                       unsigned int* out_phys,
                       unsigned int* out_flags);
int paging_map_page(unsigned int page_directory,
                    unsigned int vaddr,
                    unsigned int phys,
                    unsigned int flags);
int paging_set_page_writable(unsigned int page_directory, unsigned int vaddr, int writable);
void paging_inc_page_ref(unsigned int phys_addr);
unsigned int paging_get_page_ref(unsigned int phys_addr);
void paging_dec_page_ref(unsigned int phys_addr);

// Protection (GDT/TSS/Ring setup)
void init_protection(void);
int protection_is_ready(void);
unsigned int protection_get_cpl(void);

// Interrupts
void pic_remap(void);
void set_idt_entry(int n, unsigned int handler);
void set_idt_entry_user(int n, unsigned int handler);
void timer_handler(IRQContext* ctx);
void keyboard_handler(void);
void mouse_handler(void);
int keyboard_pop_scancode(unsigned char* out_scancode);
void mouse_init(void);
void mouse_poll_hardware(void);
int mouse_poll_state(MouseState* state);
extern void load_idt(void*);
extern void irq0_timer_handler();
extern void irq1_keyboard_handler();
extern void irq12_mouse_handler();
extern void isr_syscall_handler();

// Syscalls
// saved_cs: CS selector at the time int 0x80 fired (bits 1:0 = RPL).
// Ring-3 callers are automatically routed to linux_syscall_dispatch().
unsigned int syscall_dispatch(unsigned int number,
                              unsigned int arg0,
                              unsigned int arg1,
                              unsigned int arg2,
                              unsigned int saved_cs,
                              IRQContext* ctx);
unsigned int syscall_invoke(unsigned int number);
unsigned int syscall_invoke1(unsigned int number, unsigned int arg0);
unsigned int syscall_invoke2(unsigned int number, unsigned int arg0, unsigned int arg1);
unsigned int syscall_invoke3(unsigned int number, unsigned int arg0, unsigned int arg1, unsigned int arg2);

#define SYS_YIELD      0u
#define SYS_GET_TICKS  1u
#define SYS_GET_PID    2u
#define SYS_WAIT_TICKS 3u
#define SYS_SPAWN_DEMO 4u
#define SYS_KILL_PID   5u
#define SYS_GET_CPL    6u
#define SYS_OPEN       7u
#define SYS_CLOSE      8u
#define SYS_READ       9u
#define SYS_WRITE      10u

// Filesystem
int fs_mkdir(const char* path);

#define FS_O_READ   0x01
#define FS_O_WRITE  0x02
#define FS_O_CREATE 0x04
#define FS_O_TRUNC  0x08
#define FS_O_APPEND 0x10

void fs_fd_init(void);
int fs_fd_open(const char* path, int flags);
int fs_fd_close(int fd);
int fs_fd_read(int fd, char* buffer, int count);
int fs_fd_write(int fd, const char* buffer, int count);
void fs_fd_close_for_pid(int pid);

// ELF loader
int elf_load(const char* path, PCB* proc);

// String utilities
int str_len(const char* s);
void str_copy(char* dst, const char* src, int max);
int str_equal(const char* a, const char* b);
int mini_strcmp(const char* a, const char* b);
void int_to_str(int value, char* buf);
void str_concat(char* dest, const char* src);

// Logger
void logger_init(void);
void log_set_level(int level);
int log_get_level(void);
const char* log_level_name(int level);
void log_write(int level, const char* message);
int log_count(void);
int log_get_entry(int oldest_index, LogEntry* out_entry);
void log_clear(void);

// Clipboard
void clipboard_clear(void);
int clipboard_set_text(const char* text);
int clipboard_set_text_len(const char* text, int len);
const char* clipboard_get_text(void);
int clipboard_get_length(void);
int clipboard_has_text(void);
int clipboard_copy_word_at(const char* text, int len, int cursor_index);

// Panic handling
void kernel_panic(const char* reason, const char* detail);
void exception_handler(unsigned int vector, unsigned int error_code, unsigned int eip, unsigned int cs, unsigned int eflags);
int handle_page_fault(unsigned int* frame, unsigned int fault_addr);
extern void (*exception_stub_table[32])(void);

// Display
void scroll_screen(char* video);
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_smiggles_art(char* video, int* cursor);
void set_cursor_position(int cursor);
char scancode_to_char(unsigned char scancode, int shift);
void display_init(char* video);
void display_hide_mouse(char* video);
void display_refresh_mouse(char* video);
void display_set_mouse_position(int col, int row);
int display_scroll_view(int delta, char* video);
int display_is_scrollback_active(void);
void display_sync_live_screen(char* video);
void display_restore_live_screen(char* video);
void display_begin_capture(char* buffer, int max_len, int suppress_screen);
int display_end_capture(void);

// Editor
void nano_editor(const char* filename, char* video, int* cursor);

// Calculator
int is_math_expr(const char* s);
void handle_calc_command(const char* expr, char* video, int* cursor);

// Tiny BASIC
void basic_repl(char* video, int* cursor);
int basic_run_file(const char* filename, char* video, int* cursor);

// Commands
void dispatch_command(const char* cmd, char* video, int* cursor);
void add_to_history(const char* cmd);
int find_file(const char* name);
void handle_clear_command(char* video, int* cursor);
void handle_tab_completion(char* cmd_buf, int* cmd_len, int* cmd_cursor, char* video, int* cursor, int line_start);

// Time utilities
unsigned char cmos_read(unsigned char reg);
unsigned char bcd_to_bin(unsigned char bcd);
void get_time_string(char* buf);
void time_settings_load(void);
void time_settings_save(void);
int time_set_timezone(const char* tz_name);
const char* time_get_timezone_name(void);

// Disk I/O for persistent storage
int disk_read_sector(unsigned int lba, void* buffer);
int disk_write_sector(unsigned int lba, const void* buffer);

// ATA PIO disk driver
int ata_read_sector(unsigned int lba, void* buffer);
int ata_write_sector(unsigned int lba, const void* buffer);

// PCI
int pci_find_rtl8139(PciRtl8139Info* out_info);
int pci_enable_device_io_busmaster(uint8_t bus, uint8_t slot, uint8_t function);
void pci_scan_and_print(char* video, int* cursor);

// RTL8139
int rtl8139_init(void);
int rtl8139_get_status(Rtl8139Status* out_status);
int rtl8139_send_frame(const uint8_t* frame, int length);
int rtl8139_poll_receive(uint8_t* frame_out, int max_length, int* out_length);
void rtl8139_print_status(char* video, int* cursor);

// ARP
#define ARP_CACHE_SIZE 8
int arp_set_local_ip(const uint8_t ip[4]);
int arp_get_local_ip(uint8_t ip_out[4]);
int arp_send_request(const uint8_t target_ip[4]);
int arp_poll_once(void);
int arp_process_frame(const uint8_t* frame, int length);
int arp_get_cache_count(void);
int arp_get_cache_entry(int index, uint8_t ip_out[4], uint8_t mac_out[6]);
int arp_lookup_mac(const uint8_t ip[4], uint8_t mac_out[6]);

// IPv4
int ipv4_poll_once(void);
int ipv4_get_stats(IPv4Stats* out_stats);

// ICMP
int icmp_send_echo_request(const uint8_t target_ip[4], uint16_t identifier, uint16_t sequence);
int icmp_poll_once(void);
int icmp_process_frame(const uint8_t* frame, int length);
int icmp_get_stats(ICMPStats* out_stats);

// UDP
int udp_send_datagram(const uint8_t target_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t* payload, int payload_len);
int udp_poll_once(void);
int udp_process_frame(const uint8_t* frame, int length);
int udp_recv_next(uint8_t src_ip_out[4], uint16_t* src_port_out, uint16_t* dst_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len);
int udp_recv_next_for_port(uint16_t dst_port_filter, uint8_t src_ip_out[4], uint16_t* src_port_out, uint16_t* dst_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len);
int udp_discard_for_port(uint16_t dst_port_filter);
int udp_get_stats(UDPStats* out_stats);
int udp_set_listen_port(uint16_t port);
int udp_clear_listen_port(void);
int udp_get_listen_port(uint16_t* out_port);

typedef struct {
    int in_use;
    int type;
    uint16_t local_port;
} SocketInfo;

#define SOCK_TYPE_UDP 1

// Minimal sockets API (UDP milestone)
int sock_open_udp(void);
int sock_bind(int fd, uint16_t local_port);
int sock_sendto(int fd, const uint8_t target_ip[4], uint16_t target_port, const uint8_t* payload, int payload_len);
int sock_recvfrom(int fd, uint8_t src_ip_out[4], uint16_t* src_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len);
int sock_close(int fd);
int sock_get_count(void);
int sock_get_info(int index, SocketInfo* out_info);

// TCP (handshake milestone)
int tcp_poll_once(void);
int tcp_process_frame(const uint8_t* frame, int length);
int tcp_set_listen_port(uint16_t port);
int tcp_clear_listen_port(void);
int tcp_get_listen_port(uint16_t* out_port);
int tcp_get_stats(TCPStats* out_stats);
int tcp_get_conn_count(void);
int tcp_get_conn_info(int index, TCPConnInfo* out_info);

// Unified network dispatcher
int net_poll_once(void);

// ── Real context switch (kernel_entry.asm) ──────────────────────────────────
// Saves edi/esi/ebx/ebp/eflags on the current stack, stores ESP in
// *save_esp, loads load_esp, restores the saved frame and returns into
// the new process.  Used by context_switch() in process.c.
extern void context_switch_asm(unsigned int* save_esp,
                               unsigned int load_esp,
                               unsigned int load_cr3);

// Set TSS.esp0 for ring transitions into kernel mode.
void protection_set_kernel_stack(unsigned int kernel_esp0);

// Drop CPU into CPL=3 (ring 3) at entry:user_esp via iretd.
// GDT must have user code (0x18) and user data (0x20) descriptors.
extern void jump_to_ring3(unsigned int entry, unsigned int user_esp);
extern void resume_from_irq_context(IRQContext* ctx);

// ── Linux i386 syscall ABI compatibility ─────────────────────────────────────
// When a ring-3 process calls  int 0x80  with eax = one of these numbers
// we dispatch through linux_syscall_dispatch() instead of our own table.
#define LINUX_SYS_EXIT          1
#define LINUX_SYS_FORK          2
#define LINUX_SYS_READ          3
#define LINUX_SYS_WRITE         4
#define LINUX_SYS_OPEN          5
#define LINUX_SYS_CLOSE         6
#define LINUX_SYS_WAITPID       7
#define LINUX_SYS_EXECVE        11
#define LINUX_SYS_GETPID        20
#define LINUX_SYS_GETUID        24
#define LINUX_SYS_GETGID        47
#define LINUX_SYS_GETEUID       49
#define LINUX_SYS_GETEGID       50
#define LINUX_SYS_BRK           45
#define LINUX_SYS_IOCTL         54
#define LINUX_SYS_MMAP          90
#define LINUX_SYS_MUNMAP        91
#define LINUX_SYS_UNAME         122
#define LINUX_SYS_WRITEV        146
#define LINUX_SYS_EXIT_GROUP    252

#define LINUX_SIGINT  2
#define LINUX_SIGPIPE 13
#define LINUX_SIGTERM 15
#define LINUX_SIGCHLD 17

// Linux errno values returned as negative integers from syscalls
#define LINUX_EPERM     (-1)
#define LINUX_ENOENT    (-2)
#define LINUX_EBADF     (-9)
#define LINUX_ENOMEM    (-12)
#define LINUX_EFAULT    (-14)
#define LINUX_EINVAL    (-22)
#define LINUX_ENOSYS    (-38)

// Linux uname() struct (old_utsname layout used by most static binaries)
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} LinuxUtsname;

// Process ring level flags (stored in PCB in the future)
#define PROC_RING_KERNEL 0
#define PROC_RING_USER   3

// Dispatch a Linux-ABI syscall.  Called from syscall_dispatch when eax
// matches a known Linux syscall number.
unsigned int linux_syscall_dispatch(unsigned int number,
                                    unsigned int arg0,
                                    unsigned int arg1,
                                    unsigned int arg2,
                                    IRQContext* ctx);

#endif // KERNEL_H

// I/O port functions for ATA
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned short inw(unsigned short port) {
    unsigned short ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(unsigned short port, unsigned short val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(unsigned short port, unsigned int val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}