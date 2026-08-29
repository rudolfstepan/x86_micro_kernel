typedef unsigned long long shell_u64;
typedef long long shell_i64;
typedef unsigned int shell_u32;
typedef unsigned char shell_u8;

#define REIST_SYS_EXIT 9ULL
#define REIST_SYS_READ 15ULL
#define REIST_SYS_WRITE 20ULL
#define REIST_SYS_GETPID 22ULL
#define REIST_SYS_SPAWN 23ULL
#define REIST_SYS_WAIT 24ULL
#define REIST_SYS_SPAWNV 30ULL
#define REIST_SYS_YIELD 40ULL
#define REIST_SYS_IPC_CREATE 49ULL
#define REIST_SYS_IPC_SEND 50ULL
#define REIST_SYS_IPC_RECEIVE 51ULL
#define REIST_SYS_IPC_CLOSE 52ULL
#define REIST_SYS_IPC_RECEIVE_TIMEOUT 54ULL
#define REIST_SYS_IPC_DELEGATE 55ULL
#define REIST_SYS_IPC_RELEASE 58ULL
#define REIST_STDIN 0ULL
#define REIST_STDOUT 1ULL
#define REIST_EAGAIN (-11LL)
#define REIST_ETIMEDOUT (-110LL)
#define SHELL_COMMAND_CAPACITY 16U
#define SHELL_POLL_LIMIT 67108864U
#define SHELL_PARENT_PID 300LL
#define SHELL_CHILD_PID 301LL
#define SHELL_CHILD_STATUS 77U
#define IPC_MESSAGE_VERSION 1U
#define IPC_MESSAGE_SIZE 140U
#define IPC_MESSAGE_LENGTH 8U
#define IPC_RIGHT_SEND 0x01U
#define IPC_RECEIVE_TIMEOUT_MS 10ULL

typedef struct {
    shell_u32 version;
    shell_u32 struct_size;
    shell_u32 length;
    shell_u8 payload[128];
} shell_ipc_message_t;

_Static_assert(sizeof(shell_ipc_message_t) == IPC_MESSAGE_SIZE,
               "REIST-v1 IPC message size changed");

static shell_i64 shell_syscall3(shell_u64 number, shell_u64 first,
                                shell_u64 second, shell_u64 third)
{
    register shell_u64 rax __asm__("rax") = number;
    register shell_u64 rdi __asm__("rdi") = first;
    register shell_u64 rsi __asm__("rsi") = second;
    register shell_u64 rdx __asm__("rdx") = third;

    __asm__ volatile("syscall"
                     : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx)
                     : "rcx", "r11", "memory");
    return (shell_i64)rax;
}

static shell_i64 shell_write(const char *message, shell_u64 length)
{
    return shell_syscall3(REIST_SYS_WRITE, REIST_STDOUT,
                          (shell_u64)message, length);
}

static int shell_write_exact(const char *message, shell_u64 length)
{
    return shell_write(message, length) == (shell_i64)length;
}

static __attribute__((noreturn)) void shell_exit(shell_u64 status)
{
    (void)shell_syscall3(REIST_SYS_EXIT, status, 0ULL, 0ULL);
    __asm__ volatile("ud2");
    __builtin_unreachable();
}

static int command_equals(const shell_u8 *command, const char *expected,
                          shell_u32 expected_length,
                          shell_u32 actual_length)
{
    shell_u32 index;

    if (actual_length != expected_length) {
        return 0;
    }
    for (index = 0U; index < expected_length; ++index) {
        if (command[index] != (shell_u8)expected[index]) {
            return 0;
        }
    }
    return 1;
}

static void clear_command(shell_u8 *command)
{
    shell_u32 index;

    for (index = 0U; index < SHELL_COMMAND_CAPACITY; ++index) {
        command[index] = 0U;
    }
}

static void clear_ipc_message(shell_ipc_message_t *message)
{
    shell_u32 index;
    shell_u8 *bytes = (shell_u8 *)message;

    for (index = 0U; index < IPC_MESSAGE_SIZE; ++index) {
        bytes[index] = 0U;
    }
}

