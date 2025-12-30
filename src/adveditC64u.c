/*
 * Advanced Commodore Editor (C64) — Version 2.2
 *
 * Advanced Commodore Editor
 * Version 2.2
 * by Dr. Eric O. Flores (c) 2025
 * Abacus Super C / cc65 target
 *
 * Goals (v2.2):
 * - Visible caret (reverse-video cursor)
 * - Soft wrap display (40-col viewport, wrapped visual rows)
 * - Fix SHIFT/caps input by accepting PETSCII printable range (not ASCII-only)
 * - Better menus: popup boxed menus (File/Edit/Help)
 * - Designed for C64 Ultimate: save/load to IEC device (USB mapped device #)
 *
 * Build (Pop!_OS / Linux):
 *   cl65 -t c64 -O -o adv_editor_v22.prg advedit22.c
 */

#include <conio.h>
#include <cbm.h>
#include <ctype.h>
#include <string.h>

/* -----------------------------
   Screen layout (40x25)
   row 0    : menu bar
   row 1-23 : editor viewport
   row 24   : status/prompt
------------------------------ */
#define SCREEN_W        40
#define SCREEN_H        25
#define MENU_Y          0
#define VIEW_Y          1
#define VIEW_H          23
#define STATUS_Y        24

/* Text model */
#define MAX_LINES       250
#define MAX_COLS        160      /* internal line capacity */
#define CLIP_MAX        160

/* Default IEC device:
   Many setups: 8 = disk drive / Ultimate USB mapping
   If your Ultimate exposes USB as a different device, change here or via File->Device. */
#define DEFAULT_DEVICE  8

/* -----------------------------
   Editor state
------------------------------ */
static unsigned char lines[MAX_LINES][MAX_COLS + 1];
static unsigned int  line_count = 1;

static unsigned int cur_line = 0;
static unsigned int cur_col  = 0;     /* column in the logical line (0..len) */

/* View origin for soft-wrap:
   view_top_line = first logical line shown
   view_top_seg  = which wrap-segment of that line starts at top (0..segs-1)
*/
static unsigned int view_top_line = 0;
static unsigned int view_top_seg  = 0;

static unsigned char clip[CLIP_MAX + 1];
static unsigned char has_clip = 0;

/* Current filename/device */
static unsigned char current_name[32];
static unsigned char has_filename = 0;
static unsigned int  current_device = DEFAULT_DEVICE;

/* Search */
static unsigned char last_search[32];

/* -----------------------------
   Utility
------------------------------ */
static unsigned int umin(unsigned int a, unsigned int b) { return (a < b) ? a : b; }

static unsigned int line_len(unsigned int i) {
    return (unsigned int)strlen((const char*)lines[i]);
}

static unsigned int segs_for_len(unsigned int len) {
    /* soft wrap into 40-column segments */
    if (len == 0) return 1;
    return (len + (SCREEN_W - 1)) / SCREEN_W;
}

static unsigned int segs_for_line(unsigned int i) {
    return segs_for_len(line_len(i));
}

/* Clamp cursor column to current line length */
static void clamp_col(void) {
    unsigned int len = line_len(cur_line);
    if (cur_col > len) cur_col = len;
}

/* -----------------------------
   Status / prompt line
------------------------------ */
static void status_msg(const char* msg) {
    cclearxy(0, STATUS_Y, SCREEN_W);
    gotoxy(0, STATUS_Y);
    cputs(msg);
}

static void status_fileline(void) {
    char buf[41];
    unsigned int ln = cur_line + 1;
    unsigned int cc = cur_col + 1;

    /* Keep it short for 40 columns */
    if (has_filename) {
        /* "8:NAME  L:001 C:001" */
        strncpy(buf, (const char*)current_name, 16);
        buf[16] = 0;
    } else {
        strcpy(buf, "Untitled");
    }

    /* Print device + file */
    cclearxy(0, STATUS_Y, SCREEN_W);
    gotoxy(0, STATUS_Y);

    cprintf("%u:", (unsigned)current_device);
    cputs(has_filename ? (const char*)current_name : "Untitled");
    cprintf("  L:%u C:%u", (unsigned)ln, (unsigned)cc);
}

