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


#define DRD_SHARED 0xFFFFFFFF


//------------------------------------------------------------------------------
// Debug printing
//------------------------------------------------------------------------------

FILE *log_file;


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

bool vc_leq(mambo_ht_t* a, mambo_ht_t* b){

  for(int index = 0; index < a->size; index++)
    if(a->entries[index].value > b->entries[index].value)
      return false;

  return true;
}// vc_leq

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




typedef struct {
  bool in_mutex_op;
  mambo_ht_t* thread_vc;
} drd_thread_t;

int drd_pre_thread(mambo_context *ctx) {

  drd_thread_t *td = mambo_alloc(ctx, sizeof(drd_thread_t));
  assert(td != NULL);
  td->in_mutex_op = false;

  if(ctx->thread_data->parent_thread == NULL){// If this is the main thread

    // Initalise the vector clock for the current thread
    mambo_ht_t* thread_vc = mambo_alloc(ctx, sizeof(mambo_ht_t));
    assert(thread_vc != NULL);
    int ret = mambo_ht_init(thread_vc, 64, 0, 70, true);
    assert(ret == MAMBO_SUCCESS);


    // Insert the ID of the current thread in its VC
    ret = mambo_ht_add(thread_vc, mambo_get_thread_id(ctx), 1);
    assert(ret == 0);

    td->thread_vc = thread_vc;
  }else{// If the thread was spawned by another thread

    // Retrieve the VC of the parent thread
    unsigned int p_id = ctx->plugin_id;
    drd_thread_t* parent_data = ctx->thread_data->parent_thread->plugin_priv[p_id];
    mambo_ht_t* parent_vc = parent_data->thread_vc;

    // Initalise the vector clock
    mambo_ht_t* child_vc = mambo_alloc(ctx, sizeof(mambo_ht_t));
    assert(child_vc != NULL);
    int ret = mambo_ht_init(child_vc, parent_vc->size, parent_vc->index_shift, parent_vc->fill_factor, parent_vc->allow_resize);
    assert(ret == MAMBO_SUCCESS);

    // Insert the ID of the current thread in its VC
    ret = mambo_ht_add(child_vc, mambo_get_thread_id(ctx), 1);
    assert(ret == 0);


    // Join the data from its parent thread
    vc_join(child_vc, parent_vc);
    vc_increment(parent_vc, ctx->thread_data->parent_thread->tid);
    td->thread_vc = child_vc;
  }

  // Set the thread specific data
  mambo_set_thread_plugin_data(ctx, td);
} // drd_pre_thread

int drd_post_thread(mambo_context *ctx) {

  //Free the memory used by the VC
  drd_thread_t* td = mambo_get_thread_plugin_data(ctx);

  mambo_free(ctx, td->thread_vc);
  mambo_free(ctx, td); 

} // drd_post_thread





//------------------------------------------------------------------------------
// Synchronisation Handlers
//------------------------------------------------------------------------------





mambo_ht_t global_mutexes;

uintptr_t acquire(uintptr_t lock_ptr, drd_thread_t* td){

  mambo_ht_t* thread_vc = td->thread_vc;
  td->in_mutex_op = true;

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


int drd_pre_mutex_lock(mambo_context *ctx) {
  
  emit_push(ctx, (1 << x0) | (1 << x1));
  emit_set_reg_ptr(ctx, x1, mambo_get_thread_plugin_data(ctx));
  int ret = emit_safe_fcall(ctx, acquire, 2);
  assert(ret == MAMBO_SUCCESS);
  emit_pop(ctx, (1 << x0) | (1 << x1));

}// drd_pre_mutex_lock


void release(uintptr_t lock_ptr, drd_thread_t* td, int thread_id){

  mambo_ht_t* thread_vc = td->thread_vc;
  td->in_mutex_op = true;

  mambo_ht_t* lock_vc;
  int ret = mambo_ht_get(&global_mutexes, lock_ptr, (uintptr_t*)&lock_vc);
  assert(ret==MAMBO_SUCCESS);

  vc_copy(lock_vc, thread_vc);  

  // Update pointer value in the mutex ht
  ret = mambo_ht_add(&global_mutexes, lock_ptr, (uintptr_t)lock_vc);
  assert(ret==MAMBO_SUCCESS);

  vc_increment(thread_vc, thread_id);

}// release


int drd_pre_mutex_unlock(mambo_context *ctx) {

  emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2));
  emit_set_reg_ptr(ctx, x1, mambo_get_thread_plugin_data(ctx));
  emit_set_reg(ctx, x2, mambo_get_thread_id(ctx));
  int ret = emit_safe_fcall(ctx, release, 3);
  assert(ret == MAMBO_SUCCESS);
  emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2));

}// drd_pre_mutex_unlock


