/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <strings.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "0.0.1"
#define EDITOR_TAB 8
#define EDITOR_QUIT_TIMES 1
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

/*** data ***/

struct editorSyntax {
    char *filetype;
    char **filematch;
    int flags;
};

typedef struct editorrow {
    int length;
    char *text;
    int rsize;
    char *render;
    unsigned char *hl; // stores the syntax highlighting codes for each render char
} editorrow;

struct editorConfig {
    struct termios originalTermi;
    int screenrows; // 1 indexed
    int screencols; // 1 indexed
    int cursorX; // 0 indexed
    int cursorY; // 0 indexed
    editorrow* erow;
    int numrows; // 1 indexed
    int rowOff; // 0 indexed
    int colOff; // 0 indexed
    int renderX; // 0 indexed
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int dirty;
    struct editorSyntax *syntax;
} E;

/*** filetypes ***/
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *formatstr, ...);
char* editorPrompt(char *prompt, void (*callback)(char* query, int cur_key));

/*** struct append buffer ***/

#define APPEND_BUFFER_INIT {NULL, 0}

struct AppendBuffer {
    char *buffer;
    int length;
};

void abAppend(struct AppendBuffer* ab, char* s, int len) {
    char* new = realloc(ab->buffer, ab->length + len);

    if (new == NULL) {
        return ;
    }
    memcpy(&new[ab->length], s, len);
    ab->buffer = new;
    ab->length += len;
}

void abFree(struct AppendBuffer *ab) {
    free(ab->buffer);
}

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.originalTermi) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.originalTermi) == -1) 
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.originalTermi;
    raw.c_iflag = raw.c_iflag & (~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag = raw.c_oflag & (~OPOST);
    raw.c_cflag = raw.c_cflag | (CS8);
    raw.c_lflag = raw.c_lflag & (~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
    die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char ch;

    while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) 
            die("read");
    }

    if (ch == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';


        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }       
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return ch;
    }
    
}

int getCursorPosition(int *rows, int *cols) {
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    char buffer[32];
    unsigned int i = 0;
    while (i < sizeof(buffer)) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1) break;

        if (buffer[i] == 'R') break;
        i++;
    }
    buffer[i] = '\0';
    if (buffer[0] != '\x1b' || buffer[1] != '[') return -1;
    if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int * row, int * col) {

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(row, col);
    } else {
        *row = ws.ws_row;
        *col = ws.ws_col;
    }

    return 0;
}

/*** syntax highlighting ***/
int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(editorrow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL) return;

    int prev_sep = 1;
    int in_string = 0;


    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_NUMBER: 
            return 32;
        case HL_MATCH: // search query match
            return 31;
        case HL_STRING: 
            return 33;
        default: 
            return 37;
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.erow[filerow]);
                }

                return;
            }
            i++;
        }
    }
}

/*** row operations ***/