/* Simple prompt input at status line */
static void prompt_input(const char* prompt, unsigned char* out, unsigned int outmax) {
    unsigned int n = 0;
    unsigned char ch;

    if (outmax == 0) return;
    out[0] = 0;

    cclearxy(0, STATUS_Y, SCREEN_W);
    gotoxy(0, STATUS_Y);
    cputs(prompt);

    while (1) {
        ch = cgetc();

        if (ch == 13) { /* Return */
            out[n] = 0;
            return;
        }
        if (ch == 27) { /* ESC cancel */
            out[0] = 0;
            return;
        }
        if (ch == 20) { /* DEL */
            if (n > 0) {
                --n;
                out[n] = 0;
                /* erase last char visually */
                gotoxy((unsigned char)(strlen(prompt) + n), STATUS_Y);
                cputc(' ');
                gotoxy((unsigned char)(strlen(prompt) + n), STATUS_Y);
            }
            continue;
        }

        /* Accept visible PETSCII range broadly.
           Avoid control keys < 32. */
        if (ch >= 32) {
            if (n + 1 < outmax) {
                out[n++] = ch;
                out[n] = 0;
                cputc(ch);
            }
        }
    }
}

/* -----------------------------
   Soft-wrap view mapping
------------------------------ */

/* Compute caret screen position (x,y) within viewport (0..39, 0..VIEW_H-1).
   Returns 1 if visible, 0 if not visible. */
static unsigned char caret_to_screen(unsigned char* outx, unsigned char* outy) {
    unsigned int l = view_top_line;
    unsigned int seg = view_top_seg;
    unsigned int row = 0;

    while (row < VIEW_H && l < line_count) {
        unsigned int len = line_len(l);
        unsigned int segs = segs_for_len(len);

        /* each seg is one visual row */
        while (seg < segs && row < VIEW_H) {
            if (l == cur_line) {
                unsigned int caret_seg = (cur_col / SCREEN_W);
                unsigned int caret_x   = (cur_col % SCREEN_W);

                if (caret_seg == seg) {
                    *outx = (unsigned char)caret_x;
                    *outy = (unsigned char)row;
                    return 1;
                }
            }
            ++seg;
            ++row;
        }

        ++l;
        seg = 0;
    }

    return 0;
}

/* Ensure the cursor is visible in viewport by adjusting view_top_line/view_top_seg */
static void scroll_to_caret(void) {
    unsigned int l, seg;
    unsigned int caret_row = 0;
    unsigned int caret_seg = (cur_col / SCREEN_W);
    unsigned int found = 0;

    /* If caret already visible, do nothing */
    {
        unsigned char x,y;
        if (caret_to_screen(&x,&y)) return;
    }

    /* Compute caret row from start of file (virtual rows) */
    caret_row = 0;
    for (l = 0; l < line_count; ++l) {
        unsigned int segs_l = segs_for_line(l);
        if (l == cur_line) {
            caret_row += caret_seg;
            found = 1;
            break;
        }
        caret_row += segs_l;
    }
    if (!found) return;

    /* Compute current view top row (virtual rows) */
    {
        unsigned int top_row = 0;
        for (l = 0; l < line_count; ++l) {
            unsigned int segs_l = segs_for_line(l);
            if (l == view_top_line) {
                top_row += view_top_seg;
                break;
            }
            top_row += segs_l;
        }

        /* Move view so caret is near middle (like nano) */
        if (caret_row > 0) {
            unsigned int desired_top;
            if (caret_row > (VIEW_H / 2)) desired_top = caret_row - (VIEW_H / 2);
            else desired_top = 0;

            /* Convert desired_top (virtual row) -> (view_top_line, view_top_seg) */
            l = 0;
            while (l < line_count) {
                unsigned int segs_l = segs_for_line(l);
                if (desired_top < segs_l) {
                    view_top_line = l;
                    view_top_seg  = desired_top;
                    return;
                }
                desired_top -= segs_l;
                ++l;
            }
            /* fallback */
            view_top_line = 0;
            view_top_seg = 0;
        }
    }
}