void mutex_op_exit(drd_thread_t* td){

  mambo_ht_t* thread_vc = td->thread_vc;
  td->in_mutex_op = false;

}// mutex_op_exit


int drd_post_mutex(mambo_context *ctx) {
  
  emit_push(ctx, (1 << x0));
  emit_set_reg_ptr(ctx, x0, mambo_get_thread_plugin_data(ctx));
  int ret = emit_safe_fcall(ctx, mutex_op_exit, 1);
  assert(ret == MAMBO_SUCCESS);
  emit_pop(ctx, (1 << x0));

}// drd_pre_mutex_lock






//------------------------------------------------------------------------------
// Memory operation instrumentation
//------------------------------------------------------------------------------





mambo_ht_t mem_acc;

typedef struct {
  uint32_t write_epoch;
  uint32_t read_epoch;
  mambo_ht_t* shared_vc;

  pthread_mutex_t synch;
} drd_mem_t;


// SHARED = 0xFFFFFFFF
void drd_write(uintptr_t addr, uint32_t current_epoch, drd_thread_t* td, uintptr_t source_addr) {

  if(td->in_mutex_op)
    return;

  uint32_t current_clock = (current_epoch & 0xFFFF);
  uint32_t current_tid   = (current_epoch & 0xFFFF0000) >> 16;

  drd_mem_t* vd;
  int ret = mambo_ht_get(&mem_acc, addr, (uintptr_t*) &vd);

  if(ret != MAMBO_SUCCESS){// The address is accessed for the first time

    // Associate the hash table with the switch statement
    drd_mem_t* new_acc = malloc(sizeof(drd_mem_t));

    if (pthread_mutex_init(&(new_acc->synch), NULL) == -1){                                  
      perror("mutex_init error");
      exit(2);
    }

    new_acc->write_epoch = current_epoch;
    new_acc->read_epoch  = 0;

    new_acc->shared_vc = malloc(sizeof(mambo_ht_t));
    ret = mambo_ht_init(new_acc->shared_vc, 256, 0, 70, true);
    assert(ret == MAMBO_SUCCESS);
    
    ret = mambo_ht_add(&mem_acc, addr, (uintptr_t)new_acc);
    assert(ret == 0);
    return;

  }else{

    if(vd->write_epoch == 0){
      vd->write_epoch = current_epoch;
      return;
    }

    pthread_mutex_lock(&(vd->synch));

    // Write-Write race
    uint32_t write_clock = (vd->write_epoch & 0xFFFF);
    uint32_t write_tid   = (vd->write_epoch & 0xFFFF0000) >> 16;
    // Retrieve the write time of the previous thread 
    uintptr_t last_op_time_write = 0;
    ret = mambo_ht_get(td->thread_vc, write_tid, &last_op_time_write);

    uint32_t read_clock = (vd->read_epoch & 0xFFFF);
    uint32_t read_tid   = (vd->read_epoch & 0xFFFF0000) >> 16;
    // Retrieve the write time of the previous thread
    uintptr_t last_op_time_read = 0;
    ret = mambo_ht_get(td->thread_vc, read_tid, &last_op_time_read);
    
    // Synch mechanism should be put in place

    // Write same epoch
    if(vd->write_epoch == current_epoch){
      pthread_mutex_unlock(&(vd->synch));
      return;
    }

    // Write-Write race
    if(write_clock > last_op_time_write){
      printf("Write-Write race detected @ %lx\n", source_addr);
      pthread_mutex_unlock(&(vd->synch));
      return;
    }


    // Read-Write Race
    if(vd->read_epoch != DRD_SHARED){ 

      if(read_clock > last_op_time_read){
        printf("Read-Write race detected @ %lx\n", source_addr);
        vd->write_epoch = current_epoch;
        pthread_mutex_unlock(&(vd->synch));
        return;
      }

    }else{// Shared-Write

      if(!vc_leq(vd->shared_vc, td->thread_vc)){
        printf("Shared-Write race detected @ %lx\n", source_addr);
        vd->write_epoch = current_epoch;
        pthread_mutex_unlock(&(vd->synch));
        return;
      }
    }

    pthread_mutex_unlock(&(vd->synch));

  }
} //drd_write


