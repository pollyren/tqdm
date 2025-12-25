# tqdm

### Overview
`tqdm` is a single-header C implementation of a terminal progress bar for Unix-based systems, inspired by Python's `tqdm`. It provides a lightweight, dependency-free way to add progress bars to track the progress of loops and long-running tasks.

### Features
- Single header, no build system required
- ANSI terminal rendering with unicode block characters
- Automatic terminal width detection without external dependencies
- Dynamic resizing support on terminal window size changes
- Elapsed time tracking and completion time estimation
- Convenience macros for common loop patterns
- Compatible with C and C++

### Usage

To use `tqdm`, simply include the `tqdm.h` header in your C/C++ project. The following demonstrates how to use `tqdm` to display a progress bar for a loop:

```c
#include "tqdm.h"

int main() {
    tqdm bar;
    tqdm_init(&bar, 6e7, "Making progress on a very important task", 50);
    for (int i = 0; i < 6e7; ++i) {
        usleep(1); // simulate work
        tqdm_update(&bar, 1);
    }
}
```

This produces the following output:
```
Making progress on a very important task:  18% |██▍          | 11039892/60000000 [00:39<02:52, 284.45it/s]
```

For convenience, `tqdm` provides macros to simplify common, though admittedly contrived, patterns. The following example uses the `TQDM_FOR_BEGIN` and `TQDM_FOR_END` macros to achieve the same effect as above:

```c
int i;
TQDM_FOR_BEGIN(i, 0, 6e7, "Making progress on a very important task")
    usleep(1);
TQDM_FOR_END;
```

To iterate over a fixed range, `tqdm` further provides the `TQDM_TRANGE` and `TQDM_END_TRANGE` macros:

```c
TQDM_TRANGE(42e3)
    usleep(100);
TQDM_END_TRANGE;
```

Note that `tqdm` prints the progress bar to standard error by default to avoid interfering with standard output. Thus, the progress bar will appear even if the program's output is redirected. This behaviour can be modified by changing the `tqdm` struct's `_fd` field.

### Terminal resizing
By default, `tqdm` automatically adjusts the progress bar width when the terminal window is resized. This feature can be disabled by setting the `TQDM_DYNAMIC_RESIZE` macro to `0` in `tqdm.h`. In scenarios where the minimum interval between updates (`min_interval_ms`) is noticeably large, dynamic resizing will take place on the subsequent call to `tqdm_update` following a terminal resize event.

By construction, the length of the bar itself is dynamically calculated based on the terminal width, the length of the description string and other fixed-width components of the progress bar display. This length is then clamped to ensure the bar is visible but does not exceed the terminal width. However, if the terminal width is insufficient to display all of these elements, the printing may appear garbled.