#include <unistd.h>
#include <termios.h>

void enableRawMode() {
    struct termios raw;

    tcgetattr(STDIN_FILENO, &raw);

    raw.c_lflag = raw.c_lflag & (~ECHO);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    
    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1 && !(ch == 'q' || ch == 'Q'));
    return 0;
}
