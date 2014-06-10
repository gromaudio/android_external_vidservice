/*****************************************************************************
* vidservice.c: Daemon that receives commands input devices and makes
*               camera devices produce pictures into frame buffers.
*
* No. | Date        | Author       | Description
* ============================================================================
* 1   | 6 Jun 2014 | Ivan Zaitsev | First release.
*
******************************************************************************/
/*
 * Copyright (C) 2013 X-Media tech, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */ 
 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <assert.h> 
#include <getopt.h>  
#include <fcntl.h>  
#include <unistd.h> 
#include <errno.h> 
#include <linux/input.h>
#include <sys/stat.h> 
#include <sys/types.h> 
#include <sys/time.h> 
#include <sys/mman.h> 
#include <sys/ioctl.h> 
#include <asm/types.h>


//--------------------------------------------------------------------------
static void errno_exit( const char * s ) 
{ 
  fprintf( stderr, "%s error %d, %s\n",s, errno, strerror( errno ) ); 
  exit( EXIT_FAILURE ); 
}
 
//-------------------------------------------------------------------------- 
int main (int argc,char ** argv) 
{ 
  char                *dev_name = "/dev/input/event1"; 
  int                 dev_fd,
                      res;
  struct input_event  event;

  dev_fd = open( dev_name, O_RDWR );
  if( dev_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", dev_name, strerror( errno ) );
    return -1;
  }


  for(;;)
  { 
    fd_set fds; 
    struct timeval tv; 
    int r; 
    FD_ZERO( &fds ); 
    FD_SET( dev_fd, &fds ); 

    /* Timeout. */ 
    tv.tv_sec  = 2; 
    tv.tv_usec = 0; 

    r = l( dev_fd + 1, &fds, NULL, NULL, &tv ); 

    if( -1 == r ) 
    { 
      if( EINTR == errno ) 
        continue; 
      errno_exit( "select" ); 
    }  

    fprintf( stderr, "EVENT: " );
    res = read( dev_fd, &event, sizeof( event ) );
    if( res == sizeof( event ) ) 
    {
       fprintf( stderr, "type %08X, code %08X, value %08X\n", event.type, event.code, event.value );
    }
    else
    {
      fprintf( stderr, "read error %s\n", strerror( errno ) );
    }
  } 

  close( dev_fd );
  return 0; 
} 


