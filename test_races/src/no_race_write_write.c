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

/*
  This is an example of a data race being avoided by using mutexes as a 
  synchronisation method.
*/

#include <pthread.h>

int shared_var;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *th()
{
  pthread_mutex_lock(&mutex);
  shared_var++;
  pthread_mutex_unlock(&mutex);
}

int main()
{
  pthread_t th0, th1;

  pthread_create(&th0, NULL, th, NULL);
  pthread_create(&th1, NULL, th, NULL);

  pthread_join(th0, NULL);
  pthread_join(th1, NULL);

  return 0;
}