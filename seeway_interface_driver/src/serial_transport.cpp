// Updated implementation to handle EINTR and short writes
void write_all(int fd, const char* buffer, size_t length) {
    size_t total_written = 0;
    while (total_written < length) {
        ssize_t result = write(fd, buffer + total_written, length - total_written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted system call
            }
            perror("Write error");
            break;
        } else if (result == 0) {
            // No bytes written, break out of the loop
            break;
        }
        total_written += result;
    }
}