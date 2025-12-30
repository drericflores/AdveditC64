/******* adveditc64u.c *****************
 * Advance Editor Commdore 64 Ultimate v2.1 (C64 / cc65)
 * (c) 2025 Dr. Eric O. Flores
 * Abacus Super C spirit port (cc65 toolchain)
 *
 * Notes:
 * - Designed for Commodore 64 Ultimate usage where "USB drive" is typically exposed
 *   through a device number (often 8/9/10/11 depending on Ultimate config).
 * - This editor uses CBM KERNAL I/O through cc65 (cbm_open/cbm_read/cbm_write).
 *
 * Improvements in v2.1:
 * 1) Visible cursor (cursor(1) + inverse block highlight)
 * 2) Display wrap at 40 columns (soft-wrap view)
 * 3) Capital letters: CASE toggle (F7) converts a-z to A-Z when enabled
 * 4) Better menu: top command bar + popup menu
 */

#include <conio.h>
#include <cbm.h>
#include <c64.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------- CONFIG ---------------------------------- */

#define SCREEN_W        40
#define SCREEN_H        25
#define TOPBAR_Y        0
#define EDIT_Y0         1
#define EDIT_ROWS       22         /* lines 1..22 */
#define STATUS_Y        23
#define HELP_Y          24

#define MAX_LINES       220
#define MAX_LINE_LEN    120        /* allow longer than 40; view wraps */
#define DEFAULT_DEVICE  8          /* Ultimate commonly uses 8; user can change */

#define LFN             1          /* logical file number */
#define SA              2          /* secondary address for sequential */

/* key codes (cc65 conio returns PETSCII-ish codes + CH_Fn constants) */
#ifndef CH_F1
#define CH_F1 133
#define CH_F2 137
#define CH_F3 134
#define CH_F4 138
#define CH_F5 135
#define CH_F6 139
#define CH_F7 136
#define CH_F8 140
#endif

/* ----------------------------- STATE ----------------------------------- */

static char lines[MAX_LINES][MAX_LINE_LEN + 1];
static unsigned int line_count = 1;

static unsigned int cx = 0;        /* cursor x in "logical line" coordinates (0..len) */
static unsigned int cy = 0;        /* current logical line index (0..line_count-1) */

static unsigned int view_top = 0;  /* top logical line index shown */
static unsigned int view_off = 0;  /* wrap row offset within a long line (in 40-col chunks) */

static unsigned char device_num = DEFAULT_DEVICE;
static char filename[32] = {0};
static unsigned char dirty = 0;

static unsigned char case_upper = 1; /* default: produce uppercase letters */

/* ----------------------------- UTIL ------------------------------------ */

static void status_msg(const char* s) {
    cclearxy(0, STATUS_Y, SCREEN_W);
    gotoxy(0, STATUS_Y);
    cputs(s);
}

static void help_msg(const char* s) {
    cclearxy(0, HELP_Y, SCREEN_W);
    gotoxy(0, HELP_Y);
    cputs(s);
}

static void draw_topbar(void) {
    unsigned char old = revers(1);
    cclearxy(0, TOPBAR_Y, SCREEN_W);
    gotoxy(0, TOPBAR_Y);
    cputs(" F1 Open  F3 Save  F5 Find  F7 Case  F8 Menu  RUN/STOP Quit ");
    revers(old);
}

static unsigned int umin(unsigned int a, unsigned int b) { return (a < b) ? a : b; }
static unsigned int umax(unsigned int a, unsigned int b) { return (a > b) ? a : b; }

static unsigned int line_len(unsigned int i) {
    return (unsigned int)strlen(lines[i]);
}

/* how many wrapped rows this logical line occupies at 40 columns (>=1) */
static unsigned int wrap_rows_for_line(unsigned int i) {
    unsigned int len = line_len(i);
    unsigned int rows = (len / SCREEN_W) + 1;
    if (rows == 0) rows = 1;
    return rows;
}

/* clamp cursor inside current line */
static void clamp_cursor(void) {
    unsigned int len = line_len(cy);
    if (cx > len) cx = len;
}

