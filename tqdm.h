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

#define TQDM_MINIMUM_BAR_WIDTH 10

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

#define TQDM_FULL_BLOCK TQDM_BLOCKS[8]
#define TQDM_EMPTY_BLOCK ' '

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

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
    const char *_after_description;
    /// minimum interval between updates (in milliseconds)
    uint32_t min_interval_ms;

    /* for internal bookkeeping */
    /// time in ms when the progress bar was started, based on CLOCK_MONOTONIC
    long _start;
    /// time in ms when the progress bar was last printed, based on CLOCK_MONOTONIC
    long _last_print;
    /// internal boolean to track if the bar has been drawn, for \r handling
    bool _drawn;
    /// file descriptor to write to (STDERR_FILENO by default)
    int _fd;
} tqdm;

static long _tqdm_timespec_to_ms(struct timespec *ts) {
    return ts->tv_sec * 1e3 + ts->tv_nsec / 1e6;
}

/// helper to get the current time in milliseconds using CLOCK_MONOTONIC
static long _tqdm_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return _tqdm_timespec_to_ms(&ts);
}

/// helper to get terminal width, defaults to 80 if unavailable
static int _tqdm_terminal_size(tqdm *t) {
    struct winsize w;
    if (ioctl(t->_fd, TIOCGWINSZ, &w) == -1) {
        return 80;
    }
    return w.ws_col ? w.ws_col : 80;
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

    // if minimum interval not reached and not finished yet, skip update
    if (now_ms - last_ms < t->min_interval_ms && t->current_steps < t->total_steps) {
        return;
    }

    double elapsed = now_ms - t->_start;
    double steps_per_ms = t->current_steps / (elapsed + 1e-9);
    double percent_complete = (double)t->current_steps / t->total_steps;
    int width = _tqdm_terminal_size(t);

    // compute an estimate of the remaining time based on current steps per ms
    double remaining = (steps_per_ms > 0 && t->current_steps < t->total_steps)
                        ? (t->total_steps - t->current_steps) / steps_per_ms
                        : 0;

    // format elapsed and remaining time strings, steps per second
    char elapsed_str[32], remaining_str[32], steps_per_ms_str[32];
    _tqdm_format_time(elapsed, elapsed_str, sizeof(elapsed_str));
    _tqdm_format_time(remaining, remaining_str, sizeof(remaining_str));
    snprintf(steps_per_ms_str, sizeof(steps_per_ms_str), "%.2f", steps_per_ms);

    char *orient = t->_drawn ? "\r\033[K" : "";

    char before_bar_line[512];
    int before_bar_length = snprintf(
        before_bar_line, sizeof(before_bar_line),
        "%s%s%3.0f%% |",
        t->description,
        t->_after_description,
        percent_complete * 100
    );

    char after_bar_line[512];
    int after_bar_length = snprintf(
        after_bar_line, sizeof(after_bar_line),
        "| %llu/%llu [%s<%s, %sit/s]",
        t->current_steps, t->total_steps,
        elapsed_str,
        remaining_str,
        steps_per_ms_str
    );

    // compute length of non-bar elements, accounting for nonprintable characters
    unsigned int nonbar_width = before_bar_length + after_bar_length;
    unsigned int bar_width = width - nonbar_width;

    // build the bar using utf-8 block characters
    char bar[bar_width * 3 + 1]; // each block can be up to 3 bytes, plus NUL

    // compute the number of full and partial blocks to display
    double filled_cells = percent_complete * bar_width;
    int full_cells = (int)filled_cells;
    double fractional_cell = filled_cells - full_cells;

    // fill in the bar string
    int bar_pos = 0;
    for (int i = 0; i < bar_width; i++) {
        if (i < full_cells) {
            const char *block = TQDM_FULL_BLOCK;
            while (*block && bar_pos < (int)sizeof(bar) - 1) {
                bar[bar_pos++] = *block++;
            }
        } else if (i == full_cells && full_cells < bar_width) {
            // if last partial block, determine which block to use
            int block_idx = MIN(MAX(fractional_cell * 8, 0), 8);
            const char *block = TQDM_BLOCKS[block_idx];
            while (*block && bar_pos < (int)sizeof(bar) - 1) {
                bar[bar_pos++] = *block++;
            }
        } else {
            bar[bar_pos++] = TQDM_EMPTY_BLOCK;
        }
    }

    bar[bar_pos] = 0;

    // finally, construct the complete line and write to stderr
    char line[1024];
    int written = snprintf(
        line, sizeof(line), "%s%s%s%s", orient, before_bar_line, bar, after_bar_line
    );

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