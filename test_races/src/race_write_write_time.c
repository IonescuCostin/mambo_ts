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
  Compared to the simple version this contains a time delay that is meant to 
  order memory accesses. This does not represent a legitimate synchronisation 
  mechanism and should be detected by the tool.
*/

#include <pthread.h>
#include <unistd.h>

int shared_var;

void *th()
{
  shared_var++;
}

int main()
{
  pthread_t th0, th1;

  pthread_create(&th0, NULL, th, NULL);
  sleep(1);
  pthread_create(&th1, NULL, th, NULL);

  pthread_join(th0, NULL);
  pthread_join(th1, NULL);

  return 0;
}