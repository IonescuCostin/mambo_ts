/*
  This file is part of MAMBO, a low-overhead dynamic binary modification tool:
    https://github.com/beehive-lab/mambo

  Copyright 2017-2020 The University of Manchester

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include "../../plugins.h"

#ifdef PLUGINS_NEW


//------------------------------------------------------------------------------
// Debug printing
//------------------------------------------------------------------------------

// FILE *fp;
//    char ch;
// char *filename = "log.txt";
//    char *content = "This text is appeneded later to the file, using C programming.";




//------------------------------------------------------------------------------
// Vector Clock operations
//------------------------------------------------------------------------------



pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void vc_copy(mambo_ht_t* into, mambo_ht_t* from){
  pthread_mutex_lock(&mutex);

  // Resize the vector if necessary 
  if(into->size != from->size){
    free(into);
    into = malloc(sizeof(mambo_ht_t));
    
    int ret = mambo_ht_init(into, from->size, from->index_shift, from->fill_factor, from->allow_resize);
    assert(ret == MAMBO_SUCCESS);
  }

  for(int index = 0; index < from->size; index++){
    into->entries[index].key = from->entries[index].key;
    into->entries[index].value = from->entries[index].value;
  }

  pthread_mutex_unlock(&mutex);

}// vc_copy

void vc_join(mambo_ht_t* into, mambo_ht_t* from){

  pthread_mutex_lock(&mutex);
  
  for(int index = 0; index < from->size; index++){
    if(from->entries[index].key != 0){

      uintptr_t ret_clock;
      int ret = mambo_ht_get(into, from->entries[index].key, &ret_clock);

      if(ret != MAMBO_SUCCESS)
        ret = mambo_ht_add(into, from->entries[index].key, from->entries[index].value);
      else if(ret_clock < from->entries[index].value)
        ret = mambo_ht_add(into, from->entries[index].key, from->entries[index].value);
      
    }
  }// for

  pthread_mutex_unlock(&mutex);

}// vc_join

void vc_increment(mambo_ht_t* vector_clock, int thread_id){

  pthread_mutex_lock(&mutex);

  // Get the current value of the lamport clock for this thread
  uintptr_t l_clock;
  int ret = mambo_ht_get(vector_clock, thread_id, &l_clock);
  assert(ret == 0); 

  //Store the incremented value
  ret = mambo_ht_add(vector_clock, thread_id, l_clock + 1);
  assert(ret == 0);

  pthread_mutex_unlock(&mutex);

}// vc_incrememnt

void vc_print(mambo_ht_t* vector_clock){

  pthread_mutex_lock(&mutex);

  printf("================ VC ================\n");
  for(int index = 0; index < vector_clock->size; index++){
    if(vector_clock->entries[index].key != 0)
      printf("Entry %d:   K-%lX, V-%lu\n", index, vector_clock->entries[index].key, vector_clock->entries[index].value);
  }
  printf("\n");

  pthread_mutex_unlock(&mutex);
}// vc_print





//------------------------------------------------------------------------------
// Thread Handlers
//------------------------------------------------------------------------------





int drd_pre_thread_handler(mambo_context *ctx) {

  if(ctx->thread_data->parent_thread == NULL){// If this is the main thread

    // Initalise the vector clock for the current thread
    mambo_ht_t* thread_vc = mambo_alloc(ctx, sizeof(mambo_ht_t));
    int ret = mambo_ht_init(thread_vc, 64, 0, 70, true);
    assert(ret == MAMBO_SUCCESS);


    // Insert the ID of the current thread in its VC
    ret = mambo_ht_add(thread_vc, mambo_get_thread_id(ctx), 1);
    assert(ret == 0);

    // Set the VC as thread specific data
    mambo_set_thread_plugin_data(ctx, thread_vc);

  }else{// If the thread was spawned by another thread

    // Retrieve the VC of the parent thread
    unsigned int p_id = ctx->plugin_id;
    mambo_ht_t* parent_vc = ctx->thread_data->parent_thread->plugin_priv[p_id];

    // Initalise the vector clock
    mambo_ht_t* child_vc = mambo_alloc(ctx, sizeof(mambo_ht_t));
    int ret = mambo_ht_init(child_vc, parent_vc->size, parent_vc->index_shift, parent_vc->fill_factor, parent_vc->allow_resize);
    assert(ret == MAMBO_SUCCESS);

    // Insert the ID of the current thread in its VC
    ret = mambo_ht_add(child_vc, mambo_get_thread_id(ctx), 1);
    assert(ret == 0);


    // Join the data from its parent thread
    
    vc_join(child_vc, parent_vc);
    vc_increment(parent_vc, ctx->thread_data->parent_thread->tid);
    mambo_set_thread_plugin_data(ctx, child_vc);
  }

} // drd_pre_thread_handler

int drd_post_thread_handler(mambo_context *ctx) {

  //Free the memory used by the VC
  mambo_ht_t* thread_vc = mambo_get_thread_plugin_data(ctx);

  mambo_free(ctx, thread_vc);

} // drd_post_thread_handler





//------------------------------------------------------------------------------
// Synchronisation Handlers
//------------------------------------------------------------------------------





mambo_ht_t global_mutexes;

uintptr_t acquire(uintptr_t lock_ptr, mambo_ht_t* thread_vc){

  mambo_ht_t* lock_vc = NULL;
  int ret = mambo_ht_get(&global_mutexes, lock_ptr, (uintptr_t*)&lock_vc);

  if(ret != MAMBO_SUCCESS){// The lock is being acquired for the very first time

    lock_vc = malloc(sizeof(mambo_ht_t));

    ret = mambo_ht_init(lock_vc, thread_vc->size, thread_vc->index_shift, thread_vc->fill_factor, thread_vc->allow_resize);
    assert(ret == 0);

    // Associate the hash table with the switch statement
    ret = mambo_ht_add(&global_mutexes, lock_ptr, (uintptr_t)lock_vc);
    assert(ret == 0);

  }

  vc_join(thread_vc, lock_vc);

}// acquire


int drd_mutex_lock(mambo_context *ctx) {
  
  emit_push(ctx, (1 << x0) | (1 << x1));
  emit_set_reg_ptr(ctx, x1, mambo_get_thread_plugin_data(ctx));
  int ret = emit_safe_fcall(ctx, acquire, 2);
  assert(ret == 0);
  emit_pop(ctx, (1 << x0) | (1 << x1));

}// drd_mutex_lock


void release(uintptr_t lock_ptr, mambo_ht_t* thread_vc, int thread_id){

  mambo_ht_t* lock_vc;
  int ret = mambo_ht_get(&global_mutexes, lock_ptr, (uintptr_t*)&lock_vc);
  assert(ret==MAMBO_SUCCESS);

  vc_copy(lock_vc, thread_vc);  

  // Update pointer value in the mutex ht
  ret = mambo_ht_add(&global_mutexes, lock_ptr, (uintptr_t)lock_vc);
  assert(ret==MAMBO_SUCCESS);

  vc_increment(thread_vc, thread_id);

}// release


int drd_mutex_unlock(mambo_context *ctx) {

  emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2));
  emit_set_reg_ptr(ctx, x1, mambo_get_thread_plugin_data(ctx));
  emit_set_reg(ctx, x2, mambo_get_thread_id(ctx));
  int ret = emit_safe_fcall(ctx, release, 3);
  assert(ret == 0);
  emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2));

}// drd_mutex_unlock





//------------------------------------------------------------------------------
// MAMBO Declaration
//------------------------------------------------------------------------------

__attribute__((constructor)) void init_drd() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  printf("===== MAMBO DRD =====\n");

  
  // Threads
  int ret = mambo_register_pre_thread_cb(ctx, &drd_pre_thread_handler);
  assert(ret == MAMBO_SUCCESS);
  ret = mambo_register_post_thread_cb(ctx, &drd_post_thread_handler);
  assert(ret == MAMBO_SUCCESS);


  // Synchronisation
  ret = mambo_register_function_cb(ctx, "pthread_mutex_lock", &drd_mutex_lock, NULL, 2);
  assert(ret == MAMBO_SUCCESS);
  ret = mambo_register_function_cb(ctx, "pthread_mutex_unlock", &drd_mutex_unlock, NULL, 1);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_ht_init(&global_mutexes, 1024, 0, 70, true);
  assert(ret == MAMBO_SUCCESS);


  
  // ret = mambo_register_exit_cb(ctx, &drd_exit_handler);
  // assert(ret == MAMBO_SUCCESS);

  // ret = mambo_register_pre_inst_cb(ctx, &drd_pre_inst_handler);
  // assert(ret == MAMBO_SUCCESS);

}// init_drd
#endif