/* -----------------------------
   Drawing
------------------------------ */
static void draw_menu_bar(void) {
    /* Nano-ish top hint bar */
    cclearxy(0, MENU_Y, SCREEN_W);
    gotoxy(0, MENU_Y);
    cputs("F1 File  F3 Edit  F5 Help");
}

static void draw_view(void) {
    unsigned int row = 0;
    unsigned int l = view_top_line;
    unsigned int seg = view_top_seg;

    /* Clear viewport */
    for (row = 0; row < VIEW_H; ++row) {
        cclearxy(0, (unsigned char)(VIEW_Y + row), SCREEN_W);
    }

    row = 0;
    while (row < VIEW_H && l < line_count) {
        unsigned int len = line_len(l);
        unsigned int segs = segs_for_len(len);

        while (seg < segs && row < VIEW_H) {
            unsigned int start = seg * SCREEN_W;
            unsigned int i;
            unsigned char linebuf[SCREEN_W + 1];

            /* copy 40 chars slice */
            for (i = 0; i < SCREEN_W; ++i) {
                unsigned int idx = start + i;
                if (idx < len) linebuf[i] = lines[l][idx];
                else linebuf[i] = ' ';
            }
            linebuf[SCREEN_W] = 0;

            gotoxy(0, (unsigned char)(VIEW_Y + row));
            cputs((const char*)linebuf);

            ++seg;
            ++row;
        }

        ++l;
        seg = 0;
    }

    /* Draw visible caret as reverse-video char (reliable cursor) */
    {
        unsigned char x,y;
        if (caret_to_screen(&x,&y)) {
            unsigned int len = line_len(cur_line);
            unsigned int abs = (cur_col);
            unsigned char ch = ' ';

            if (abs < len) ch = lines[cur_line][abs];

            revers(1);
            cputcxy(x, (unsigned char)(VIEW_Y + y), ch);
            revers(0);
        }
    }
}

/* -----------------------------
   File I/O (IEC via cbm_*)
   Works with 1541/Ultimate/etc. (device number configurable)
------------------------------ */

static unsigned char load_file_from_device(unsigned int dev, const unsigned char* name) {
    unsigned char buf[256];
    unsigned int pos = 0;
    unsigned int ln = 0;
    unsigned int col = 0;
    int r;

    /* reset buffer */
    for (ln = 0; ln < MAX_LINES; ++ln) lines[ln][0] = 0;
    line_count = 1;
    cur_line = 0;
    cur_col = 0;
    view_top_line = 0;
    view_top_seg = 0;

    /* open file for read: secondary address 2 is common */
    if (cbm_open(2, (unsigned char)dev, 2, (const char*)name) != 0) {
        status_msg("Open failed (device/file)");
        return 0;
    }

    ln = 0; col = 0;
    while (1) {
        r = cbm_read(2, buf, sizeof(buf));
        if (r <= 0) break;

        for (pos = 0; pos < (unsigned int)r; ++pos) {
            unsigned char ch = buf[pos];

            if (ch == 13) { /* CR = new line */
                if (ln + 1 < MAX_LINES) {
                    lines[ln][col] = 0;
                    ++ln;
                    col = 0;
                }
                continue;
            }

            if (ch == 10) continue; /* ignore LF */
            if (ch < 32) continue;  /* ignore other controls */

            if (col < MAX_COLS) {
                lines[ln][col++] = ch;
                lines[ln][col] = 0;
            }
        }
    }

    cbm_close(2);

    line_count = (ln + 1);
    if (line_count == 0) line_count = 1;

    return 1;
}

