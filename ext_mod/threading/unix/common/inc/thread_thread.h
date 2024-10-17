// micropython includes
#include "py/obj.h"
#include "py/runtime.h"

#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "thread_common.h"

#ifndef __THREAD_THREAD_H__
    #define __THREAD_THREAD_H__

    typedef struct _thread_entry_args_t {
        mp_obj_dict_t *dict_locals;
        mp_obj_dict_t *dict_globals;
        size_t stack_size;
        mp_obj_t fun;
        size_t n_args;
        size_t n_kw;
        mp_obj_t args[];
    } thread_entry_args_t;

    typedef struct _mp_obj_thread_thread_t {
        mp_obj_base_t base;
        TaskHandle_t id;
        mp_obj_t ident;

        mp_obj_t name;

        thread_entry_args_t *call_args;

        int ready;              // whether the thread is ready and running
        int is_alive;
        uint8_t core_id;
        void *arg;              // thread Python args, a GC root pointer
        void *stack;            // pointer to the stack
        size_t stack_len;       // number of words in the stack
        struct _mp_obj_thread_thread_t *next;

    } mp_obj_thread_thread_t;


    extern const mp_obj_fun_builtin_fixed_t thread_start_obj;
    extern const mp_obj_fun_builtin_fixed_t thread_is_alive_obj;

    void *thread_entry_cb(mp_obj_thread_thread_t *th);
    void thread_attr_func(mp_obj_t self_in, qstr attr, mp_obj_t *dest);

#endif