#ifndef STDIO_H
#define STDIO_H


#include "../filesystem/fat32.h"
#include "../drivers/video/video.h"
#include "../kernel/system.h"

// File Handling Functions
File* fopen(const char* filename, const char* mode);
// int fclose(int fd);
int fread(void* buffer, int size, File* fd);
// int fwrite(void* buffer, int size, int count, int fd);
// int fseek(int fd, int offset, int whence);
// int ftell(int fd);
int rmfile(const char* path);                                   // remove file
int mkfile(const char* path);                                   // create file

// Directory Handling Functions
int mkdir(const char* path);                                    // make directory
int rmdir(const char* path);                                    // remove directory
int chdir(const char* path);                                    // change directory
//int getcwd(char* buffer, int size);
int readdir(const char* path, char* buffer, unsigned int *size);// read directory

// Console Functions
int printf(const char* format, ...);                            // print formatted string
// int scanf(const char* format, ...);
// int getchar();
// int putchar(int character);
// int puts(const char* string)
// int gets(char* string);

// // Utility Functions
// int strlen(const char* string);
// int strcmp(const char* string1, const char* string2);
// int strcpy(char* destination, const char* source);
// int strncpy(char* destination, const char* source, int count);
// int strcat(char* destination, const char* source);
// int strncat(char* destination, const char* source, int count);
// int strtok(const char* string, const char* delimiters, char* token, int token_size);
// int atoi(const char* string);
// int itoa(int value, char* buffer, int radix);
// int htoa(int value, char* buffer);
// int normalize_path(const char* input_path, char* normalized_path, const char* current_path);
// int split_input(const char* input, char* command, char** arguments, int input_size, int max_arguments);
// int read_directory_path(const char* path);
// int change_directory(const char* path);
// int clear_screen();
// int beep(int frequency);
// int sleep(int milliseconds);
// int reboot();
// int shutdown();

// // System Functions
// int get_ticks();
// int get_time();
// int get_date();
// int get_version();
// int get_memory_map();
// int get_process_list();
// int get_process_count();
// int get_process_name(int pid, char* buffer);
// int get_process_state(int pid);
// int get_process_priority(int pid);
// int get_process_memory(int pid);
// int get_process_time(int pid);
// int get_process_ticks(int pid);

// // Math Functions
// int pow(int base, int exponent);
// int sqrt(int value);
// int rand();
// int srand(int seed);

// // Graphics Functions
// int draw_pixel(int x, int y, int color);
// int draw_line(int x1, int y1, int x2, int y2, int color);
// int draw_rectangle(int x, int y, int width, int height, int color);
// int draw_circle(int x, int y, int radius, int color);
// int draw_text(int x, int y, const char* text, int color);
// int draw_bitmap(int x, int y, const char* filename);

// // Sound Functions
// int play_sound(int frequency);
// int stop_sound();

// // Network Functions
// int ping(const char* address);
// int get_mac_address(char* buffer);
// int get_ip_address(char* buffer);
// int get_gateway_address(char* buffer);
// int get_subnet_mask(char* buffer);
// int get_dns_address(char* buffer);
// int get_dhcp_status();
// int get_dhcp_lease_time();
// int get_dhcp_renew_time();

// // Other Functions
// int execute(const char* filename, const char* arguments, int priority);
// int exit(int exit_code);
// int get_exit_code();
// int get_pid();
// int get_ppid();
// int get_process_count();

// // System Calls
// int system_call(int eax, int ebx, int ecx, int edx, int esi, int edi);



#endif // STDIO_H