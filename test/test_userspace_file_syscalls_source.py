import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UserspaceFileSyscallSourceTests(unittest.TestCase):
    def test_sleepable_file_syscalls_do_not_disable_preemption(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        sleepable = (
            "OPEN", "READ", "CLOSE", "STAT", "READDIR", "CREATE",
            "OPEN_FLAGS", "LSEEK", "FSTAT", "FTRUNCATE", "TOUCH",
            "WRITE", "UNLINK", "SPAWN", "READDIR_BATCH", "GETCWD",
            "CHDIR", "SPAWNV", "SPACE", "MKDIR", "RMDIR", "RENAME",
            "FSYNC",
        )
        for name in sleepable:
            case = source[source.index(f"case SYS_{name}:") :]
            case = case[:case.index("break;")]
            self.assertNotIn("scheduler_preempt_disable()", case, name)
            self.assertNotIn("scheduler_preempt_enable()", case, name)

    def test_open_accepts_nonempty_copied_paths(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            source,
            re.compile(
                r"copy_string_from_user\(path, sizeof\(path\), user_path\)\s*"
                r"<\s*0"
            ),
        )

    def test_read_validates_userspace_destination(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("user_range_accessible", source)
        self.assertIn("copy_to_user", source)

    def test_relative_paths_use_the_process_working_directory(self):
        source = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("process_resolve_path", source)
        self.assertIn("process->working_directory", source)

    def test_program_loader_reads_the_image_sequentially(self):
        source = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        loader = source[source.index("static int load_program_file") :]
        loader = loader[:loader.index("\n}\n")]
        self.assertIn("vfs_read(node, 0, loaded_size", loader)
        self.assertNotIn("amount > 4096U", loader)

    def test_directory_metadata_is_copied_through_the_user_boundary(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("syscall_stat", source)
        self.assertIn("syscall_readdir", source)
        self.assertIn("syscall_copy_file_info", source)
        self.assertIn("copy_to_user(user_info", source)

    def test_file_writes_copy_data_from_userspace(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        write = source[source.index("static int syscall_write") :
                       source.index("static int syscall_fsync")]
        self.assertIn("syscall_create", source)
        self.assertIn("syscall_write", source)
        self.assertIn("syscall_unlink", source)
        self.assertIn("FILE_WRITE_CHUNK_CAPACITY (64U * 1024U)", source)
        self.assertIn("FILE_WRITE_STAGING_TIMEOUT_MS 10000U", source)
        self.assertIn("static uint8_t file_write_staging[", source)
        self.assertIn("static kernel_mutex_t file_write_staging_mutex = "
                      "KERNEL_MUTEX_INIT", source)
        self.assertIn("kernel_mutex_lock_for(&file_write_staging_mutex", write)
        self.assertIn("int copy_result = copy_from_user(", write)
        self.assertIn(
            "file_write_staging, (const uint8_t*)user_buffer + total", write
        )
        self.assertIn(
            "? process_file_write(process, descriptor, file_write_staging,",
            write,
        )
        self.assertIn("kernel_mutex_unlock(&file_write_staging_mutex)", write)
        self.assertIn("process_file_write_chunk_capacity", write)
        self.assertIn("VFS_DEFAULT_WRITE_CHUNK_CAPACITY", write)
        self.assertIn("if (amount > backend_capacity)", write)
        self.assertNotIn("uint8_t buffer[512]", write)
        self.assertNotIn(
            "descriptor, (const uint8_t*)user_buffer + total", write
        )
        process = (ROOT / "kernel/proc/process.c").read_text(
            encoding="utf-8"
        )
        capacity = process[
            process.index("uint32_t process_file_write_chunk_capacity"):
            process.index("int process_file_write(")
        ]
        self.assertIn("vfs_write_chunk_capacity(file->node, offset)", capacity)
        fat32 = (ROOT / "fs/fat32/fat32_vfs_adapter.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(".write_chunk_capacity = "
                      "fat32_vfs_write_chunk_capacity", fat32)
        self.assertIn("return FAT32_ORDERED_APPEND_MAX_BYTES;", fat32)

    def test_process_exit_closes_open_descriptors(self):
        source = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("process_close_all_files", source)

    def test_child_exit_status_is_retained_until_wait(self):
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("process_wait_status", process)
        self.assertIn("child->has_exited = false", process)
        self.assertIn("process->exit_status = status", scheduler)
        self.assertIn("process->has_exited = true", scheduler)
        self.assertIn("process_orphan_children", scheduler)

    def test_process_syscalls_validate_userspace_arguments(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("syscall_getpid", source)
        self.assertIn("copy_string_from_user(path, sizeof(path), user_path)", source)
        self.assertIn("syscall_wait", source)
        self.assertIn("copy_to_user(user_status", source)

    def test_wait_blocks_and_is_woken_by_child_exit(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        sdk = (ROOT / "userspace/sdk/x86os.c").read_text(encoding="utf-8")
        self.assertIn("TASK_WAITING", scheduler)
        self.assertIn("wait_queue_wake_all_locked", scheduler)
        self.assertIn("process_wait_status_locked", syscall)
        self.assertIn("process_table_lock_irqsave", syscall)
        self.assertIn("wait_queue_block_until_spinlocked", syscall)
        wait = sdk[sdk.index("int x86os_wait") :]
        wait = wait[:wait.index("\n}")]
        self.assertNotIn("x86os_delay", wait)

    def test_process_table_is_copied_to_userspace(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("syscall_process_info", source)
        self.assertIn("copy_to_user(user_info, &info, sizeof(info))", source)
        self.assertIn("process_get_info", process)
        self.assertIn("scheduler_terminate_task(task_id)", process)

    def test_working_directory_syscalls_stay_process_local(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("syscall_getcwd", source)
        self.assertIn("syscall_chdir", source)
        self.assertIn("copy_string_from_user(path, sizeof(path), user_path)", source)
        self.assertIn("process->working_directory", process)
        self.assertIn("entry.type != VFS_DIRECTORY", process)

    def test_spawnv_copies_argument_vector_from_userspace(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("syscall_spawnv", source)
        self.assertIn("char *arguments = (char*)k_malloc(", source)
        self.assertIn("copy_from_user(&user_argument", source)
        self.assertIn("copy_string_from_user(argument, SYSCALL_ARGUMENT_CAPACITY", source)
        self.assertIn("process_spawn_args(parent, path, argc, argument_list)", source)
        self.assertIn("k_free(arguments);", source)

    def test_drive_metadata_is_copied_without_kernel_pointers(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("syscall_drive_info", source)
        self.assertIn("copy_to_user(user_info, &info, sizeof(info))", source)

    def test_ls_has_compact_output_and_opt_in_paging(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        ls = (ROOT / "userspace/programs/ls.c").read_text(encoding="utf-8")
        self.assertIn("syscall_space", source)
        self.assertIn("vfs_space(path, &info)", source)
        self.assertIn("LS_COLUMNS 4U", ls)
        self.assertIn('text_equal(argument, "--pager")', ls)
        self.assertIn("options->pager", ls)
        self.assertIn("options->long_format", ls)
        self.assertIn("options->show_hidden", ls)
        self.assertIn("options->classify_dirs", ls)
        self.assertIn("options->human_sizes", ls)
        self.assertIn("key != 'q' && key != 'Q' && key != 0x1b", ls)

    def test_directory_mutation_is_available_to_userspace(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("syscall_mkdir", source)
        self.assertIn("syscall_rmdir", source)
        self.assertIn("process_drive_mount", process)
        self.assertIn("path[1] == ':'", process)

    def test_ctrl_c_terminates_blocked_userspace_input(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        getchar_case = source[source.index("case SYS_TERMINAL_GETCHAR:") :]
        getchar_case = getchar_case[:getchar_case.index("break;")]
        self.assertIn("result == 0x03U", getchar_case)
        self.assertIn("task_exit_status(130);", getchar_case)

    def test_blocking_keyboard_read_requires_sleepable_context(self):
        source = (ROOT / "drivers/char/kb.c").read_text(encoding="utf-8")
        getchar = source[source.index("char getchar(void)") :]
        getchar = getchar[:getchar.index("\n}")]
        self.assertIn("KASSERT_CAN_SLEEP();", getchar)
        self.assertNotIn("irq_enable();", getchar)
        self.assertIn("spinlock_acquire(&input_queue_lock)", getchar)
        self.assertIn("wait_queue_block_until_spinlocked", getchar)
        self.assertIn("&input_queue_lock, flags", getchar)
        self.assertNotIn("wait_queue_block_until_locked(", getchar)
        self.assertIn("KEYBOARD_POLL_INTERVAL_MS", getchar)
        self.assertIn('"hlt"', getchar)
        self.assertIn("wait_queue_wake_all_locked", source)

    def test_buffered_terminal_output_validates_userspace_memory(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        sdk = (ROOT / "userspace/sdk/x86os.c").read_text(encoding="utf-8")
        self.assertIn("static int syscall_terminal_write", source)
        self.assertIn("user_range_accessible", source)
        self.assertIn("X86OS_SYS_TERMINAL_WRITE", sdk)
        self.assertIn("static int syscall_terminal_draw", source)
        self.assertIn("X86OS_SYS_TERMINAL_DRAW", sdk)
        self.assertIn("X86OS_SYS_GETCHAR_NONBLOCKING", sdk)


if __name__ == "__main__":
    unittest.main()
