// Copyright (c) 1981-86 Robert A. Koeneke
// Copyright (c) 1987-94 James E. Wilson
//
// This work is free software released under the GNU General Public License
// version 2.0, and comes with ABSOLUTELY NO WARRANTY.
//
// See LICENSE and AUTHORS for more information.

// Terminal I/O code, uses the curses package

#include <cstdlib>
#include "headers.h"
#include "curses.h"

static bool curses_on = false;

// Spare window for saving the screen. -CJS-
static WINDOW *save_screen;

int eof_flag = 0;             // Is used to signal EOF/HANGUP condition
bool panic_save = false;      // True if playing from a panic save

// Set up the terminal into a suitable state -MRC-
static void moriaTerminalInitialize() {
    raw();                 // <curses.h> disable control characters. I.e. Ctrl-C does not work!
    // cbreak();           // <curses.h> use raw() instead as it disables Ctrl chars
    noecho();              // <curses.h> do not echo typed characters
    nonl();                // <curses.h> disable translation return/newline for detection of return key
    keypad(stdscr, false); // <curses.h> disable keypad input as we handle that ourselves
    // curs_set(0);        // <curses.h> sets the appearance of the cursor based on the value of visibility

#ifdef __APPLE__
    set_escdelay(50);      // <curses.h> default delay on macOS is 1 second, let's do something about that!
#endif

    curses_on = true;
}

// initializes the terminal / curses routines
bool terminalInitialize() {
    initscr();

    // Check we have enough screen. -CJS-
    if (LINES < 24 || COLS < 80) {
        (void) printf("Screen too small for moria.\n");
        return false;
    }

    save_screen = newwin(0, 0, 0, 0);
    if (save_screen == nullptr) {
        (void) printf("Out of memory in starting up curses.\n");
        return false;
    }

    moriaTerminalInitialize();

    (void) clear();
    (void) refresh();

    return true;
}

// Put the terminal in the original mode. -CJS-
void terminalRestore() {
    if (!curses_on) {
        return;
    }

    // Dump any remaining buffer
    putQIO();

    // this moves curses to bottom right corner
    int y = 0;
    int x = 0;
    getyx(stdscr, y, x);
    mvcur(y, x, LINES - 1, 0);

    // exit curses
    endwin();
    (void) fflush(stdout);

    curses_on = false;
}

void terminalSaveScreen() {
    overwrite(stdscr, save_screen);
}

void terminalRestoreScreen() {
    overwrite(save_screen, stdscr);
    touchwin(stdscr);
}

void terminalBellSound() {
    putQIO();

    // The player can turn off beeps if they find them annoying.
    if (config::options::error_beep_sound) {
        (void) write(1, "\007", 1);
    }
}

// Dump the IO buffer to terminal -RAK-
void putQIO() {
    // Let inventoryExecuteCommand() know something has changed.
    screen_has_changed = true;

    (void) refresh();
}

// Flush the buffer -RAK-
void flushInputBuffer() {
    if (eof_flag != 0) {
        return;
    }

    while (checkForNonBlockingKeyPress(0));
}

// Clears screen
void clearScreen() {
    if (message_ready_to_print) {
        printMessage(CNIL);
    }
    (void) clear();
}

void clearToBottom(int row) {
    (void) move(row, 0);
    clrtobot();
}

// move cursor to a given y, x position
void moveCursor(Coord_t coords) {
    (void) move(coords.y, coords.x);
}

void addChar(char ch, Coord_t coords) {
    if (mvaddch(coords.y, coords.x, ch) == ERR) {
        abort();
    }
}

// Dump IO to buffer -RAK-
void putString(const char *out_str, Coord_t coords) {
    // truncate the string, to make sure that it won't go past right edge of screen.
    if (coords.x > 79) {
        coords.x = 79;
    }

    vtype_t str = {'\0'};
    (void) strncpy(str, out_str, (size_t) (79 - coords.x));
    str[79 - coords.x] = '\0';

    if (mvaddstr(coords.y, coords.x, str) == ERR) {
        abort();
    }
}

// Outputs a line to a given y, x position -RAK-
void putStringClearToEOL(const std::string &str, Coord_t coords) {
    if (coords.y == MSG_LINE && message_ready_to_print) {
        printMessage(CNIL);
    }

    (void) move(coords.y, coords.x);
    clrtoeol();
    putString(str.c_str(), coords);
}

// Clears given line of text -RAK-
void eraseLine(Coord_t coords) {
    if (coords.y == MSG_LINE && message_ready_to_print) {
        printMessage(CNIL);
    }

    (void) move(coords.y, coords.x);
    clrtoeol();
}

