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
#include <linux/videodev2.h> 
#include <linux/fb.h> 

//--------------------------------------------------------------------------
#define INPUT_DEVICE_NAME       "/dev/input/event1"
#define CAMERA_EXT_DEVICE_NAME  "/dev/video0"
#define CAMERA_INT_DEVICE_NAME  "/dev/video1"
#define CAMERA_OUT_DEVICE_NAME  "/dev/graphics/fb2"
#define HUD_OUT_DEVICE_NAME     "/dev/graphics/fb4"
#define HUD_PICTURE_FILE_NAME   "/boot/hud/screen_1.bmp"
#define HUD_NUM_OF_PICTURES     4

//--------------------------------------------------------------------------
int     input_fd,
        cam_ext_fd, 
        cam_int_fd,
        cam_out_fd;

//--------------------------------------------------------------------------
static int xioctl( int fd,int request,void * arg ) 
{ 
  int r; 
  do
  {
    r = ioctl( fd, request, arg ); 
  }while( -1 == r && EINTR == errno ); 
  return r; 
} 

//--------------------------------------------------------------------------
static void errno_exit( const char * s ) 
{ 
  fprintf( stderr, "%s error %d, %s\n",s, errno, strerror( errno ) ); 
  exit( EXIT_FAILURE ); 
}

//--------------------------------------------------------------------------
static void update_hud( char *sHudDevName, int iScrId ) 
{ 
  int                         hud_fd, 
                              pic_fd,
                              hudScrSize;
  char                        sPicName[ 100 ];
  struct  fb_var_screeninfo   vHudInfo;
  struct  fb_fix_screeninfo   fHudInfo; 
  unsigned char              *pHudFb,
                             *pPicFb;
  struct stat                 PicStat;

  hud_fd = open( sHudDevName, O_RDWR );
  if( hud_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", sHudDevName, strerror( errno ) );
    return;
  }

  strncpy( sPicName, HUD_PICTURE_FILE_NAME, sizeof( sPicName ) );
  sPicName[ strlen( sPicName ) - 5 ] = 0x30 + iScrId;
  pic_fd = open( sPicName, O_RDWR );
  if( pic_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", sPicName, strerror( errno ) );
    close( hud_fd );
    return;
  }

  // Get fixed screen information 
  if( -1 == xioctl( hud_fd, FBIOGET_FSCREENINFO, &fHudInfo ) ) 
  { 
    printf("Error reading fixed information.\n"); 
    goto hud_exit; 
  }

  // Get variable screen information. 
  if( -1 == xioctl( hud_fd, FBIOGET_VSCREENINFO, &vHudInfo ) ) 
  { 
    fprintf( stderr, "Error reading variable information.\n" ); 
    goto hud_exit; 
  } 
  hudScrSize = vHudInfo.xres_virtual * vHudInfo.yres_virtual * vHudInfo.bits_per_pixel / 8;


  // Map frame buffer device to memory.
  pHudFb = ( unsigned char * )mmap( NULL, hudScrSize, PROT_READ | PROT_WRITE, MAP_SHARED , hud_fd, 0 ); 
  if( (int)pHudFb == -1 ) 
  { 
    fprintf( stderr, "Error: failed to map framebuffer device to memory.\n" ); 
    goto hud_exit; 
  } 

  // Map input picture to memory.
  fstat( pic_fd, &PicStat );
  pPicFb = ( unsigned char * )mmap( NULL, PicStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED , pic_fd, 0 ); 
  if( (int)pPicFb == -1 ) 
  { 
    fprintf( stderr, "Error: failed to map picture to memory.\n" ); 
    goto hud_exit; 
  } 

  memcpy( pHudFb + vHudInfo.yoffset * fHudInfo.line_length, pPicFb + 54, hudScrSize );

  munmap( pHudFb, hudScrSize );
  munmap( pPicFb, PicStat.st_size );

hud_exit:
  close( hud_fd );
  close( pic_fd );
}
 
//-------------------------------------------------------------------------- 
int main (int argc,char ** argv) 
{ 
  int                 res,
                      iHudPicId;
  struct input_event  event;

  input_fd = open( INPUT_DEVICE_NAME, O_RDWR );
  if( input_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", INPUT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }

  cam_ext_fd = open( CAMERA_EXT_DEVICE_NAME, O_RDWR | O_NONBLOCK );
  if( cam_ext_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", CAMERA_EXT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }

  cam_int_fd = open( CAMERA_EXT_DEVICE_NAME, O_RDWR | O_NONBLOCK );
  if( cam_int_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", CAMERA_EXT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }

  cam_out_fd = open( CAMERA_OUT_DEVICE_NAME, O_RDWR );
  if( cam_out_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", CAMERA_OUT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }

  iHudPicId = 0;
  update_hud( HUD_OUT_DEVICE_NAME, iHudPicId );

  for(;;)
  { 
    fd_set fds; 
    struct timeval tv; 
    int r; 
    FD_ZERO( &fds ); 
    FD_SET( input_fd, &fds ); 

    tv.tv_sec  = 2; 
    tv.tv_usec = 0; 

    r = select( input_fd + 1, &fds, NULL, NULL, &tv ); 

    if( -1 == r ) 
    { 
      if( EINTR == errno ) 
        continue; 
      errno_exit( "select" ); 
    }  

    fprintf( stderr, "EVENT: " );
    res = read( input_fd, &event, sizeof( event ) );
    if( res == sizeof( event ) ) 
    {
       fprintf( stderr, "type %08X, code %08X, value %08X\n", event.type, event.code, event.value );
       if( event.value )
       {
          switch( event.code )
          {
            case KEY_F1:
              iHudPicId = ( iHudPicId + 1 ) % HUD_NUM_OF_PICTURES;
              update_hud( HUD_OUT_DEVICE_NAME, iHudPicId );
              break;

          }
       }
    }
    else
    {
      fprintf( stderr, "read error %s\n", strerror( errno ) );
    }
  } 


exit:
  close( input_fd );
  close( cam_ext_fd );
  close( cam_int_fd );
  close( cam_out_fd );
  return 0; 
} 


