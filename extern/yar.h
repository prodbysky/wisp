/* yar - dynamic arrays in C - public domain Nicholas Rixson 2025
 *
 * https://github.com/segcore/yar
 *
 * Licence: see end of file

 Sample usage:
    #define YAR_IMPLEMENTATION
    #include "yar.h"

    int main() {
        // struct { double *items; size_t count; size_t capacity; } numbers = {0};
        yar(double) numbers = {0};
        *yar_append(&numbers) = 3.14159;
        *yar_append(&numbers) = 2.71828;
        *yar_append(&numbers) = 1.61803;

        for(size_t i = 0; i < numbers.count; i++) {
            printf("%f\n", numbers.items[i]);
        }

        yar_free(&numbers);
    }
 */
#ifndef YAR_H
#define YAR_H

#include <stddef.h> // size_t
#include <string.h> // strlen

/*
 * yar(type) - Declare a new basic dynamic array
 *
 * yar_append(array) - Add a new item at the end of the array, and return a pointer to it
 *
 * yar_reserve(array, extra) - Reserve space for `extra` count of items
 *
 * yar_append_many(array, data, num) - Append a copy of existing data
 *
 * yar_append_cstr(array, data) - Append a C string (nul-terminated char array)
 *
 * yar_insert(array, index, num) - Insert items somewhere within the array. Moves items to higher indexes as required. Returns &array[index]
 *
 * yar_remove(array, index, num) - Remove items from somewhere within the array. Moves items to lower indexes as required.
 *
 * yar_reset(array) - Reset the count of elements to 0, to re-use the memory. Does not free the memory.
 *
 * yar_init(array) - Set items, count, and capacity to 0. Can usually be avoided with <declaration> = {0};
 *
 * yar_free(array) - Free items memory, and set the items, count, and capacity to 0.
 */

#define yar(type)   struct { type *items; size_t count; size_t capacity; }
#define yar_append(array)   ((_yar_append((void**)&(array)->items, &(array)->count, &(array)->capacity, sizeof((array)->items[0])) ? \
                              &(array)->items[(array)->count - 1] : NULL))
#define yar_reserve(array, extra)       ((_yar_reserve((void**)&(array)->items, &(array)->count, &(array)->capacity, sizeof((array)->items[0]), (extra)) ? \
                                          &(array)->items[(array)->count] : NULL))
#define yar_append_many(array, data, num)   ((_yar_append_many((void**)&(array)->items, &(array)->count, &(array)->capacity, sizeof((array)->items[0]), 1 ? (data) : ((array)->items), (num)) ))
#define yar_append_cstr(array, data)        yar_append_many(array, data, strlen(data))
#define yar_insert(array, index, num)       ((_yar_insert((void**)&(array)->items, &(array)->count, &(array)->capacity, sizeof((array)->items[0]), index, num) ))
#define yar_remove(array, index, num)       ((_yar_remove((void**)&(array)->items, &(array)->count, sizeof((array)->items[0]), index, num) ))
#define yar_reset(array)    (((array)->count = 0))
#define yar_init(array)     ((array)->items = NULL, (array)->count = 0, (array)->capacity = 0)
#define yar_free(array)     ((_yar_free((array)->items)), (array)->items = NULL, (array)->count = 0, (array)->capacity = 0)

#ifndef YARAPI
    #define YARAPI // nothing; overridable if needed.
#endif

#ifdef __cplusplus
    extern "C" {
#endif

// Implementation functions
YARAPI void* _yar_append(void** items_pointer, size_t* count, size_t* capacity, size_t item_size);
YARAPI void* _yar_append_many(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, void* data, size_t extra);
YARAPI void* _yar_reserve(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, size_t extra);
YARAPI void* _yar_insert(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, size_t index, size_t extra);
YARAPI void* _yar_remove(void** items_pointer, size_t* count, size_t item_size, size_t index, size_t remove);
YARAPI void* _yar_realloc(void* p, size_t new_size);
YARAPI void _yar_free(void* p);

#ifdef __cplusplus
    }
#endif

#endif // YAR_H

#if defined(YAR_IMPLEMENTATION)

#ifndef YAR_MIN_CAP
  #define YAR_MIN_CAP 16
#endif

#ifndef YAR_REALLOC
  #define YAR_REALLOC realloc
#endif

#ifndef YAR_FREE
  #define YAR_FREE free
#endif

#include <string.h> // mem* functions
YARAPI void* _yar_append(void** items_pointer, size_t* count, size_t* capacity, size_t item_size)
{
    void* result = _yar_reserve(items_pointer, count, capacity, item_size, 1);
    if (result != NULL) *count += 1;
    return result;
}

YARAPI void* _yar_append_many(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, void* data, size_t extra)
{
    void* result = _yar_reserve(items_pointer, count, capacity, item_size, extra);
    if (result != NULL) {
        memcpy(result, data, item_size * extra);
        *count += extra;
    }
    return result;
}

YARAPI void* _yar_reserve(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, size_t extra)
{
    char* items = *items_pointer;
    size_t newcount = *count + extra;
    if (newcount > *capacity) {
        size_t newcap = (*capacity < YAR_MIN_CAP) ? YAR_MIN_CAP : *capacity * 8 / 5;
        if (newcap < newcount) newcap = newcount;
        void* next = _yar_realloc(items, newcap * item_size);
        if (next == NULL) return NULL;
        items = next;
        *items_pointer = next;
        *capacity = newcap;
    }
    void* result = items + (*count * item_size);
    if (extra && result) memset(result, 0, item_size * extra);
    return result;
}

YARAPI void* _yar_insert(void** items_pointer, size_t* count, size_t* capacity, size_t item_size, size_t index, size_t extra)
{
    void* next = _yar_reserve(items_pointer, count, capacity, item_size, extra);
    if(next == NULL) return NULL;

    char* items = *items_pointer;
    if (index < *count)
    {
        memmove(&items[item_size * (index + extra)], &items[item_size * index], (*count - index) * item_size);
        memset(&items[item_size * index], 0, extra * item_size);
    }
    *count += extra;
    return items + index * item_size;
}

YARAPI void* _yar_remove(void** items_pointer, size_t* count, size_t item_size, size_t index, size_t remove)
{
    if(remove >= *count) {
        *count = 0;
        return *items_pointer;
    }
    if (index >= *count) {
        return *items_pointer;
    }
    char* items = *items_pointer;
    memmove(&items[item_size * index], &items[item_size * (index + remove)], item_size * (*count - (index + remove)));
    *count -= remove;
    return items + item_size * index;
}

YARAPI void* _yar_realloc(void* p, size_t new_size)
{
    // Declaration, so we can call it if the definition is overridden
    extern void* YAR_REALLOC(void *ptr, size_t size);
    return YAR_REALLOC(p, new_size);
}

YARAPI void _yar_free(void* p)
{
    extern void YAR_FREE(void *ptr);
    YAR_FREE(p);
}

#endif // YAR_IMPLEMENTATION
/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License

Copyright (c) 2025 Nicholas Rixson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