static unsigned char save_file_to_device(unsigned int dev, const unsigned char* name) {
    unsigned char cmd[48];
    unsigned int ln;
    int ok;

    /* To overwrite safely on IEC, many users do:
       - open a write channel with ",W"
       - some devices need "0:NAME,S,W" format
       We'll try a robust "0:NAME,W" style first.
    */
    strcpy((char*)cmd, "0:");
    strncat((char*)cmd, (const char*)name, 31);
    strncat((char*)cmd, ",W", 3);

    ok = cbm_open(2, (unsigned char)dev, 2, (const char*)cmd);
    if (ok != 0) {
        /* fallback: just NAME,W */
        strcpy((char*)cmd, (const char*)name);
        strncat((char*)cmd, ",W", 3);
        ok = cbm_open(2, (unsigned char)dev, 2, (const char*)cmd);
        if (ok != 0) {
            status_msg("Save failed (open write)");
            return 0;
        }
    }

    for (ln = 0; ln < line_count; ++ln) {
        unsigned int len = line_len(ln);
        if (len) cbm_write(2, lines[ln], (unsigned int)len);
        /* write CR */
        {
            unsigned char cr = 13;
            cbm_write(2, &cr, 1);
        }
    }

    cbm_close(2);
    return 1;
}

/* -----------------------------
   Editing ops
------------------------------ */

static void insert_char(unsigned char ch) {
    unsigned int len = line_len(cur_line);
    unsigned int i;

    if (len >= MAX_COLS) return;

    /* shift right */
    for (i = len + 1; i > cur_col; --i) {
        lines[cur_line][i] = lines[cur_line][i - 1];
    }
    lines[cur_line][cur_col] = ch;
    ++cur_col;
}

static void backspace_char(void) {
    unsigned int len = line_len(cur_line);
    unsigned int i;

    if (cur_col == 0) {
        /* join with previous line */
        if (cur_line == 0) return;

        {
            unsigned int prev_len = line_len(cur_line - 1);
            if (prev_len + len > MAX_COLS) return;

            /* append current to previous */
            strcat((char*)lines[cur_line - 1], (const char*)lines[cur_line]);

            /* delete current line */
            for (i = cur_line; i + 1 < line_count; ++i) {
                strcpy((char*)lines[i], (const char*)lines[i + 1]);
            }
            if (line_count > 1) --line_count;

            --cur_line;
            cur_col = prev_len;
        }
        return;
    }

    /* delete char before cursor */
    for (i = cur_col - 1; i < len; ++i) {
        lines[cur_line][i] = lines[cur_line][i + 1];
    }
    --cur_col;
}

static void split_line(void) {
    unsigned int len = line_len(cur_line);
    unsigned int i;

    if (line_count >= MAX_LINES) return;

    /* move lines down */
    for (i = line_count; i > cur_line + 1; --i) {
        strcpy((char*)lines[i], (const char*)lines[i - 1]);
    }
    ++line_count;

    /* new line gets tail */
    strcpy((char*)lines[cur_line + 1], (const char*)&lines[cur_line][cur_col]);
    lines[cur_line][cur_col] = 0;

    ++cur_line;
    cur_col = 0;
}

static void copy_line(void) {
    strncpy((char*)clip, (const char*)lines[cur_line], CLIP_MAX);
    clip[CLIP_MAX] = 0;
    has_clip = 1;
    status_msg("Copied line");
}

static void cut_line(void) {
    unsigned int i;
    copy_line();

    for (i = cur_line; i + 1 < line_count; ++i) {
        strcpy((char*)lines[i], (const char*)lines[i + 1]);
    }
    if (line_count > 1) --line_count;
    if (cur_line >= line_count) cur_line = line_count - 1;
    cur_col = 0;
    status_msg("Cut line");
}

