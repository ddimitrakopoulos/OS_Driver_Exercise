#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>  // For variable argument list handling

#define BASE_DIR "/dev"
#define MAX_FILENAME 256

// Function to create files /dev/lunixX-bat, /dev/lunixX-light, and /dev/lunixX-temp
void create_files(int X) {
    char path_bat[MAX_FILENAME], path_light[MAX_FILENAME], path_temp[MAX_FILENAME];

    // Create file paths
    snprintf(path_bat, MAX_FILENAME, "%s/lunix%d-bat", BASE_DIR, X);
    snprintf(path_light, MAX_FILENAME, "%s/lunix%d-light", BASE_DIR, X);
    snprintf(path_temp, MAX_FILENAME, "%s/lunix%d-temp", BASE_DIR, X);

    // Create the files
    int fd_bat = open(path_bat, O_CREAT | O_WRONLY, 0644);
    int fd_light = open(path_light, O_CREAT | O_WRONLY, 0644);
    int fd_temp = open(path_temp, O_CREAT | O_WRONLY, 0644);

    if (fd_bat < 0 || fd_light < 0 || fd_temp < 0) {
        perror("Failed to create files");
        if (fd_bat >= 0) close(fd_bat);
        if (fd_light >= 0) close(fd_light);
        if (fd_temp >= 0) close(fd_temp);
        exit(EXIT_FAILURE);
    }

    close(fd_bat);
    close(fd_light);
    close(fd_temp);

    printf("Files created successfully: %s, %s, %s\n", path_bat, path_light, path_temp);
}

// Function to remove files /dev/lunixX-bat, /dev/lunixX-light, and /dev/lunixX-temp
void remove_files(int X) {
    char path_bat[MAX_FILENAME], path_light[MAX_FILENAME], path_temp[MAX_FILENAME];

    // Create file paths
    snprintf(path_bat, MAX_FILENAME, "%s/lunix%d-bat", BASE_DIR, X);
    snprintf(path_light, MAX_FILENAME, "%s/lunix%d-light", BASE_DIR, X);
    snprintf(path_temp, MAX_FILENAME, "%s/lunix%d-temp", BASE_DIR, X);

    // Remove the files
    if (unlink(path_bat) != 0) perror("Failed to remove bat file");
    if (unlink(path_light) != 0) perror("Failed to remove light file");
    if (unlink(path_temp) != 0) perror("Failed to remove temp file");

    printf("Files removed successfully: %s, %s, %s\n", path_bat, path_light, path_temp);
}

// Function to write formatted data to a specified file
void write_formatted_data(const char *file_path, const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    int fd = open(file_path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        perror("Failed to open file for writing");
        exit(EXIT_FAILURE);
    }

    if (write(fd, buffer, strlen(buffer)) < 0) {
        perror("Failed to write to file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
    printf("Formatted data written to %s: %s\n", file_path, buffer);
}

// Test functions
int main() {
    int X = 1;  // Example identifier

    // Root privileges required to create files in /dev
    if (geteuid() != 0) {
        fprintf(stderr, "This program requires root privileges. Please run as sudo.\n");
        exit(EXIT_FAILURE);
    }

    create_files(X);

    // Specify the number of digits after the decimal point
    int decimal_places = 2;

    // Write formatted data to files with specified precision
    char format_bat[50];
    snprintf(format_bat, sizeof(format_bat), "Battery: %%0.%df%%\n", decimal_places);
    write_formatted_data("/dev/lunix1-bat", format_bat, 27.9);

    char format_light[50];
    snprintf(format_light, sizeof(format_light), "Light:  %%0.%df lux\n", decimal_places);
    write_formatted_data("/dev/lunix1-light", format_light, 345.1);

    char format_temp[50];
    snprintf(format_temp, sizeof(format_temp), "Temperature: %%0.%df C\n", decimal_places);
    write_formatted_data("/dev/lunix1-temp", format_temp, 23.45);

    // Remove files
    //remove_files(X);

    return 0;
}
