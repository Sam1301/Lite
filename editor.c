#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

struct termios originalTermi;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermi);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &originalTermi);
    atexit(disableRawMode);

    struct termios raw = originalTermi;
    raw.c_iflag = raw.c_iflag & (~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag = raw.c_oflag & (~OPOST);
    raw.c_cflag = raw.c_cflag | (CS8);
    raw.c_lflag = raw.c_lflag & (~(ECHO | ICANON | IEXTEN | ISIG));

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1 && !(ch == 'q' || ch == 'Q')) {
        if (iscntrl(ch)) {
            printf("%d\r\n", ch);
        } else {
            printf("%d ('%c')\r\n", ch, ch);
        }
    }
    return 0;
}
