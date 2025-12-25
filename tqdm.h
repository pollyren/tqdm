/**
 * @file tqdm.h
 * @brief Single header C implementation of a terminal progress bar
 *
 * MIT License
 *
 * Copyright (c) 2025 pollyren
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef TQDM_H
#define TQDM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

/**
 * @brief Feature toggle for dynamically resizing the progress bar based on terminal width.
 * Set to 1 to enable dynamic resizing with changing terminal sizes (default),
 * 0 to use a fixed width determined at initialisation.
 */
#define TQDM_DYNAMIC_RESIZE 1

#define TQDM_DEFAULT_TERMINAL_WIDTH 80
#define TQDM_MINIMUM_TERMINAL_WIDTH 10
#define TQDM_MAXIMUM_TERMINAL_WIDTH 1024
#define TQDM_MINIMUM_BAR_WIDTH 1

static const char *TQDM_BLOCKS[] = {
    " ",                // ' '
    "\xE2\x96\x8F",     // '▏'
    "\xE2\x96\x8E",     // '▎'
    "\xE2\x96\x8D",     // '▍'
    "\xE2\x96\x8C",     // '▌'
    "\xE2\x96\x8B",     // '▋'
    "\xE2\x96\x8A",     // '▊'
    "\xE2\x96\x89",     // '▉'
    "\xE2\x96\x88"      // '█'
};

#define TQDM_EMPTY_IDX  0
#define TQDM_FULL_IDX   8

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, low, high) (MIN(MAX((x), (low)), (high)))

/**
 * @brief Struct representing a tqdm progress bar
 *
 * Contains information on total steps, current progress, description,
 * timing information, and minimum update interval.
 */
typedef struct {
    /* user-facing */
    /// total number of steps
    uint64_t total_steps;
    /// current step count
    uint64_t current_steps;
    /// description string
    const char *description;
    /// minimum interval between updates (in milliseconds)
    uint32_t min_interval_ms;

    /* for internal bookkeeping */
    /// internal string to append after description ("" if no description)
    const char *_after_description;
    /// time in ms when the progress bar was started, based on CLOCK_MONOTONIC
    long _start;
    /// time in ms when the progress bar was last printed, based on CLOCK_MONOTONIC
    long _last_print;
    /// internal boolean to track if the bar has been drawn, for \r handling
    bool _drawn;
    /// file descriptor to write to (STDERR_FILENO by default)
    int _fd;
    /// terminal width
    unsigned int _term_width;
} tqdm;

#if TQDM_DYNAMIC_RESIZE
/// flag set when a SIGWINCH is received
static volatile sig_atomic_t _tqdm_winch = 0;

/// signal handler for SIGWINCH to set _tqdm_winch flag
static void _tqdm_handle_sigwinch(int signo) {
    (void)signo;
    _tqdm_winch = 1;
}

/// helper function to install the SIGWINCH handler, once program-wide
static void _tqdm_install_sigwinch(void) {
    static int installed = 0;
    if (!installed) {
        signal(SIGWINCH, _tqdm_handle_sigwinch);
        installed = 1;
    }
}
#endif // TQDM_DYNAMIC_RESIZE

static long _tqdm_timespec_to_ms(struct timespec *ts) {
    return ts->tv_sec * 1e3 + ts->tv_nsec / 1e6;
}

/// helper to get the current time in milliseconds using CLOCK_MONOTONIC
static long _tqdm_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return _tqdm_timespec_to_ms(&ts);
}

/// helper to get terminal width, defaults to TQDM_DEFAULT_TERMINAL_WIDTH if unavailable
static unsigned int _tqdm_terminal_size(tqdm *t) {
    struct winsize w;
    if (ioctl(t->_fd, TIOCGWINSZ, &w) == -1) {
        return TQDM_DEFAULT_TERMINAL_WIDTH;
    }
    return w.ws_col
            ? CLAMP(w.ws_col, TQDM_MINIMUM_TERMINAL_WIDTH, TQDM_MAXIMUM_TERMINAL_WIDTH)
            : TQDM_DEFAULT_TERMINAL_WIDTH;
}

/// helper to format time and write into buffer of size n
static void _tqdm_format_time(double milliseconds, char *buffer, size_t n) {
    int total_seconds = (int)(milliseconds / 1000 + 0.5);
    int h = total_seconds / 3600;
    int m = (total_seconds % 3600) / 60;
    int s = total_seconds % 60;

    // print hours if present
    if (h > 0) {
        snprintf(buffer, n, "%02d:%02d:%02d", h, m, s);
    } else {
        snprintf(buffer, n, "%02d:%02d", m, s);
    }
}

/**
 * @brief Initialise a tqdm progress bar
 *
 * @param t Pointer to tqdm struct to initialise
 * @param total_steps Total number of steps
 * @param description Description string to display alongside the progress bar
 */
static void tqdm_init(tqdm *t, uint64_t total_steps, const char *description, uint32_t min_interval_ms) {
    t->total_steps = total_steps;
    t->current_steps = 0;
    t->description = description;
    if (description && strlen(description) > 0) {
        t->_after_description = ": ";
    } else {
        t->_after_description = "";
    }
    t->min_interval_ms = min_interval_ms;
    t->_start = _tqdm_now_ms();
    t->_last_print = t->_start;
    t->_drawn = false;
    t->_fd = STDERR_FILENO;
    t->_term_width = _tqdm_terminal_size(t);

#if TQDM_DYNAMIC_RESIZE
    _tqdm_install_sigwinch();
#endif // TQDM_DYNAMIC_RESIZE
}

/**
 * @brief Update the tqdm progress bar by a given number of steps
 *
 * @param t Pointer to tqdm struct to update
 * @param step Number of steps to increment
 */