void editorUpdateRow(editorrow * row) {
    int tabs = 0;
    for (int i = 0 ; i < row->length ; i++) {
        if (row->text[i] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->length + tabs * (EDITOR_TAB - 1) + 1); // 1 char for tabs already counted in row.length
    
    int idx = 0;  
    for (int i = 0 ; i < row->length ; i++) {
        if (row->text[i] == '\t') {
            row->render[idx++] = ' ';
            while (idx % EDITOR_TAB != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->text[i];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) {
        return ;
    }
    
    E.erow = realloc(E.erow, sizeof(editorrow) * (E.numrows + 1));
    memmove(&E.erow[at+1], &E.erow[at], sizeof(editorrow) * (E.numrows - at));

    E.erow[at].length = len; // excluding '\0' at the end of string
    E.erow[at].text = malloc(len + 1);
    memcpy(E.erow[at].text, s, len+1);
    
    E.erow[at].render = NULL;
    E.erow[at].hl = NULL;

    E.erow[at].rsize = 0;
    editorUpdateRow(&E.erow[at]);

    E.numrows++;
    E.dirty++;
}

int editorRowCursorXToRenderX(editorrow * erow, int cx) {
    int rx = 0;
    for (int i = 0; i < cx; i++) {
        if (erow->text[i] == '\t') {
            rx += (EDITOR_TAB - 1) - (rx % EDITOR_TAB);
        }
        rx++;
    }
    
    return rx;
}

int editorRowRenderCToCursorX(editorrow *erow, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < erow->length; cx++) {
        if (erow->text[cx] == '\t') {
            cur_rx += (EDITOR_TAB - 1) - (cur_rx % EDITOR_TAB);
        }
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

void editorRowInsertChar(editorrow* erow, int at, char c) {
    if (at < 0 || at > erow->length) {
        at = erow->length;
    }

    erow->text = realloc(erow->text, erow->length + 2);
    memmove(&erow->text[at+1], &erow->text[at], erow->length - at + 1);
    erow->length++;
    erow->text[at] = c;
    editorUpdateRow(erow); // populate render and rsize for this erow
    E.dirty++;
}

void editorRowDelChar(editorrow* erow, int at) {
    if (at < 0 || at >= erow->length) {
        return;
    }

    memmove(&erow->text[at], &erow->text[at+1], erow->length - at);
    erow->length--;
    editorUpdateRow(erow);
    E.dirty++;    
}

void editorFreeRow(editorrow *row) {
    free(row->text);
    free(row->render);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) {
        return;
    }

    editorFreeRow(&E.erow[at]);
    memcpy(&E.erow[at], &E.erow[at+1], sizeof(editorrow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowAppendString(editorrow * row, char* s, size_t len) {
    row->text = realloc(row->text, row->length + len + 1); // +1 for null char
    memcpy(&row->text[row->length], s, len);
    row->length += len;
    row->text[row->length] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (E.cursorY == E.numrows) {
        editorInsertRow(E.numrows, "", 0); // add a new row after end of file
    }

    editorRowInsertChar(&E.erow[E.cursorY], E.cursorX, c);
    E.cursorX++;
}

void editorDelChar() {
    if (E.cursorY == E.screenrows || (E.cursorX == 0 && E.cursorY == 0)) {
        return;
    }

    editorrow *row = &E.erow[E.cursorY];
    if (E.cursorX > 0) {
        editorRowDelChar(row, E.cursorX-1);
        E.cursorX--;
    } else if (E.cursorX == 0) {
        E.cursorX = E.erow[E.cursorY-1].length;
        editorRowAppendString(&E.erow[E.cursorY-1], row->text, row->length);
        editorDelRow(E.cursorY);
        E.cursorY--;
    }
}

void editorInsertNewLine() {
    if (E.cursorX == 0) {
        editorInsertRow(E.cursorY, "", 0);
    } else {
        editorrow *row = &E.erow[E.cursorY];
        editorInsertRow(E.cursorY+1, &row->text[E.cursorX], row->length - E.cursorX);
        row = &E.erow[E.cursorY]; // reassign as editorInsertRow reallocates E.erow pointer
        row->length = E.cursorX;
        row->text[row->length] = '\0';
        editorUpdateRow(row);
    }

    E.cursorX = 0;
    E.cursorY++;
}

/*** file I/O ***/

void editorOpen(char * file) {
    free(E.filename);
    E.filename = strdup(file);

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(file, "r");
    if (!fp) {
        die("fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }

        editorInsertRow(E.numrows, line, linelen);
    }

    free(line);
    fclose(fp);
    E.dirty = 0; // when file is opened, there are no unsaved changes.
}

// caller should free the memory of pointer returned
char* editorRowsToString(int *len) {
    int total = 0;
    int i = 0;
    for (i = 0 ; i < E.numrows ; i++) {
        total += E.erow[i].length + 1; // +1 for '\n'
    }

    *len = total;

    char *buffer = malloc(total);
    char *temp = buffer;
    i = 0;
    for (i = 0 ; i < E.numrows ; i++) {
        memcpy(temp, E.erow[i].text, E.erow[i].length);
        temp += E.erow[i].length;
        temp[0] = '\n';
        temp++;
    }

    return buffer;
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to disk", len);
                E.dirty = 0; // changes saved successfully
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char* query, int cur_key) {
    static int last_match = -1; // last matching text y position
    static int direction = 1; // 1 for forward annd -1 for backward

    static int saved_hl_line;
    static char *saved_hl = NULL;
    
    if (saved_hl) {
        memcpy(E.erow[saved_hl_line].hl, saved_hl, E.erow[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (cur_key == '\r' || cur_key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (cur_key == ARROW_DOWN || cur_key == ARROW_RIGHT) {
        direction = 1;
    } else if (cur_key == ARROW_UP || cur_key == ARROW_LEFT) {
        direction = -1;
    } else {
        direction = 1;
        last_match = -1;
    }

    if (last_match == -1) {
        direction = 1;
    }
    int current = last_match;

    for (int i = 0 ; i < E.numrows ; i++) {
        current += direction;

        if (current == -1) {
            current = E.numrows - 1;
        } else if (current == E.numrows) {
            current = 0;
        }

        editorrow* erow = &E.erow[current];
        char *match = strstr(erow->render, query);
        if (match) {
            last_match = current;
            E.cursorY = current;
            E.cursorX = editorRowRenderCToCursorX(erow, match - erow->render);
            /* so that we are scrolled to the very bottom of the file, 
            which will cause editorScroll() to scroll upwards at the next 
            screen refresh so that the matching line will be at the very 
            top of the screen.*/
            E.rowOff = E.numrows;

            saved_hl_line = current;
            saved_hl = malloc(erow->rsize);
            memcpy(saved_hl, erow->hl, erow->rsize);
            memset(&erow->hl[match - erow->render], HL_MATCH, strlen(query));
            break; 
        }
    }
}

void editorFind() {
    int saved_cursorX = E.cursorX;
    int saved_cursorY = E.cursorY;
    int saved_colOff = E.colOff;
    int saved_rowOff = E.rowOff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    
    if (query) {
        free(query);
    } else {
        E.cursorX = saved_cursorX;
        E.cursorY = saved_cursorY;
        E.colOff = saved_colOff;
        E.rowOff = saved_rowOff;
    }
}

/*** output ***/

void editorDrawRows(struct AppendBuffer* ab) {
    for (int i = 0 ; i < E.screenrows ; i++) {
        int fileRow = i + E.rowOff;
        if (fileRow >= E.numrows) {
            if (E.numrows == 0 && i == E.screenrows / 2) {
                char welcome[80];

                int welcomeLen = snprintf(welcome, sizeof(welcome), "Text Editor -- version %s", EDITOR_VERSION);
                if (welcomeLen > E.screencols) welcomeLen = E.screencols;
                
                int leftPadding = (E.screencols - welcomeLen) / 2;
                if (leftPadding) {
                    abAppend(ab, "~", 1);
                }
                while (leftPadding > 0) {
                    abAppend(ab, " ", 1);
                    leftPadding--;
                }

                abAppend(ab, welcome, welcomeLen);
            } else {
                abAppend(ab, "~", 1);
            }  
        } else {
            int len = E.erow[fileRow].rsize - E.colOff;

            if (len < 0) {
                len = 0; // when user goes past the current line
            }
            if (len > E.screencols) {
                len = E.screencols;
            }

            char *c = &E.erow[fileRow].render[E.colOff];
            unsigned char *hl = &E.erow[fileRow].hl[E.colOff];
            int current_color = -1; // default color
            for (int j = 0; j < len; j++) {
                if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (current_color != color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }
        
        abAppend(ab, "\x1b[K", 3); // clear rest of current line
        abAppend(ab, "\r\n", 2);
    } 
}

void editorDrawStatusBar(struct AppendBuffer *ab) {
    abAppend(ab, "\x1b[7m", 4); // inverted colors on
    
    char status[80], lineStatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
        E.filename ? E.filename : "[No Name]", E.numrows,
        E.dirty ? "(modified)" : "");
    int lineLen = snprintf(lineStatus, sizeof(lineStatus), "%s | %d:%d", E.syntax ? E.syntax->filetype : "no filetype", E.cursorY + 1, E.numrows);

    if (len > E.screencols) {
        len = E.screencols;
    }
    abAppend(ab, status, len);

    while (len < E.screencols) {
        if (E.screencols - len == lineLen) {
            abAppend(ab, lineStatus, lineLen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // inverted colors off
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct AppendBuffer *ab) {
    abAppend(ab, "\x1b[K", 3);
    int messageLen = strlen(E.statusmsg);
    if (messageLen > E.screencols) {
        messageLen = E.screencols;
    }
    if (messageLen && time(NULL) - E.statusmsg_time < 5) {
        abAppend(ab, E.statusmsg, messageLen);
    }
}

void editorScroll() {
    E.renderX = 0;
    if (E.cursorY < E.numrows) {
        E.renderX = editorRowCursorXToRenderX(&E.erow[E.cursorY], E.cursorX);
    }

    if (E.cursorY < E.rowOff) { // going past top of the screen
        E.rowOff = E.cursorY;
    }

    if (E.cursorY >= E.rowOff + E.screenrows) { // going past bottom of the screen
        E.rowOff = E.cursorY - E.screenrows + 1;
    }

    if (E.renderX < E.colOff) { // going past left of the screen
        E.colOff = E.renderX;
    }

    if (E.renderX >= E.screencols + E.colOff) { // going past right of the screen
        E.colOff = E.renderX - E.screencols + 1;
    }
}

void editorRefreshTerminal() {
    editorScroll();

    struct AppendBuffer ab = APPEND_BUFFER_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide cursor
    abAppend(&ab, "\x1b[H", 3); // bring cursor back up

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cursorY - E.rowOff + 1, E.renderX - E.colOff + 1); // cursorX and cursorY are 0 indexed
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor

    write(STDOUT_FILENO, ab.buffer, ab.length);
    abFree(&ab);
}

void editorSetStatusMessage(const char *formatstr, ...) {
    va_list ap;
    va_start(ap, formatstr);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), formatstr, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

void editorMoveCursor(int c) {
    editorrow *erow = E.cursorY < E.numrows ? &E.erow[E.cursorY] : NULL;

    switch(c) {
        case ARROW_LEFT:
            if (E.cursorX != 0) {
                E.cursorX--;
            } else if (E.cursorY > 0) {
                E.cursorY--;
                E.cursorX = E.erow[E.cursorY].length;
            }
            break;
        case ARROW_DOWN:
            if (E.cursorY < E.numrows) { // allow scroll till one line past end of file
                E.cursorY++;
            }
            break;
        case ARROW_RIGHT:
            if (erow && E.cursorX < erow->length) { // allow scroll till one char past end of line
                E.cursorX++;
            } else if (erow && E.cursorX == erow->length) {
                E.cursorY++;
                E.cursorX = 0;
            }
            break;
        case ARROW_UP:
            if (E.cursorY != 0) {
                E.cursorY--;
            }
            break;
    }

    erow = E.cursorY < E.numrows ? &E.erow[E.cursorY] : NULL; // cursorY may be different, hence calculate again
    int len = erow ? erow->length : 0;
    if (E.cursorX > len) {
        E.cursorX = len;
    }
}

void editorProcessKey() {
    int c = editorReadKey();
    static int quit_times = EDITOR_QUIT_TIMES;

    switch (c) {        
        case CTRL_KEY('q') :         // exit on CTrl+Q
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING! File has unsaved changes. Press Ctrl-Q %d more time(s) to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_UP:
            editorMoveCursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cursorY = E.rowOff;
                } else if (c == PAGE_DOWN) {
                    E.cursorY = E.rowOff + E.screenrows - 1;
                    if (E.cursorY > E.numrows) E.cursorY = E.numrows;
                }

                int ii = E.screenrows;
                while (ii--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;
        case HOME_KEY:
            E.cursorX = 0;
            break;
        case END_KEY:
            if (E.cursorY < E.numrows) {
                E.cursorX = E.erow[E.cursorY].length;
            }
            break;
        case '\r':
            editorInsertNewLine();
            break;
        case BACKSPACE:
        case DEL_KEY:
        case CTRL_KEY('h'):
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        case CTRL_KEY('l'): // do nothing for escape key and Ctrl+L
        case '\x1b':
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('f'):
            editorFind();
            break;
        default:
            editorInsertChar(c);
    }

    quit_times = EDITOR_QUIT_TIMES;
}

char* editorPrompt(char *prompt, void (*callback)(char* query, int cur_key)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshTerminal();

        int c = editorReadKey();

        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            free(buf);
            if (callback) callback(buf, c);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                buflen *= 2;
                buf = realloc(buf, buflen);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
    }
}

/*** init ***/

void initEditor() {
    E.cursorX = 0;
    E.cursorY = 0;
    E.numrows = 0;
    E.erow = NULL;
    E.rowOff = 0;
    E.colOff = 0;
    E.renderX = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
    E.screenrows -= 2; // for status and message bar
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    
    editorSetStatusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-F = search");

    while (1) {
        editorRefreshTerminal();
        editorProcessKey();
    }
    return 0;
}
