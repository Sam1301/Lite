#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


struct termios originalTermi;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermi);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &originalTermi);
    atexit(disableRawMode);

    struct termios raw = originalTermi;
    raw.c_lflag = raw.c_lflag & (~ECHO);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1 && !(ch == 'q' || ch == 'Q'));
    return 0;
}