void drd_read(uintptr_t addr, uint32_t current_epoch, drd_thread_t* td, uintptr_t source_addr) {

  if(td->in_mutex_op)
    return;

  uint32_t current_clock = (current_epoch & 0xFFFF);
  uint32_t current_tid   = (current_epoch & 0xFFFF0000) >> 16;

  drd_mem_t* vd;
  int ret = mambo_ht_get(&mem_acc, addr, (uintptr_t*) &vd);

  if(ret != MAMBO_SUCCESS){// The address is accessed for the first time

    // Associate the hash table with the switch statement
    drd_mem_t* new_acc = malloc(sizeof(drd_mem_t));

    if (pthread_mutex_init(&(new_acc->synch), NULL) == -1){                                  
      perror("mutex_init error");
      exit(2);
    }

    new_acc->write_epoch = 0;
    new_acc->read_epoch  = current_epoch;

    new_acc->shared_vc = malloc(sizeof(mambo_ht_t));
    ret = mambo_ht_init(new_acc->shared_vc, 256, 0, 70, true);
    assert(ret == MAMBO_SUCCESS);
    
    ret = mambo_ht_add(&mem_acc, addr, (uintptr_t)new_acc);
    assert(ret == 0);
    return;

  }else{

    if(vd->read_epoch == 0){
      vd->read_epoch = current_epoch;
      return;
    }

    pthread_mutex_lock(&(vd->synch));

    uint32_t write_clock = (vd->write_epoch & 0xFFFF);
    uint32_t write_tid   = (vd->write_epoch & 0xFFFF0000) >> 16;
    // Retrieve the write time of the previous thread 
    uintptr_t last_op_time_write = 0;
    ret = mambo_ht_get(td->thread_vc, write_tid, &last_op_time_write);

    uint32_t read_clock = (vd->read_epoch & 0xFFFF);
    uint32_t read_tid   = (vd->read_epoch & 0xFFFF0000) >> 16;
    // Retrieve the write time of the previous thread
    uintptr_t last_op_time_read = 0;
    ret = mambo_ht_get(td->thread_vc, read_tid, &last_op_time_read);

    // if(vd->read_epoch == 0)
    //   vd->read_epoch = current_epoch;


    // Write same epoch
    if(vd->read_epoch == current_epoch){
      pthread_mutex_unlock(&(vd->synch));
      return;
    }


    uint32_t shared_epoch = 0;
    ret = mambo_ht_add(vd->shared_vc, current_tid, (uintptr_t)shared_epoch);

    // Read Shared same epoch
    if(vd->read_epoch==DRD_SHARED && shared_epoch == current_epoch){
      pthread_mutex_unlock(&(vd->synch));
      return;
    }


    if(write_clock > last_op_time_write){
      printf("Write-Read race detected @ %lx\n", source_addr);
      pthread_mutex_unlock(&(vd->synch));
      return;
    }



    if(vd->read_epoch!=DRD_SHARED){
      if(read_clock <= last_op_time_read)
        vd->read_epoch = current_epoch;
      else{
        
        ret = mambo_ht_add(vd->shared_vc, read_tid, (uintptr_t)read_clock);
        assert(ret == 0);

        ret = mambo_ht_add(vd->shared_vc, current_tid, (uintptr_t)current_clock);
        assert(ret == 0);

        vd->read_epoch = DRD_SHARED;
      }
    }else{
      ret = mambo_ht_add(vd->shared_vc, current_tid, (uintptr_t)current_clock);
      assert(ret == 0);
    }

    pthread_mutex_unlock(&(vd->synch));

  }
} //drd_read



// Ignore Store Exclusive instructions
bool should_ignore(mambo_context *ctx){
  if (mambo_get_inst(ctx) == A64_LDX_STX) {
    uint32_t size, o2, l, o1, rs, o0, rt2, rn, rt;
    a64_LDX_STX_decode_fields(mambo_get_source_addr(ctx), &size, &o2, &l, &o1, &rs, &o0, &rt2, &rn, &rt);
    // don't instrument store exclusive
    if (o2 == 0 && l == 0 && o1 == 0) {
      return true;
    }
  }
  return false;
}//should_ignore