static int ipc_message_is_token(const shell_ipc_message_t *message,
                                shell_u8 final_digit)
{
    static const shell_u8 prefix[IPC_MESSAGE_LENGTH - 2U] = {
        (shell_u8)'t', (shell_u8)'o', (shell_u8)'k',
        (shell_u8)'e', (shell_u8)'n', (shell_u8)'7'
    };
    shell_u32 index;

    if (message->version != IPC_MESSAGE_VERSION ||
        message->struct_size != IPC_MESSAGE_SIZE ||
        message->length != IPC_MESSAGE_LENGTH) {
        return 0;
    }
    for (index = 0U; index < IPC_MESSAGE_LENGTH - 2U; ++index) {
        if (message->payload[index] != prefix[index]) {
            return 0;
        }
    }
    if (message->payload[index] != final_digit ||
        message->payload[index + 1U] != 0U) {
        return 0;
    }
    index += 2U;
    for (; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

static int ipc_message_is_empty(const shell_ipc_message_t *message)
{
    shell_u32 index;

    if (message->version != IPC_MESSAGE_VERSION ||
        message->struct_size != IPC_MESSAGE_SIZE || message->length != 0U) {
        return 0;
    }
    for (index = 0U; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

void _start(void)
{
    static const char ready[] = "REIST_X86_64_RING3_SHELL_READY\r\n";
    static const char prompt[] = "C:\\>";
    static const char info[] = "REIST_X86_64_RING3_SHELL_INFO_OK\r\n";
    static const char help[] = "HELP INFO RUN EXIT\r\n";
    static const char run_ok[] = "REIST_X86_64_RING3_SHELL_RUN_OK\r\n";
    static const char unknown[] = "Unknown command\r\n";
    shell_u8 command[SHELL_COMMAND_CAPACITY];
    shell_u8 input_byte = 0U;
    shell_u32 command_length = 0U;
    shell_u32 polls = 0U;

    clear_command(command);
    if (!shell_write_exact(ready, sizeof(ready) - 1U) ||
        !shell_write_exact(prompt, sizeof(prompt) - 1U)) {
        shell_exit(2ULL);
    }

    while (polls < SHELL_POLL_LIMIT) {
        shell_i64 result = shell_syscall3(REIST_SYS_READ, REIST_STDIN,
                                          (shell_u64)&input_byte, 1ULL);
        ++polls;
        if (result == REIST_EAGAIN) {
            (void)shell_syscall3(REIST_SYS_YIELD, 0ULL, 0ULL, 0ULL);
            continue;
        }
        if (result != 1LL) {
            shell_exit(3ULL);
        }
        polls = 0U;
        if (input_byte == '\r' || input_byte == '\n') {
            if (command_equals(command, "INFO", 4U, command_length)) {
                if (!shell_write_exact(info, sizeof(info) - 1U) ||
                    !shell_write_exact(prompt, sizeof(prompt) - 1U)) {
                    shell_exit(4ULL);
                }
            } else if (command_equals(command, "HELP", 4U, command_length)) {
                if (!shell_write_exact(help, sizeof(help) - 1U) ||
                    !shell_write_exact(prompt, sizeof(prompt) - 1U)) {
                    shell_exit(5ULL);
                }
            } else if (command_equals(command, "RUN", 3U, command_length)) {
                shell_u8 child_path[SHELL_COMMAND_CAPACITY] __attribute__((aligned(8))) =
                    "/shell/child";
                shell_u8 child_token[SHELL_COMMAND_CAPACITY] __attribute__((aligned(8))) =
                    "token77";
                shell_u64 child_argv[2] __attribute__((aligned(8))) = {
                    (shell_u64)child_path, (shell_u64)child_token
                };
                shell_u32 child_status __attribute__((aligned(4))) = 0U;
                shell_u32 ipc_handle __attribute__((aligned(4))) = 0U;
                shell_ipc_message_t ipc_message __attribute__((aligned(8)));
                shell_i64 parent_pid = shell_syscall3(REIST_SYS_GETPID, 0ULL, 0ULL, 0ULL);
                shell_i64 child_pid;
                shell_i64 waited_pid;

                if (parent_pid != SHELL_PARENT_PID) {
                    shell_exit(11ULL);
                }
                if (shell_syscall3(REIST_SYS_IPC_CREATE,
                                   (shell_u64)&ipc_handle, 0ULL, 0ULL) != 0LL ||
                    ipc_handle == 0U) {
                    shell_exit(15ULL);
                }
                clear_ipc_message(&ipc_message);
                ipc_message.version = IPC_MESSAGE_VERSION;
                ipc_message.struct_size = IPC_MESSAGE_SIZE;
                if (shell_syscall3(REIST_SYS_IPC_RECEIVE_TIMEOUT,
                                   (shell_u64)ipc_handle,
                                   (shell_u64)&ipc_message,
                                   IPC_RECEIVE_TIMEOUT_MS) != REIST_ETIMEDOUT ||
                    !ipc_message_is_empty(&ipc_message)) {
                    shell_exit(19ULL);
                }
                child_pid = shell_syscall3(REIST_SYS_SPAWNV,
                                           (shell_u64)child_path,
                                           (shell_u64)child_argv, 2ULL);
                if (child_pid != SHELL_CHILD_PID) {
                    shell_exit(12ULL);
                }
                if (shell_syscall3(REIST_SYS_IPC_DELEGATE,
                                   (shell_u64)ipc_handle,
                                   (shell_u64)child_pid,
                                   IPC_RIGHT_SEND) != 0LL) {
                    shell_exit(16ULL);
                }
                if (shell_syscall3(REIST_SYS_YIELD, 0ULL, 0ULL, 0ULL) != 0LL ||
                    shell_syscall3(REIST_SYS_YIELD, 0ULL, 0ULL, 0ULL) != 0LL ||
                    shell_syscall3(REIST_SYS_YIELD, 0ULL, 0ULL, 0ULL) != 0LL) {
                    shell_exit(20ULL);
                }
                clear_ipc_message(&ipc_message);
                ipc_message.version = IPC_MESSAGE_VERSION;
                ipc_message.struct_size = IPC_MESSAGE_SIZE;
                if (shell_syscall3(REIST_SYS_IPC_RECEIVE,
                                   (shell_u64)ipc_handle,
                                   (shell_u64)&ipc_message, 0ULL) != 0LL ||
                    !ipc_message_is_token(&ipc_message, (shell_u8)'6')) {
                    shell_exit(21ULL);
                }
                clear_ipc_message(&ipc_message);
                ipc_message.version = IPC_MESSAGE_VERSION;
                ipc_message.struct_size = IPC_MESSAGE_SIZE;
                if (shell_syscall3(REIST_SYS_IPC_RECEIVE_TIMEOUT,
                                   (shell_u64)ipc_handle,
                                   (shell_u64)&ipc_message,
                                   IPC_RECEIVE_TIMEOUT_MS) != 0LL ||
                    !ipc_message_is_token(&ipc_message, (shell_u8)'7')) {
                    shell_exit(17ULL);
                }
                waited_pid = shell_syscall3(REIST_SYS_WAIT, (shell_u64)child_pid,
                                            (shell_u64)&child_status, 0ULL);
                if (waited_pid != SHELL_CHILD_PID ||
                    child_status != SHELL_CHILD_STATUS) {
                    shell_exit(13ULL);
                }
                if (shell_syscall3(REIST_SYS_IPC_CLOSE,
                                   (shell_u64)ipc_handle, 0ULL, 0ULL) != 0LL) {
                    shell_exit(18ULL);
                }
                if (!shell_write_exact(run_ok, sizeof(run_ok) - 1U) ||
                    !shell_write_exact(prompt, sizeof(prompt) - 1U)) {
                    shell_exit(14ULL);
                }
            } else if (command_equals(command, "EXIT", 4U, command_length)) {
                shell_exit(0ULL);
            } else if (command_length != 0U) {
                if (!shell_write_exact(unknown, sizeof(unknown) - 1U) ||
                    !shell_write_exact(prompt, sizeof(prompt) - 1U)) {
                    shell_exit(6ULL);
                }
            } else if (!shell_write_exact(prompt, sizeof(prompt) - 1U)) {
                shell_exit(7ULL);
            }
            clear_command(command);
            command_length = 0U;
            continue;
        }
        if (command_length + 1U >= SHELL_COMMAND_CAPACITY) {
            shell_exit(8ULL);
        }
        command[command_length] = input_byte;
        ++command_length;
        command[command_length] = 0U;
    }
    shell_exit(9ULL);
}