static void paste_line(void) {
    unsigned int i;

    if (!has_clip) { status_msg("Clipboard empty"); return; }
    if (line_count >= MAX_LINES) return;

    for (i = line_count; i > cur_line + 1; --i) {
        strcpy((char*)lines[i], (const char*)lines[i - 1]);
    }
    ++line_count;
    strcpy((char*)lines[cur_line + 1], (const char*)clip);
    ++cur_line;
    cur_col = 0;
    status_msg("Pasted line");
}

/* -----------------------------
   Soft-wrap cursor movement (nano-ish)
------------------------------ */
static void move_left(void) {
    if (cur_col > 0) {
        --cur_col;
    } else if (cur_line > 0) {
        --cur_line;
        cur_col = line_len(cur_line);
    }
}

static void move_right(void) {
    unsigned int len = line_len(cur_line);
    if (cur_col < len) {
        ++cur_col;
    } else if (cur_line + 1 < line_count) {
        ++cur_line;
        cur_col = 0;
    }
}

static void move_down(void) {
    unsigned int len = line_len(cur_line);
    unsigned int segs = segs_for_len(len);
    unsigned int seg = (cur_col / SCREEN_W);
    unsigned int x   = (cur_col % SCREEN_W);

    if (seg + 1 < segs) {
        /* next visual row in same logical line */
        cur_col += SCREEN_W;
        if (cur_col > len) cur_col = len;
        return;
    }

    /* next logical line, keep x */
    if (cur_line + 1 < line_count) {
        ++cur_line;
        len = line_len(cur_line);
        cur_col = umin(x, len);
    }
}

static void move_up(void) {
    unsigned int len = line_len(cur_line);
    unsigned int seg = (cur_col / SCREEN_W);
    unsigned int x   = (cur_col % SCREEN_W);

    if (seg > 0) {
        /* prev visual row in same logical line */
        if (cur_col >= SCREEN_W) cur_col -= SCREEN_W;
        else cur_col = 0;
        return;
    }

    if (cur_line > 0) {
        --cur_line;
        len = line_len(cur_line);
        /* go to last segment row and keep x */
        {
            unsigned int segs = segs_for_len(len);
            unsigned int base = (segs > 0) ? ((segs - 1) * SCREEN_W) : 0;
            cur_col = base + x;
            if (cur_col > len) cur_col = len;
        }
    }
}

/* -----------------------------
   Search / replace
------------------------------ */
static unsigned char find_next(const unsigned char* needle) {
    unsigned int l = cur_line;
    unsigned int c = cur_col;
    unsigned int start_l = l;
    unsigned int start_c = c;

    if (needle[0] == 0) return 0;

    /* search current line from current col */
    while (1) {
        const unsigned char* hay = lines[l];
        const unsigned char* p;

        p = (const unsigned char*)strstr((const char*)(&hay[(l == cur_line) ? c : 0]), (const char*)needle);
        if (p) {
            cur_line = l;
            cur_col  = (unsigned int)(p - hay);
            return 1;
        }

        l++;
        c = 0;
        if (l >= line_count) {
            l = 0;
        }
        if (l == start_l) break;
    }

    cur_line = start_l;
    cur_col = start_c;
    return 0;
}

static void dialog_search(void) {
    unsigned char s[32];
    prompt_input("Search: ", s, sizeof(s));
    if (s[0] == 0) { status_msg(""); return; }
    strcpy((char*)last_search, (const char*)s);

    if (!find_next(s)) status_msg("Not found");
}

