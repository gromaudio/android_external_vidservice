/*****************************************************************************
* vidservice.c: Daemon that receives commands input devices and makes
*               camera devices produce pictures into frame buffers.
*
* Authors: Ivan Zaitsev
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
#include <stdbool.h>
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
#define CAMERA_OUT_DEVICE_NAME  "/dev/graphics/fb0"
#define VIDEO_OUT_SWITCH_NAME   "/sys/class/video_output/LCD/state"

#define HUD_OUT_DEVICE_NAME     "/dev/graphics/fb4"
#define HUD_PICTURE_FILE_NAME   "/boot/hud/screen_1.bmp"
#define HUD_NUM_OF_PICTURES     4

#define CAMERA_OUT_FB_SIZE     ( 640 * 480 * 2 )

#define CLEAR(x) memset (&(x), 0, sizeof (x))

//--------------------------------------------------------------------------
typedef struct
{ 
    void * start; 
    size_t length; 
}BUFFER; 

typedef struct
{
  int             fd;
  unsigned int    PixelFormat;
  unsigned int    NumOfBuffers;
  BUFFER         *buffers;
}CAMERA_DEVICE;

typedef struct
{
  int                       fd;
  struct fb_var_screeninfo  vInfo;
  struct fb_fix_screeninfo  fInfo;
  unsigned int              BuffSize;
  unsigned char            *Buff;
}FB_DEVICE;

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
static void updateHud( char *sHudDevName, int iScrId ) 
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
static int initMmap( CAMERA_DEVICE* pBuffDev )
{
  struct v4l2_requestbuffers req; 

  CLEAR (req); 
  req.count   = 4; 
  req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  req.memory  = V4L2_MEMORY_MMAP; 

  if( -1 == xioctl( pBuffDev->fd, VIDIOC_REQBUFS, &req ) ) 
  { 
    fprintf (stderr, "Error: does not support memory mapping.\n" ); 
    return 1;
  } 

  if( req.count < 4 ) 
  { 
    fprintf (stderr, "Error: Insufficient buffer memory.\n" ); 
    return 1;
  } 

  pBuffDev->buffers = calloc( req.count, sizeof( BUFFER ) ); 
  if( !pBuffDev->buffers ) 
  { 
    fprintf( stderr, "Error: Out of memory.\n" ); 
    return 1;
  } 

  for( pBuffDev->NumOfBuffers = 0; pBuffDev->NumOfBuffers < req.count; ++pBuffDev->NumOfBuffers ) 
  { 
    struct v4l2_buffer buf; 

    CLEAR (buf); 

    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf.memory = V4L2_MEMORY_MMAP; 
    buf.index  = pBuffDev->NumOfBuffers; 

    if( -1 == xioctl( pBuffDev->fd, VIDIOC_QUERYBUF, &buf ) ) 
    {
      fprintf( stderr, "Error: VIDIOC_QUERYBUF.\n" ); 
      return 1;
    }

    pBuffDev->buffers[ pBuffDev->NumOfBuffers ].length = buf.length; 
    pBuffDev->buffers[ pBuffDev->NumOfBuffers ].start  = mmap( NULL, buf.length, PROT_READ | PROT_WRITE ,MAP_SHARED, pBuffDev->fd, buf.m.offset ); 

    if( MAP_FAILED == pBuffDev->buffers[ pBuffDev->NumOfBuffers ].start ) 
    {
      fprintf( stderr, "Error: mmap.\n" ); 
      return 1;
    }
  }
  return 0;
}

//-------------------------------------------------------------------------- 
static int startStreaming( CAMERA_DEVICE* pBuffDev )
{
  unsigned int        i; 
  enum v4l2_buf_type  type; 

  for( i = 0; i < pBuffDev->NumOfBuffers; ++i ) 
  { 
    struct v4l2_buffer buf; 
    CLEAR (buf); 

    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf.memory = V4L2_MEMORY_MMAP; 
    buf.index  = i; 

    if( -1 == xioctl( pBuffDev->fd, VIDIOC_QBUF, &buf ) )
    {
      fprintf( stderr, "Error: VIDIOC_QBUF.\n" ); 
      return 1;
    }
  } 

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 

  if( -1 == xioctl( pBuffDev->fd, VIDIOC_STREAMON, &type ) ) 
  {
    fprintf( stderr, "Error: VIDIOC_STREAMON.\n" ); 
    return 1;
  }

  return 0;
}

//-------------------------------------------------------------------------- 
static int startCamera( char* sCamDevName, 
                        unsigned int PixelFormat, 
                        CAMERA_DEVICE* pBuffDev )
{
  int                 input;
  struct v4l2_cropcap cropcap; 
  struct v4l2_crop    crop; 
  struct v4l2_format  fmt; 

  pBuffDev->fd = open( sCamDevName, O_RDWR | O_NONBLOCK, 0 ); 
  if( pBuffDev->fd < 0 ) 
  { 
    fprintf( stderr, "Cannot open '%s': %d, %s\n",sCamDevName, errno, strerror (errno) ); 
    return 1; 
  }

  input = 1;
  fprintf( stderr, "Select V4L2 input %d for device %s\n", input, sCamDevName );
  if( ioctl( pBuffDev->fd, VIDIOC_S_INPUT, &input ) < 0 ) 
  {
    fprintf( stderr, "VIDIOC_S_INPUT error.\n" ); 
    return 1; 
  }

  // Select video input, video standard and tune here.
  CLEAR (cropcap); 
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  if( 0 == xioctl( pBuffDev->fd, VIDIOC_CROPCAP, &cropcap ) ) 
  { 
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    crop.c    = cropcap.defrect;
    xioctl( pBuffDev->fd, VIDIOC_S_CROP, &crop );
  }

  CLEAR( fmt ); 
  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  fmt.fmt.pix.width       = 640;  
  fmt.fmt.pix.height      = 480; 
  fmt.fmt.pix.pixelformat = PixelFormat;
  fmt.fmt.pix.field       = V4L2_FIELD_NONE; 
  pBuffDev->PixelFormat   = PixelFormat;

  if( -1 == xioctl( pBuffDev->fd, VIDIOC_S_FMT, &fmt ) ) 
  {
    fprintf( stderr, "VIDIOC_S_FMT error.\n" ); 
    return 1;
  }

  fprintf(stderr, "%dx%d, %c%c%c%c, %d\n", 
          fmt.fmt.pix.width, 
          fmt.fmt.pix.height, 
          (   fmt.fmt.pix.pixelformat & 0xFF ), 
          ( ( fmt.fmt.pix.pixelformat >> 8 ) & 0xFF ), 
          ( ( fmt.fmt.pix.pixelformat >> 16 ) & 0xFF ), 
          ( ( fmt.fmt.pix.pixelformat >> 24 ) & 0xFF ),
          fmt.fmt.pix.field );

  if( initMmap( pBuffDev ) )
    return 1;

  if( startStreaming( pBuffDev ) )
    return 1;

  return 0;
}

//--------------------------------------------------------------------------
static void processImage( CAMERA_DEVICE* pBuffDev, FB_DEVICE* pFbDev, unsigned int BuffIdx )
{
  unsigned char *pSrcBuff,
                *pDstBuff;

  if( -1 == xioctl( pFbDev->fd, FBIOGET_VSCREENINFO, &pFbDev->vInfo ) ) 
  { 
    fprintf( stderr, "Error: reading variable information.\n"); 
    return; 
  }

  pSrcBuff = pBuffDev->buffers[ BuffIdx ].start;
  pDstBuff = pFbDev->Buff + pFbDev->vInfo.yoffset * pFbDev->fInfo.line_length;



  memcpy( pDstBuff, pSrcBuff, pBuffDev->buffers[ BuffIdx ].length );



  fprintf( stderr, "Frame 0x%08X --> 0x%08X.\n", (unsigned int)pSrcBuff, (unsigned int)pDstBuff );
}

//-------------------------------------------------------------------------- 
static void readFrame( CAMERA_DEVICE* pBuffDev, FB_DEVICE* pFbDev, bool bCamActive ) 
{
  struct v4l2_buffer buf; 
  unsigned int       i; 

  CLEAR (buf); 
  buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  buf.memory = V4L2_MEMORY_MMAP; 

  if( -1 == xioctl( pBuffDev->fd, VIDIOC_DQBUF, &buf ) ) 
  { 
    switch (errno) 
    { 
      case EAGAIN: 
        return; 
    
      case EIO:
      default: 
        fprintf( stderr, "Error: VIDIOC_DQBUF.\n"); 
    } 
  } 

  assert( buf.index < pBuffDev->NumOfBuffers );
  assert( buf.field ==V4L2_FIELD_ANY );
  if( bCamActive ) 
    processImage( pBuffDev, pFbDev, buf.index ); 
  if( -1 == xioctl( pBuffDev->fd, VIDIOC_QBUF, &buf ) ) 
    fprintf( stderr, "Error: VIDIOC_QBUF.\n"); 
}

//-------------------------------------------------------------------------- 
static int initFb( char* sFbDevName, 
                   FB_DEVICE* pFbDev )
{
  // Open frame buffer device. 
  pFbDev->fd = open( sFbDevName, O_RDWR );
  if( pFbDev->fd < 0 ) 
  {
    fprintf( stderr, "Error: could not open %s, %s\n", sFbDevName, strerror( errno ) );
    return 1;
  }

  // Get fixed screen information. 
  if( -1 == xioctl( pFbDev->fd, FBIOGET_FSCREENINFO, &pFbDev->fInfo ) ) 
  { 
    fprintf( stderr, "Error: reading fixed information.\n" ); 
    return 1;
  }

  // Get variable screen information. 
  if( -1 == xioctl( pFbDev->fd, FBIOGET_VSCREENINFO, &pFbDev->vInfo ) ) 
  { 
    fprintf( stderr, "Error: reading variable information.\n" ); 
    return 1; 
  } 
  pFbDev->BuffSize = pFbDev->vInfo.xres_virtual * pFbDev->vInfo.yres_virtual * pFbDev->vInfo.bits_per_pixel / 8;

  // Map frame buffer device to memory.
  pFbDev->Buff = ( unsigned char * )mmap( NULL, pFbDev->BuffSize, PROT_READ | PROT_WRITE, MAP_SHARED , pFbDev->fd, 0 ); 
  if( (int)pFbDev->Buff == -1 ) 
  { 
    fprintf( stderr, "Error: failed to map framebuffer device to memory.\n" ); 
    return 1;
  }

  return 0;
}
 
//-------------------------------------------------------------------------- 
int main (int argc,char ** argv) 
{ 
  CAMERA_DEVICE       CamInt,
                      CamExt;
  FB_DEVICE           CamOutFb;
  int                 input_fd,
                      video_out_fd;
  int                 res,
                      iHudPicId;
  struct input_event  event;
  bool                bExit,
                      bIntCamActive;

  input_fd = open( INPUT_DEVICE_NAME, O_RDWR | O_NONBLOCK );
  if( input_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", INPUT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }

  video_out_fd = open( VIDEO_OUT_SWITCH_NAME, O_RDWR );
  if( video_out_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", VIDEO_OUT_SWITCH_NAME, strerror( errno ) );
    goto exit;
  }

  if( initFb( CAMERA_OUT_DEVICE_NAME, &CamOutFb ) )
  {
    fprintf( stderr, "could not init frame buffer %s\n", CAMERA_OUT_DEVICE_NAME );
    goto exit;
  }

  if( startCamera( CAMERA_EXT_DEVICE_NAME, V4L2_PIX_FMT_NV12, &CamExt ) )
  {
    fprintf( stderr, "could not start camera %s\n", CAMERA_EXT_DEVICE_NAME );
    goto exit;
  }

//  if( startCamera( CAMERA_INT_DEVICE_NAME, V4L2_PIX_FMT_RGB24, &CamInt ) )
//  {
//    fprintf( stderr, "could start camera %s\n", CAMERA_EXT_DEVICE_NAME );
//    goto exit;
//  }


/*
  cam_out_fd = open( CAMERA_OUT_DEVICE_NAME, O_RDWR );
  if( cam_out_fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", CAMERA_OUT_DEVICE_NAME, strerror( errno ) );
    goto exit;
  }
*/
  

  iHudPicId = 0;
  write( video_out_fd, "0", 1 );
  updateHud( HUD_OUT_DEVICE_NAME, iHudPicId );

  bIntCamActive = true;
  bExit         = false;
  while( !bExit )
  { 
    fd_set fds; 
    struct timeval tv; 
    int r; 
    FD_ZERO( &fds ); 
    FD_SET( input_fd, &fds ); 
    FD_SET( CamExt.fd, &fds ); 

    tv.tv_sec  = 2; 
    tv.tv_usec = 0; 

    r = select( CamExt.fd + 1, &fds, NULL, NULL, &tv ); 

    if( FD_ISSET( CamExt.fd, &fds) )
    {
      readFrame( &CamExt, &CamOutFb, bIntCamActive );
    }

    if( FD_ISSET( input_fd, &fds) )
    {
      fprintf( stderr, "EVENT (%d): ", r );
      res = read( input_fd, &event, sizeof( event ) );
      if( res == sizeof( event ) ) 
      {
         fprintf( stderr, "type %08X, code %08X, value %08X\n", event.type, event.code, event.value );
         if( event.value )
         {
            switch( event.code )
            {
              case KEY_F1:
                write( video_out_fd, "0", 1 );
                break;

              case KEY_F2:
                bIntCamActive = true;
                write( video_out_fd, "1", 1 );
                break;

              case KEY_F3:
                bIntCamActive = false;
                write( video_out_fd, "1", 1 );
                break;

              case KEY_F4:
                iHudPicId = ( iHudPicId + 1 ) % HUD_NUM_OF_PICTURES;
                updateHud( HUD_OUT_DEVICE_NAME, iHudPicId );
                break;

              case KEY_C:
                bExit = true;
                break;
            }
         }
      }
    }
  } 


exit:
  return 0; 
} 


