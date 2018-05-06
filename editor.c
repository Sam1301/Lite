#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

struct termios originalTermi;

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermi) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &originalTermi) == -1) 
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = originalTermi;
    raw.c_iflag = raw.c_iflag & (~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag = raw.c_oflag & (~OPOST);
    raw.c_cflag = raw.c_cflag | (CS8);
    raw.c_lflag = raw.c_lflag & (~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
    die("tcsetattr");
}

int main() {
    enableRawMode();

    while (1) {
        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) == -1 && errno != EAGAIN) 
            die("read");
        if (iscntrl(ch)) {
            printf("%d\r\n", ch);
        } else {
            printf("%d ('%c')\r\n", ch, ch);
        }

        if (ch == 'q') break;
    }
    return 0;
}
