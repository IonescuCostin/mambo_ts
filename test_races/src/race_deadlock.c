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
  This is an example of a simple, possible deadlock.
*/
#include <pthread.h>

pthread_mutex_t lock0 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock1 = PTHREAD_MUTEX_INITIALIZER;

void *th0_run()
{
    pthread_mutex_lock(&lock0);
    pthread_mutex_lock(&lock1);
    pthread_mutex_unlock(&lock0);
    pthread_mutex_unlock(&lock1);
}

void *th1_run()
{
    pthread_mutex_lock(&lock1);
    pthread_mutex_lock(&lock0);
    pthread_mutex_unlock(&lock0);
    pthread_mutex_unlock(&lock1);
}

int main()
{
    pthread_t th0, th1;

    pthread_create(&th0, NULL, th0_run, NULL);     
    pthread_create(&th1, NULL, th1_run, NULL);

    pthread_join(th0, NULL);
    pthread_join(th1, NULL);

    return 0;
}