/* convert typed char respecting CASE toggle */
static unsigned char apply_case(unsigned char ch) {
    if (case_upper) {
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
    }
    return ch;
}

/* ----------------------------- FILE I/O -------------------------------- */

static void set_default_filename(void) {
    if (filename[0] == 0) {
        strcpy(filename, "ADVEDIT.TXT");
    }
}

/* Simple prompt at bottom line */
static void prompt_line(const char* label, char* out, unsigned char maxlen) {
    unsigned char i = 0;
    unsigned char ch;

    help_msg(label);
    cclearxy(0, HELP_Y, SCREEN_W);
    gotoxy(0, HELP_Y);
    cputs(label);

    /* input area */
    gotoxy((unsigned char)strlen(label), HELP_Y);
    cursor(1);

    out[0] = 0;
    while (1) {
        ch = cgetc();
        if (ch == 13) break;                 /* RETURN */
        if (ch == 27) { out[0] = 0; break; } /* ESC cancels */
        if (ch == 20 || ch == 8) {           /* DEL/BKSP */
            if (i) {
                --i;
                out[i] = 0;
                gotoxy((unsigned char)strlen(label), HELP_Y);
                cclearxy((unsigned char)strlen(label), HELP_Y, (unsigned char)(SCREEN_W - strlen(label)));
                gotoxy((unsigned char)strlen(label), HELP_Y);
                cputs(out);
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (i < (unsigned char)(maxlen - 1)) {
                ch = apply_case(ch);
                out[i++] = (char)ch;
                out[i] = 0;
                cputc((char)ch);
            }
        }
    }
    cursor(1);
    help_msg("");
}

static int load_file(void) {
    /* Load as sequential file; very simple: each line separated by \n or \r */
    unsigned int i = 0, j = 0;
    unsigned char b;

    cbm_k_close(LFN);

    if (filename[0] == 0) {
        status_msg("No filename.");
        return 0;
    }

    cbm_k_setlfs(LFN, device_num, SA);
    cbm_k_setnam(filename);

    if (cbm_k_open() != 0) {
        status_msg("Open failed. Check device/filename.");
        return 0;
    }

    /* clear buffer */
    for (i = 0; i < MAX_LINES; ++i) lines[i][0] = 0;
    line_count = 1;
    cx = cy = 0;
    view_top = 0;
    view_off = 0;

    i = 0; j = 0;
    while (1) {
        /* read one byte */
        if (cbm_k_readst() != 0) break;
        b = cbm_k_chrin();

        if (cbm_k_readst() != 0) break;

        if (b == 13 || b == 10) {
            lines[i][j] = 0;
            if (i + 1 < MAX_LINES) {
                ++i;
                j = 0;
                lines[i][0] = 0;
            }
            continue;
        }

        if (b >= 32 && b <= 126) {
            if (j < MAX_LINE_LEN) {
                lines[i][j++] = (char)b;
                lines[i][j] = 0;
            }
        }
    }

    line_count = umax(1, (unsigned int)(i + 1));
    cbm_k_close(LFN);

    dirty = 0;
    status_msg("Loaded.");
    return 1;
}

static int save_file(void) {
    unsigned int i;

    cbm_k_close(LFN);

    if (filename[0] == 0) {
        status_msg("No filename.");
        return 0;
    }

    /* On many CBM devices, writing sequential works via OPEN and CHROUT */
    cbm_k_setlfs(LFN, device_num, SA);
    cbm_k_setnam(filename);

    if (cbm_k_open() != 0) {
        status_msg("Save failed. Check device/filename.");
        return 0;
    }

    cbm_k_chkout(LFN);

    for (i = 0; i < line_count; ++i) {
        const char* s = lines[i];
        while (*s) {
            cbm_k_chrout((unsigned char)*s++);
        }
        /* newline */
        cbm_k_chrout(13);
    }

    cbm_k_clrchn();
    cbm_k_close(LFN);

    dirty = 0;
    status_msg("Saved.");
    return 1;
}

/* ----------------------------- RENDER ---------------------------------- */

static void clear_editor_area(void) {
    unsigned char y;
    for (y = EDIT_Y0; y < (unsigned char)(EDIT_Y0 + EDIT_ROWS); ++y) {
        cclearxy(0, y, SCREEN_W);
    }
}

static void render(void) {
    unsigned int l = view_top;
    unsigned char row = 0;

    draw_topbar();

    clear_editor_area();

    while (row < EDIT_ROWS && l < line_count) {
        unsigned int len = line_len(l);
        unsigned int start = 0;
        unsigned int chunk = 0;

        /* If viewing a specific wrap offset within the top line */
        if (l == view_top) {
            chunk = view_off;
        } else {
            chunk = 0;
        }

        start = chunk * SCREEN_W;

        while (row < EDIT_ROWS) {
            unsigned int k;
            unsigned int pos = start;
            gotoxy(0, (unsigned char)(EDIT_Y0 + row));

            if (pos >= len) {
                /* empty wrapped row */
                /* leave blank */
            } else {
                for (k = 0; k < SCREEN_W; ++k) {
                    unsigned int idx = pos + k;
                    if (idx < len) cputc(lines[l][idx]);
                    else break;
                }
            }

            ++row;
            start += SCREEN_W;

            if (start >= len) break; /* no more wrap rows */
        }

        ++l;
    }

    /* status line */
    {
        char s[41];
        unsigned int llen = line_len(cy);
        sprintf(s, "%s%s  Dev:%u  Ln:%u/%u  Col:%u  Len:%u  Case:%s",
                (dirty ? "*" : " "),
                (filename[0] ? filename : "UNTITLED"),
                (unsigned int)device_num,
                (unsigned int)(cy + 1),
                (unsigned int)line_count,
                (unsigned int)(cx + 1),
                (unsigned int)llen,
                (case_upper ? "UP" : "LO"));
        status_msg(s);
    }

    help_msg("ESC/F8 Menu   RETURN new line   DEL backspace   Cursor keys move");
}

/* Convert logical cursor (cx,cy) to screen coordinates considering wrap and view */
static void logical_to_screen(unsigned char* sx, unsigned char* sy) {
    unsigned int l = view_top;
    unsigned char row = 0;

    while (l < line_count && row < EDIT_ROWS) {
        unsigned int rows = wrap_rows_for_line(l);

        if (l == view_top && view_off > 0) {
            /* top line starts at view_off */
            if (rows > view_off) rows = (unsigned int)(rows - view_off);
            else rows = 1;
        }

        if (l == cy) {
            /* find cursor wrap row */
            unsigned int wrap = cx / SCREEN_W;
            unsigned int col  = cx % SCREEN_W;

            if (l == view_top) {
                if (wrap < view_off) wrap = view_off;
                wrap = (unsigned int)(wrap - view_off);
            }

            if ((unsigned int)row + wrap < EDIT_ROWS) {
                *sx = (unsigned char)col;
                *sy = (unsigned char)(EDIT_Y0 + row + wrap);
            } else {
                *sx = 0;
                *sy = (unsigned char)(EDIT_Y0 + EDIT_ROWS - 1);
            }
            return;
        }

        row = (unsigned char)(row + rows);
        ++l;
    }

    *sx = 0;
    *sy = EDIT_Y0;
}

/* draw inverse block cursor at current position */
static void draw_cursor_block(void) {
    unsigned char sx, sy;
    unsigned char old = revers(1);
    logical_to_screen(&sx, &sy);
    gotoxy(sx, sy);

    /* show character under cursor inversed; if at end-of-line, show space inversed */
    {
        unsigned int len = line_len(cy);
        if (cx < len) cputc(lines[cy][cx]);
        else cputc(' ');
    }

    revers(old);

    /* also enable hardware cursor for visibility */
    cursor(1);
}

/* Ensure cursor is within the visible window; adjust view_top/view_off */
static void ensure_cursor_visible(void) {
    unsigned int l = view_top;
    unsigned char row = 0;

    /* if cursor above top */
    if (cy < view_top) {
        view_top = cy;
        view_off = (unsigned int)(cx / SCREEN_W);
        return;
    }

    /* walk visible rows to see if cy fits */
    while (l < line_count && row < EDIT_ROWS) {
        unsigned int rows = wrap_rows_for_line(l);
        unsigned int top_trim = 0;

        if (l == view_top) top_trim = view_off;

        if (rows > top_trim) rows = (unsigned int)(rows - top_trim);
        else rows = 1;

        if (l == cy) {
            unsigned int wrap = cx / SCREEN_W;
            if (wrap < view_off) {
                view_off = wrap;
                return;
            }

            /* compute row position */
            {
                unsigned int rpos = row + (unsigned int)(wrap - view_off);
                if (rpos >= EDIT_ROWS) {
                    /* scroll down until visible */
                    while (rpos >= EDIT_ROWS) {
                        /* advance view window one wrapped row */
                        /* easiest: move view_off, then possibly view_top */
                        if (view_off + 1 < wrap_rows_for_line(view_top)) {
                            view_off++;
                        } else {
                            view_top++;
                            view_off = 0;
                        }
                        /* recompute */
                        ensure_cursor_visible();
                        return;
                    }
                }
            }
            return;
        }

        row = (unsigned char)(row + rows);
        ++l;
    }

    /* if cursor below visible area, scroll down by lines */
    if (cy >= view_top + EDIT_ROWS) {
        view_top = (unsigned int)(cy - (EDIT_ROWS / 2));
        view_off = 0;
    }
}

/* ----------------------------- EDIT OPS -------------------------------- */

static void insert_char(unsigned char ch) {
    unsigned int len = line_len(cy);
    unsigned int i;

    if (len >= MAX_LINE_LEN) return;

    /* insert at cx */
    for (i = len + 1; i > cx; --i) {
        lines[cy][i] = lines[cy][i - 1];
    }
    lines[cy][cx] = (char)ch;
    ++cx;
    dirty = 1;

    /* wrap-around behavior: if cx moved beyond 40-col boundary, keep editing naturally */
    ensure_cursor_visible();
}

static void backspace_char(void) {
    unsigned int len = line_len(cy);
    unsigned int i;

    if (cx == 0) {
        /* join with previous line */
        if (cy == 0) return;
        {
            unsigned int prev_len = line_len(cy - 1);
            if (prev_len + len + 1 > MAX_LINE_LEN) return;

            strcat(lines[cy - 1], lines[cy]);

            /* delete current line */
            for (i = cy; i + 1 < line_count; ++i) {
                strcpy(lines[i], lines[i + 1]);
            }
            if (line_count) --line_count;
            if (line_count == 0) line_count = 1;

            --cy;
            cx = prev_len;
            dirty = 1;
        }
        return;
    }

    /* delete char at cx-1 */
    for (i = cx - 1; i < len; ++i) {
        lines[cy][i] = lines[cy][i + 1];
    }
    --cx;
    dirty = 1;
    ensure_cursor_visible();
}

static void newline_split(void) {
    unsigned int len = line_len(cy);
    unsigned int i;

    if (line_count >= MAX_LINES) return;

    /* shift lines down */
    for (i = line_count; i > cy + 1; --i) {
        strcpy(lines[i], lines[i - 1]);
    }

    /* split */
    {
        char right[MAX_LINE_LEN + 1];
        strcpy(right, &lines[cy][cx]);
        lines[cy][cx] = 0;
        strcpy(lines[cy + 1], right);
    }

    ++line_count;
    ++cy;
    cx = 0;
    dirty = 1;
    ensure_cursor_visible();
}

/* ----------------------------- MENUS ----------------------------------- */

static void about_box(void) {
    clrscr();
    draw_topbar();
    gotoxy(0, 3);
    cputs("Advanced Commodore Editor");
    gotoxy(0, 5);
    cputs("Version 1  (C64 Port v2.1)");
    gotoxy(0, 7);
    cputs("by Dr. Eric O. Flores (c) 2025");
    gotoxy(0, 9);
    cputs("Abacus Super C / cc65");
    gotoxy(0, 12);
    cputs("F7 toggles CASE (UP/LO).");
    gotoxy(0, 14);
    cputs("Press any key...");
    cgetc();
}

static void popup_menu(void) {
    unsigned char ch;
    unsigned char old = revers(1);

    /* popup area */
    revers(1);
    cclearxy(4, 4, 32);
    cclearxy(4, 5, 32);
    cclearxy(4, 6, 32);
    cclearxy(4, 7, 32);
    cclearxy(4, 8, 32);
    cclearxy(4, 9, 32);

    gotoxy(6, 4); cputs("ADVANCE EDITOR MENU");
    gotoxy(6, 6); cputs("1 OPEN");
    gotoxy(6, 7); cputs("2 SAVE");
    gotoxy(6, 8); cputs("3 TOGGLE CASE");
    gotoxy(6, 9); cputs("4 ABOUT");

    revers(old);

    help_msg("Press 1-4, or ESC to close.");

    while (1) {
        ch = cgetc();
        if (ch == 27) break;
        if (ch == '1') { prompt_line("Filename: ", filename, sizeof(filename)); load_file(); break; }
        if (ch == '2') { if (filename[0]==0) prompt_line("Filename: ", filename, sizeof(filename)); save_file(); break; }
        if (ch == '3') { case_upper = (unsigned char)!case_upper; break; }
        if (ch == '4') { about_box(); break; }
    }
}

/* ----------------------------- MAIN LOOP ------------------------------- */

int main(void) {
    unsigned char ch;

    /* init */
    clrscr();
    cursor(1);
    textcolor(COLOR_WHITE);
    bgcolor(COLOR_BLUE);

    /* initialize buffer */
    lines[0][0] = 0;
    line_count = 1;
    set_default_filename();

    render();
    ensure_cursor_visible();
    draw_cursor_block();

    while (1) {
        ensure_cursor_visible();
        render();
        draw_cursor_block();

        ch = cgetc();

        /* Quit */
        if (ch == 3 /* RUN/STOP commonly maps to 3 in some setups */ || ch == 0x03) {
            /* confirm */
            help_msg("Quit? Y/N");
            ch = cgetc();
            if (ch == 'Y' || ch == 'y') break;
            continue;
        }

        /* function keys */
        if (ch == CH_F1) {
            prompt_line("Filename: ", filename, sizeof(filename));
            load_file();
            continue;
        }
        if (ch == CH_F3) {
            if (filename[0] == 0) prompt_line("Filename: ", filename, sizeof(filename));
            save_file();
            continue;
        }
        if (ch == CH_F7) {
            case_upper = (unsigned char)!case_upper;
            continue;
        }
        if (ch == CH_F8 || ch == 27) {
            popup_menu();
            continue;
        }

        /* cursor keys (PETSCII control codes often used by cc65 conio) */
        if (ch == CH_CURS_LEFT) {
            if (cx) --cx;
            else if (cy) { --cy; cx = line_len(cy); }
            continue;
        }
        if (ch == CH_CURS_RIGHT) {
            if (cx < line_len(cy)) ++cx;
            else if (cy + 1 < line_count) { ++cy; cx = 0; }
            continue;
        }
        if (ch == CH_CURS_UP) {
            if (cy) --cy;
            clamp_cursor();
            continue;
        }
        if (ch == CH_CURS_DOWN) {
            if (cy + 1 < line_count) ++cy;
            clamp_cursor();
            continue;
        }

        /* backspace / delete */
        if (ch == 20 || ch == 8) {
            backspace_char();
            continue;
        }

        /* return */
        if (ch == 13) {
            newline_split();
            continue;
        }

        /* printable */
        if (ch >= 32 && ch <= 126) {
            ch = apply_case(ch);
            insert_char(ch);
            continue;
        }
    }

    clrscr();
    cursor(1);
    return 0;
}