// Moves the cursor to a given interpolated y, x position -RAK-
void panelMoveCursor(Coord_t coords) {
    // Real coords convert to screen positions
    coords.y -= dg.panel.row_prt;
    coords.x -= dg.panel.col_prt;

    if (move(coords.y, coords.x) == ERR) {
        abort();
    }
}

// Outputs a char to a given interpolated y, x position -RAK-
// sign bit of a character used to indicate standout mode. -CJS
void panelPutTile(char ch, Coord_t coords) {
    // Real coords convert to screen positions
    coords.y -= dg.panel.row_prt;
    coords.x -= dg.panel.col_prt;

    if (mvaddch(coords.y, coords.x, ch) == ERR) {
        abort();
    }
}

static Coord_t currentCursorPosition() {
    int y, x;
    getyx(stdscr, y, x);
    return Coord_t{y, x};
}

// messageLinePrintMessage will print a line of text to the message line (0,0).
// first clearing the line of any text!
void messageLinePrintMessage(std::string message) {
    // save current cursor position
    Coord_t coords = currentCursorPosition();

    // move to beginning of message line, and clear it
    move(0, 0);
    clrtoeol();

    // truncate message if it's too long!
    message.resize(79);

    addstr(message.c_str());

    // restore cursor to old position
    move(coords.y, coords.x);
}

// deleteMessageLine will delete all text from the message line (0,0).
// The current cursor position will be maintained.
void messageLineClear() {
    // save current cursor position
    Coord_t coords = currentCursorPosition();

    // move to beginning of message line, and clear it
    move(0, 0);
    clrtoeol();

    // restore cursor to old position
    move(coords.y, coords.x);
}

// Outputs message to top line of screen
// These messages are kept for later reference.
void printMessage(const char *msg) {
    int new_len = 0;
    int old_len = 0;
    bool combine_messages = false;

    if (message_ready_to_print) {
        old_len = (int) strlen(messages[last_message_id]) + 1;

        // If the new message and the old message are short enough,
        // we want display them together on the same line.  So we
        // don't flush the old message in this case.

        if (msg != nullptr) {
            new_len = (int) strlen(msg);
        } else {
            new_len = 0;
        }

        if ((msg == nullptr) || new_len + old_len + 2 >= 73) {
            // ensure that the complete -more- message is visible.
            if (old_len > 73) {
                old_len = 73;
            }

            putString(" -more-", Coord_t{MSG_LINE, old_len});

            char in_char;
            do {
                in_char = getKeyInput();
            } while ((in_char != ' ') && (in_char != ESCAPE) && (in_char != '\n') && (in_char != '\r'));
        } else {
            combine_messages = true;
        }
    }

    if (!combine_messages) {
        (void) move(MSG_LINE, 0);
        clrtoeol();
    }

    // Make the null string a special case. -CJS-

    if (msg == nullptr) {
        message_ready_to_print = false;
        return;
    }

    game.command_count = 0;
    message_ready_to_print = true;

    // If the new message and the old message are short enough,
    // display them on the same line.

    if (combine_messages) {
        putString(msg, Coord_t{MSG_LINE, old_len + 2});
        strcat(messages[last_message_id], "  ");
        strcat(messages[last_message_id], msg);
    } else {
        messageLinePrintMessage(msg);
        last_message_id++;

        if (last_message_id >= MESSAGE_HISTORY_SIZE) {
            last_message_id = 0;
        }

        (void) strncpy(messages[last_message_id], msg, MORIA_MESSAGE_SIZE);
        messages[last_message_id][MORIA_MESSAGE_SIZE - 1] = '\0';
    }
}

// Print a message so as not to interrupt a counted command. -CJS-
void printMessageNoCommandInterrupt(const std::string &msg) {
    // Save command count value
    int i = game.command_count;

    printMessage(msg.c_str());

    // Restore count value
    game.command_count = i;
}

