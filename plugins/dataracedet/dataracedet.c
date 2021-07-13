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

#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "dataracedet.h"
#include "../../plugins.h"


mambo_ht_t vector_clocks;

int drd_exit_handler(mambo_context *ctx) {
  printf("MAMBO DRD EXIT\n");
}



//------------------------------------------------------------------------------
// Pthread function calls handlers
//------------------------------------------------------------------------------

int drd_mutex_lock(mambo_context *ctx) {

  // Retrieve existing thread events
  mambo_ht_t* thread_events = mambo_get_thread_plugin_data(ctx);

  // Create the current event
  drd_event* new_event = mambo_alloc(ctx, sizeof(drd_event));

  new_event->is_lock_op = true;
  new_event->thread_clocks = mambo_alloc(ctx, thread_events->entry_count * sizeof(uint64_t));
  new_event->thread_ids = mambo_alloc(ctx, thread_events->entry_count * sizeof(uint32_t));

  int helper_index = 0;
  for(int index = 0; index < thread_events->size; index++)
    if(thread_events->entries[index].key != 0){
      new_event->thread_ids[helper_index] = thread_events->entries[index].key;
      new_event->thread_clocks[helper_index++] = thread_events->entries[index].value;
    }
  
  // Record the current event
  int ret = mambo_ht_add(thread_events, (uintptr_t) new_event, 1);
  assert(ret == MAMBO_SUCCESS);


  // Increment the clock for the current thread 
  uintptr_t thread_clock;

  ret = mambo_ht_get(&vector_clocks, mambo_get_thread_id(ctx), &thread_clock);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_ht_add(&vector_clocks, mambo_get_thread_id(ctx), thread_clock+1);
  assert(ret == MAMBO_SUCCESS);
}// drd_mutex_lock



int drd_mutex_unlock(mambo_context *ctx) {

  // Retrieve existing thread events
  mambo_ht_t* thread_events = mambo_get_thread_plugin_data(ctx);

  // Create the current event
  drd_event* new_event = mambo_alloc(ctx, sizeof(drd_event));

  new_event->is_lock_op = true;
  new_event->thread_clocks = mambo_alloc(ctx, thread_events->entry_count * sizeof(uint64_t));
  new_event->thread_ids = mambo_alloc(ctx, thread_events->entry_count * sizeof(uint32_t));

  int helper_index = 0;
  for(int index = 0; index < thread_events->size; index++)
    if(thread_events->entries[index].key != 0){
      new_event->thread_ids[helper_index] = thread_events->entries[index].key;
      new_event->thread_clocks[helper_index++] = thread_events->entries[index].value;
    }
  
  // Record the current event
  int ret = mambo_ht_add(thread_events, (uintptr_t) new_event, 1);
  assert(ret == MAMBO_SUCCESS);

  
  uintptr_t thread_clock;

  ret = mambo_ht_get(&vector_clocks, mambo_get_thread_id(ctx), &thread_clock);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_ht_add(&vector_clocks, mambo_get_thread_id(ctx), thread_clock+1);
  assert(ret == MAMBO_SUCCESS);
}// drd_mutex_unlock



//------------------------------------------------------------------------------
// Thread Handlers
//------------------------------------------------------------------------------
int drd_pre_thread_handler(mambo_context *ctx) {

  int ret = mambo_ht_add(&vector_clocks, mambo_get_thread_id(ctx), 1);
  assert(ret == MAMBO_SUCCESS);


  mambo_ht_t* thread_events = mambo_alloc(ctx, sizeof(mambo_ht_t));

  ret = mambo_ht_init(thread_events, 128, 0, 70, true);
  assert(ret == MAMBO_SUCCESS);

  mambo_set_thread_plugin_data(ctx, thread_events);
} // drd_post_thread_handler

int drd_post_thread_handler(mambo_context *ctx) {

  uintptr_t thread_clock;
  int ret = mambo_ht_get(&vector_clocks, mambo_get_thread_id(ctx), &thread_clock);
  assert(ret == MAMBO_SUCCESS);

  printf("Thread %X\n", mambo_get_thread_id(ctx));
  printf("Vector Clock %ld\n\n", thread_clock);

  //Free the memory used to record thread events
  mambo_ht_t* thread_events = mambo_get_thread_plugin_data(ctx);

  for(int index = 0; index < thread_events->size; index++)
    if(thread_events->entries[index].key != 0){
      drd_event* event = (drd_event*)thread_events->entries[index].key;
      mambo_free(ctx, event->thread_clocks);
      mambo_free(ctx, event->thread_ids);
      mambo_free(ctx, event);
    }
  
  mambo_free(ctx, thread_events);
} // drd_post_thread_handler



__attribute__((constructor)) void init_drd() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  printf("===== MAMBO DRD =====\n");

  int ret = mambo_ht_init(&vector_clocks, 128, 0, 70, true);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_register_pre_thread_cb(ctx, &drd_pre_thread_handler);
  assert(ret == MAMBO_SUCCESS);
  ret = mambo_register_post_thread_cb(ctx, &drd_post_thread_handler);
  assert(ret == MAMBO_SUCCESS);


  ret = mambo_register_function_cb(ctx, "pthread_mutex_lock", &drd_mutex_lock, NULL, 2);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_register_function_cb(ctx, "pthread_mutex_unlock", &drd_mutex_unlock, NULL, 1);
  assert(ret == MAMBO_SUCCESS);





  // ret = mambo_register_exit_cb(ctx, &drd_exit_handler);
  // assert(ret == MAMBO_SUCCESS);

  // ret = mambo_register_pre_inst_cb(ctx, &drd_pre_inst_handler);
  // assert(ret == MAMBO_SUCCESS);



//   ret = mambo_register_function_cb(ctx, "pthread_create", &tesan_mutex_create, NULL, 1);
//   assert(ret == MAMBO_SUCCESS);
//   mambo_register_post_thread_cb(ctx, &cachesim_post_thread_handler);

}// init_drd
