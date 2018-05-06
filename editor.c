#include <unistd.h>

int main() {
    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1 && !(ch == 'q' || ch == 'Q'));
    return 0;
}
