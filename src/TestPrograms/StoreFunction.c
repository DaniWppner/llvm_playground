#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int globals[2] = {0, 0};

struct ArgOfFunctionOne {
    int a;
    int b;
};

struct ArgOfFunctionTwo {
    int x;
    int y;
    int OutOfBoundsForOne;
};

int function_one(void *raw) {
    struct ArgOfFunctionOne *p = (struct ArgOfFunctionOne *)raw;
    globals[0] += p->a;
    return 0;
}

int function_two(void *raw) {
    struct ArgOfFunctionTwo *p = (struct ArgOfFunctionTwo *)raw;
    globals[1] += p->OutOfBoundsForOne;
    return 1;
}

typedef int (*CallFn)(void *);

struct FnDesc {
    CallFn fn;
    void *arg;
};

struct FnDescHolder {
    struct FnDesc *FnInformation;
    int extra;
};

// Creates FnDescs for both functions, returns one and leaks the other.
struct FnDesc *get_random_function_one_or_two(void) {
    struct ArgOfFunctionOne *one = malloc(sizeof(*one));
    if (one) { one->a = 4; one->b = 8; }

    struct ArgOfFunctionTwo *two = malloc(sizeof(*two));
    if (two) { two->x = 10; two->y = 3; two->OutOfBoundsForOne = 5; }

    struct FnDesc *d1 = malloc(sizeof(*d1));
    if (d1) { d1->fn = function_one; d1->arg = one; }

    struct FnDesc *d2 = malloc(sizeof(*d2));
    if (d2) { d2->fn = function_two; d2->arg = two; }

    if (time(NULL) % 2 == 0) {
        return d1;
    }
    return d2;
}

// Wraps the chosen FnDesc in a struct with more random dynamic data.
struct FnDescHolder *wrap_picked(struct FnDesc *picked) {
    struct FnDescHolder *h = malloc(sizeof(*h));
    if (!h) return NULL;
    h->FnInformation = picked;
    h->extra = time(NULL) % 100;
    return h;
}

int main(void) {
    struct FnDesc *picked = get_random_function_one_or_two();
    struct FnDescHolder *h = wrap_picked(picked);
    if (!h) return 1;

    int result = h->FnInformation->fn(h->FnInformation->arg);
    printf("chosen global variable=%d\nvalue of global variable=%d\nrandom value=%d\n", result, h->extra, globals[result]);
    free(h);
    return 0;
}