static void dialog_search_replace(void) {
    unsigned char s[32];
    unsigned char r[32];
    unsigned int len_s, len_r;

    prompt_input("Find: ", s, sizeof(s));
    if (s[0] == 0) { status_msg(""); return; }
    prompt_input("Repl: ", r, sizeof(r));
    /* allow empty replacement */

    len_s = (unsigned int)strlen((const char*)s);
    len_r = (unsigned int)strlen((const char*)r);

    if (find_next(s)) {
        /* simple in-line replace at current match */
        unsigned int len = line_len(cur_line);
        unsigned int i;

        if (len - len_s + len_r > MAX_COLS) {
            status_msg("Replace too long");
            return;
        }

        /* delete old */
        for (i = cur_col; i + len_s <= len; ++i) {
            lines[cur_line][i] = lines[cur_line][i + len_s];
        }

        /* make room if needed */
        if (len_r > 0) {
            unsigned int newlen = (unsigned int)strlen((const char*)lines[cur_line]);
            /* shift right */
            for (i = newlen + len_r; i > cur_col + len_r - 1; --i) {
                lines[cur_line][i] = lines[cur_line][i - len_r];
            }
            /* insert */
            for (i = 0; i < len_r; ++i) {
                lines[cur_line][cur_col + i] = r[i];
            }
        }

        status_msg("Replaced");
    } else {
        status_msg("Not found");
    }
}

/* -----------------------------
   Popup menu UI
------------------------------ */
static void draw_box(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    unsigned char i;

    /* simple ASCII box */
    cputcxy(x, y, '+');
    for (i = 1; i < w - 1; ++i) cputcxy(x + i, y, '-');
    cputcxy(x + w - 1, y, '+');

    for (i = 1; i < h - 1; ++i) {
        cputcxy(x, y + i, '|');
        cclearxy(x + 1, y + i, w - 2);
        cputcxy(x + w - 1, y + i, '|');
    }

    cputcxy(x, y + h - 1, '+');
    for (i = 1; i < w - 1; ++i) cputcxy(x + i, y + h - 1, '-');
    cputcxy(x + w - 1, y + h - 1, '+');
}

static unsigned char menu_file(void) {
    unsigned char k;
    unsigned char name[32];
    unsigned char devs[4];
    unsigned int dev;

    draw_box(3, 3, 34, 12);
    gotoxy(5, 4);  cputs("FILE");
    gotoxy(5, 6);  cputs("O Open");
    gotoxy(5, 7);  cputs("S Save");
    gotoxy(5, 8);  cputs("A Save As");
    gotoxy(5, 9);  cputs("D Device #");
    gotoxy(5,10);  cputs("C Close");
    gotoxy(5,11);  cputs("Q Quit");
    gotoxy(5,13);  cputs("ESC cancel");

    while (1) {
        k = cgetc();
        if (k == 27) return 0;

        k = (unsigned char)toupper(k);

        if (k == 'O') {
            prompt_input("Open name: ", name, sizeof(name));
            if (name[0]) {
                if (load_file_from_device(current_device, name)) {
                    strncpy((char*)current_name, (const char*)name, sizeof(current_name) - 1);
                    current_name[sizeof(current_name) - 1] = 0;
                    has_filename = 1;
                    status_msg("Loaded");
                }
            }
            return 0;
        }

        if (k == 'S') {
            if (!has_filename) {
                prompt_input("Save as: ", name, sizeof(name));
                if (name[0]) {
                    if (save_file_to_device(current_device, name)) {
                        strncpy((char*)current_name, (const char*)name, sizeof(current_name) - 1);
                        current_name[sizeof(current_name) - 1] = 0;
                        has_filename = 1;
                        status_msg("Saved");
                    }
                }
            } else {
                if (save_file_to_device(current_device, current_name)) status_msg("Saved");
            }
            return 0;
        }

        if (k == 'A') {
            prompt_input("Save as: ", name, sizeof(name));
            if (name[0]) {
                if (save_file_to_device(current_device, name)) {
                    strncpy((char*)current_name, (const char*)name, sizeof(current_name) - 1);
                    current_name[sizeof(current_name) - 1] = 0;
                    has_filename = 1;
                    status_msg("Saved");
                }
            }
            return 0;
        }

        if (k == 'D') {
            prompt_input("Device #: ", devs, sizeof(devs));
            dev = DEFAULT_DEVICE;
            if (isdigit((unsigned char)devs[0])) {
                dev = (unsigned int)(devs[0] - '0');
                if (isdigit((unsigned char)devs[1])) dev = (unsigned int)(dev * 10 + (devs[1] - '0'));
            }
            current_device = dev;
            status_msg("Device set");
            return 0;
        }

        if (k == 'C') {
            lines[0][0] = 0;
            line_count = 1;
            cur_line = 0;
            cur_col = 0;
            view_top_line = 0;
            view_top_seg = 0;
            has_filename = 0;
            current_name[0] = 0;
            status_msg("Closed");
            return 0;
        }

        if (k == 'Q') {
            return 1;
        }
    }
}