// Returns a single character input from the terminal. -CJS-
//
// This silently consumes ^R to redraw the screen and reset the
// terminal, so that this operation can always be performed at
// any input prompt. getKeyInput() never returns ^R.
char getKeyInput() {
    putQIO();         // Dump IO buffer
    game.command_count = 0; // Just to be safe -CJS-

    while (true) {
        int ch = getch();

        // some machines may not sign extend.
        if (ch == EOF) {
            // avoid infinite loops while trying to call getKeyInput() for a -more- prompt.
            message_ready_to_print = false;

            eof_flag++;

            (void) refresh();

            if (!game.character_generated || game.character_saved) {
                endGame();
            }

            playerDisturb(1, 0);

            if (eof_flag > 100) {
                // just in case, to make sure that the process eventually dies
                panic_save = true;

                (void) strcpy(game.character_died_from, "(end of input: panic saved)");
                if (!saveGame()) {
                    (void) strcpy(game.character_died_from, "panic: unexpected eof");
                    game.character_is_dead = true;
                }
                endGame();
            }
            return ESCAPE;
        }

        if (ch != CTRL_KEY('R')) {
            return (char) ch;
        }

        (void) wrefresh(curscr);
        moriaTerminalInitialize();
    }
}

// Prompts (optional) and returns ord value of input char
// Function returns false if <ESCAPE> is input
bool getCommand(const std::string &prompt, char &command) {
    if (!prompt.empty()) {
        putStringClearToEOL(prompt, Coord_t{0, 0});
    }
    command = getKeyInput();

    messageLineClear();

    return command != ESCAPE;
}

// Gets a string terminated by <RETURN>
// Function returns false if <ESCAPE> is input
bool getStringInput(char *in_str, Coord_t coords, int slen) {
    (void) move(coords.y, coords.x);

    for (int i = slen; i > 0; i--) {
        (void) addch(' ');
    }

    (void) move(coords.y, coords.x);

    int start_col = coords.x;
    int end_col = coords.x + slen - 1;

    if (end_col > 79) {
        end_col = 79;
    }

    char *p = in_str;

    bool flag = false;
    bool aborted = false;

    while (!flag && !aborted) {
        int key = getKeyInput();
        switch (key) {
            case ESCAPE:
                aborted = true;
                break;
            case CTRL_KEY('J'):
            case CTRL_KEY('M'):
                flag = true;
                break;
            case DELETE:
            case CTRL_KEY('H'):
                if (coords.x > start_col) {
                    coords.x--;
                    putString(" ", coords);
                    moveCursor(coords);
                    *--p = '\0';
                }
                break;
            default:
                if ((isprint(key) == 0) || coords.x > end_col) {
                    terminalBellSound();
                } else {
                    mvaddch(coords.y, coords.x, (char) key);
                    *p++ = (char) key;
                    coords.x++;
                }
                break;
        }
    }

    if (aborted) {
        return false;
    }

    // Remove trailing blanks
    while (p > in_str && p[-1] == ' ') {
        p--;
    }
    *p = '\0';

    return true;
}

// Used to verify a choice - user gets the chance to abort choice. -CJS-
bool getInputConfirmation(const std::string &prompt) {
    putStringClearToEOL(prompt, Coord_t{0, 0});

    int y, x;
    getyx(stdscr, y, x);

    if (x > 73) {
        (void) move(0, 73);
    } else if (y != 0) {
        // use `y` to prevent compiler warning.
    }

    (void) addstr(" [y/n]");

    char input = ' ';
    while (input == ' ') {
        input = getKeyInput();
    }

    messageLineClear();

    return (input == 'Y' || input == 'y');
}

// Pauses for user response before returning -RAK-
void waitForContinueKey(int line_number) {
    putStringClearToEOL("[ press any key to continue ]", Coord_t{line_number, 23});
    (void) getKeyInput();
    eraseLine(Coord_t{line_number, 0});
}

// Provides for a timeout on input. Does a non-blocking read, consuming the data if
// any, and then returns 1 if data was read, zero otherwise.
//
// Porting:
//
// In systems without the select call, but with a sleep for fractional numbers of
// seconds, one could sleep for the time and then check for input.
//
// In systems which can only sleep for whole number of seconds, you might sleep by
// writing a lot of nulls to the terminal, and waiting for them to drain, or you
// might hack a static accumulation of times to wait. When the accumulation reaches
// a certain point, sleep for a second. There would need to be a way of resetting
// the count, with a call made for commands like run or rest.
bool checkForNonBlockingKeyPress(int microseconds) {
    (void) microseconds;

    // Ugly non-blocking read...Ugh! -MRC-
    timeout(8);
    int result = getch();
    timeout(-1);

    return result > 0;
}

// Find a default user name from the system.
void getDefaultPlayerName(char *buffer) {
    // Gotta have some name
    const char *defaultName = "X";

    unsigned int bufCharCount = PLAYER_NAME_SIZE;

    if (!GetUserName(buffer, &bufCharCount)) {
        (void)strcpy(buffer, defaultName);
    }
}

// Check user permissions on Unix based systems,
// or if on Windows just return. -MRC-
bool checkFilePermissions() {
    return true;
}