int drd_pre_inst(mambo_context *ctx) {

  if(mambo_is_load_or_store(ctx) && !should_ignore(ctx)){// Insturment a memory access


    drd_thread_t* td = mambo_get_thread_plugin_data(ctx);
    mambo_ht_t* thread_vc = td->thread_vc;
    uintptr_t l_clock;
    int ret = mambo_ht_get(thread_vc, mambo_get_thread_id(ctx), &l_clock);
    assert(ret == 0);


    // LSB
    // 16 bits - Clock
    // 16 bits - TID
    // MSB
    uint32_t epoch_data = mambo_get_thread_id(ctx) << 16;
    epoch_data = epoch_data + (l_clock&0xFFFF);


    // Handle reads and writes separately to maintain code clarity
    if(mambo_is_load(ctx)){ // Read

      emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));

      ret = mambo_calc_ld_st_addr(ctx, 0);
      assert(ret == MAMBO_SUCCESS);
      emit_set_reg(ctx, x1, epoch_data); 
      emit_set_reg(ctx, x2, (uintptr_t) mambo_get_thread_plugin_data(ctx));
      emit_set_reg(ctx, x3, (uintptr_t)mambo_get_source_addr(ctx));
      ret = emit_safe_fcall(ctx, drd_read, 0);
      assert(ret == MAMBO_SUCCESS);

      emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));

    }else{ // Write

      emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));

      ret = mambo_calc_ld_st_addr(ctx, 0);
      assert(ret == MAMBO_SUCCESS);
      emit_set_reg(ctx, x1, epoch_data); 
      emit_set_reg(ctx, x2, (uintptr_t) mambo_get_thread_plugin_data(ctx));
      emit_set_reg(ctx, x3, (uintptr_t) mambo_get_source_addr(ctx));
      ret = emit_safe_fcall(ctx, drd_write, 0);
      assert(ret == MAMBO_SUCCESS);

      emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));

    }
  }
} // drd_pre_inst




























int drd_exit(mambo_context *ctx) {

  for(int index = 0; index < global_mutexes.size; index++){
    if(global_mutexes.entries[index].key != 0){
      free((mambo_ht_t*)global_mutexes.entries[index].value);
    }
  }


  for(int index = 0; index < mem_acc.size; index++){
    if(mem_acc.entries[index].key != 0){
      free((drd_mem_t*)mem_acc.entries[index].value);
    }
  }


  // printf("ACC: %d\n", access);
  // printf("SAFE F ACC: %d\n", safef_access);
  fclose(log_file);

} // drd_exit





//------------------------------------------------------------------------------
// MAMBO setup
//------------------------------------------------------------------------------

__attribute__((constructor)) void init_drd() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  printf("===== MAMBO DRD =====\n");

  
  // Threads
  int ret = mambo_register_pre_thread_cb(ctx, &drd_pre_thread);
  assert(ret == MAMBO_SUCCESS);
  ret = mambo_register_post_thread_cb(ctx, &drd_post_thread);
  assert(ret == MAMBO_SUCCESS);


  // Synchronisation
  ret = mambo_register_function_cb(ctx, "pthread_mutex_lock", &drd_pre_mutex_lock, &drd_post_mutex, 2);
  assert(ret == MAMBO_SUCCESS);
  ret = mambo_register_function_cb(ctx, "pthread_mutex_unlock", &drd_pre_mutex_unlock, &drd_post_mutex, 1);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_ht_init(&global_mutexes, 1024, 0, 70, true);
  assert(ret == MAMBO_SUCCESS);



  ret = mambo_ht_init(&mem_acc, 32768, 0, 70, true);
  assert(ret == MAMBO_SUCCESS);



  // Memory accesses instrumentation
  ret = mambo_register_pre_inst_cb(ctx, &drd_pre_inst);
  assert(ret == MAMBO_SUCCESS);


  
  ret = mambo_register_exit_cb(ctx, &drd_exit);
  assert(ret == MAMBO_SUCCESS);



  // Initalise Debugging
  log_file = fopen("log.txt", "w");
}// init_drd
#endif