static void menu_edit(void) {
    unsigned char k;

    draw_box(3, 3, 34, 12);
    gotoxy(5, 4);  cputs("EDIT");
    gotoxy(5, 6);  cputs("Y Copy line");
    gotoxy(5, 7);  cputs("X Cut line");
    gotoxy(5, 8);  cputs("P Paste line");
    gotoxy(5, 9);  cputs("F Search");
    gotoxy(5,10);  cputs("R Search/Replace");
    gotoxy(5,13);  cputs("ESC cancel");

    while (1) {
        k = cgetc();
        if (k == 27) return;
        k = (unsigned char)toupper(k);

        if (k == 'Y') { copy_line(); return; }
        if (k == 'X') { cut_line(); return; }
        if (k == 'P') { paste_line(); return; }
        if (k == 'F') { dialog_search(); return; }
        if (k == 'R') { dialog_search_replace(); return; }
    }
}

static void menu_help(void) {
    unsigned char k;

    draw_box(2, 4, 36, 10);
    gotoxy(4, 5); cputs("ABOUT");
    gotoxy(4, 7); cputs("Advanced Commodore Editor");
    gotoxy(4, 8); cputs("Version 2.2");
    gotoxy(4, 9); cputs("by Dr. Eric O. Flores (c) 2025");
    gotoxy(4,10); cputs("Abacus Super C / cc65");
    gotoxy(4,12); cputs("ESC to return");

    while (1) {
        k = cgetc();
        if (k == 27) return;
    }
}

/* Function keys on C64 (cc65 conio) */
#define KEY_F1  133
#define KEY_F3  134
#define KEY_F5  135

static void handle_key(unsigned char k, unsigned char* quit_flag) {
    /* Function keys => menus */
    if (k == KEY_F1) { if (menu_file()) *quit_flag = 1; return; }
    if (k == KEY_F3) { menu_edit(); return; }
    if (k == KEY_F5) { menu_help(); return; }

    /* Return = split line */
    if (k == 13) { split_line(); return; }

    /* DEL key (backspace) = 20 */
    if (k == 20) { backspace_char(); return; }

    /* Arrow keys (typical C64 codes) */
    if (k == 145) { move_up(); return; }    /* Up */
    if (k == 17)  { move_down(); return; }  /* Down */
    if (k == 157) { move_left(); return; }  /* Left */
    if (k == 29)  { move_right(); return; } /* Right */

    /* Printable PETSCII:
       accept >= 32, but ignore a few known special codes if you want.
       This fixes SHIFT/caps because C64 returns PETSCII beyond ASCII 126. */
    if (k >= 32) {
        insert_char(k);
        return;
    }
}

/* -----------------------------
   Entry
------------------------------ */
int main(void) {
    unsigned char quit_flag = 0;
    unsigned char k;

    /* init buffer */
    lines[0][0] = 0;
    current_name[0] = 0;
    has_filename = 0;
    current_device = DEFAULT_DEVICE;
    last_search[0] = 0;

    clrscr();
    bgcolor(COLOR_BLUE);
    textcolor(COLOR_WHITE);
    bordercolor(COLOR_BLUE);

    draw_menu_bar();
    draw_view();
    status_fileline();

    while (!quit_flag) {
        scroll_to_caret();
        draw_menu_bar();
        draw_view();
        status_fileline();

        k = cgetc();
        handle_key(k, &quit_flag);
    }

    clrscr();
    return 0;
}
