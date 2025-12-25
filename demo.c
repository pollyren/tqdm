#include "tqdm.h"

// demo1: basic usage
void demo1() {
    tqdm bar;
    tqdm_init(&bar, 4e2, "Processing data", 100);

    for (int i = 0; i < 4e2; i++) {
        usleep(500); // simulate work
        tqdm_update(&bar, 1);
    }
}

// demo2: using TQDM_TRANGE macro
void demo2() {
    TQDM_TRANGE(42e3)
        usleep(2);
    TQDM_END_TRANGE;
}

// demo3: using TQDM_FOR_BEGIN and TQDM_FOR_END macros
void demo3() {
    int i;
    TQDM_FOR_BEGIN(i, 0, 6e7, "Making progress on a very important task")
        usleep(1);
    TQDM_FOR_END;
}

// demo4: long minimum interval with dynamic resizing
void demo4() {
    tqdm bar;
    tqdm_init(&bar, 1e6, "An extremely long interval", 20000); // 20 seconds interval
    for (int i = 0; i < 1e6; i++) {
        usleep(100); // simulate work
        tqdm_update(&bar, 1);
    }
}

int main() {
    demo1();
    demo2();
    // demo3();
    demo4();

    return 0;
}