static void tqdm_update(tqdm *t, uint64_t step) {
    long now_ms = _tqdm_now_ms();
    long last_ms = t->_last_print;

    t->current_steps += step;

    bool force_redraw = false;

#if TQDM_DYNAMIC_RESIZE
    if (TQDM_DYNAMIC_RESIZE && _tqdm_winch) {
        _tqdm_winch = 0; // reset flag
        force_redraw = true;
    }
#endif // TQDM_DYNAMIC_RESIZE

    // if minimum interval not reached and not finished yet, skip update
    if (t->_drawn &&        // only skip if already drawn
        !force_redraw &&    // but don't skip if terminal resized in dynamic mode
        now_ms - last_ms < t->min_interval_ms &&
        t->current_steps < t->total_steps) {
        return;
    }

    double elapsed = now_ms - t->_start;
    double steps_per_ms = t->current_steps / (elapsed + 1e-9);
    double percent_complete = (double)t->current_steps / t->total_steps;
    unsigned int width = TQDM_DYNAMIC_RESIZE ? _tqdm_terminal_size(t) : t->_term_width;

    // compute an estimate of the remaining time based on current steps per ms
    double remaining = (steps_per_ms > 0 && t->current_steps < t->total_steps)
                        ? (t->total_steps - t->current_steps) / steps_per_ms
                        : 0;

    // format elapsed and remaining time strings, steps per second
    char elapsed_str[32], remaining_str[32], steps_per_ms_str[32];
    _tqdm_format_time(elapsed, elapsed_str, sizeof(elapsed_str));
    _tqdm_format_time(remaining, remaining_str, sizeof(remaining_str));
    snprintf(steps_per_ms_str, sizeof(steps_per_ms_str), "%.2f", steps_per_ms);

    // build the bar using utf-8 block characters
    char bar[1024];
    int bar_pos = 0;

    const char *orient = t->_drawn ? "\r\033[K" : "";
    bar_pos = snprintf(bar, sizeof(bar), "%s", orient);

    int before_bar_length = snprintf(
        bar + bar_pos, sizeof(bar) - bar_pos,
        "%s%s%3.0f%% |",
        t->description,
        t->_after_description,
        percent_complete * 100
    );
    bar_pos += before_bar_length;

    char after_bar[128];
    int after_bar_length = snprintf(
        after_bar, sizeof(after_bar),
        "| %llu/%llu [%s<%s, %sit/s]",
        t->current_steps, t->total_steps,
        elapsed_str,
        remaining_str,
        steps_per_ms_str
    );

    // compute length of non-bar elements, accounting for nonprintable characters
    unsigned int nonbar_width = before_bar_length + after_bar_length;
    unsigned int bar_width = MAX((int)(width - nonbar_width), TQDM_MINIMUM_BAR_WIDTH);

    // compute the number of full and partial blocks to display
    double filled_cells = percent_complete * bar_width;
    int full_cells = (int)filled_cells;
    double fractional_cell = filled_cells - full_cells;

    // fill in the bar string
    for (int i = 0; i < bar_width; i++) {
        ssize_t idx = TQDM_EMPTY_IDX;
        if (i < full_cells) {
            idx = TQDM_FULL_IDX;
        } else if (i == full_cells) {
            // if last partial block, determine which block to use
            idx = (ssize_t)(fractional_cell * 8);
        }

        const char *block = TQDM_BLOCKS[idx];
        memcpy(bar + bar_pos, block, strlen(block));
        bar_pos += strlen(block);
    }

    bar[bar_pos] = 0;

    // finally, construct the complete line and write to stderr
    char line[TQDM_MAXIMUM_TERMINAL_WIDTH];
    int written = snprintf(line, sizeof(line), "%s%s", bar, after_bar);

    if (written >= 0) {
        write(t->_fd, line, written);
    } else {
        fprintf(stderr, "tqdm: hmmm, there was an error formatting the progress bar\n");
    }

    // update last print time to now
    t->_last_print = now_ms;
    t->_drawn = true;
}

/* ==================== convenience macros ==================== */

/**
 * @brief Pair of macros to create a for loop with an integrated tqdm progress bar
 *
 * @param var Loop variable
 * @param start Starting value (inclusive)
 * @param end Ending value (exclusive)
 * @param desc Description string for progress bar
 *
 * Usage:
 * ```
 * int i;
 * TQDM_FOR_BEGIN(i, 0, 10000, "Processing")
 *     // loop body
 * TQDM_FOR_END;
 * ```
 */
#define TQDM_FOR_BEGIN(var, start, end, desc)                       \
    do {                                                            \
        tqdm _tqdm;                                                 \
        tqdm_init(&_tqdm, (end) - (start), (desc), 50);             \
        for (uint64_t var = (start); var < (end); ++var) {

#define TQDM_FOR_END                                                \
            tqdm_update(&_tqdm, 1);                                 \
        }                                                           \
    } while (0)

/**
 * @brief Pair of macros to iterate over a range[0, n) with an integrated tqdm progress bar
 *
 * @param n Number of iterations
 *
 * Usage:
 * ```
 * TQDM_TRANGE(10000)
 *     // loop body
 * TQDM_END_TRANGE;
 * ```
 */
#define TQDM_TRANGE(n)                                              \
    do {                                                            \
        tqdm _tqdm;                                                 \
        tqdm_init(&_tqdm, (n), "Processing", 50);                   \
        for (uint64_t _tqdm_i = 0; _tqdm_i < (n); ++_tqdm_i) {

#define TQDM_END_TRANGE                                             \
            tqdm_update(&_tqdm, 1);                                 \
        }                                                           \
    } while (0)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // TQDM_H