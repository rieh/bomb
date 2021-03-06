/* bomb win32 driver main file
   see http://draves.org/bomb
   derived from microsoft sample code
*/

static char *win32_cpp_id = "@(#) $Id: win32.cpp,v 1.1.1.1 2002/12/08 20:49:35 spotspot Exp $";

//----------------------------------------------------------------------------
// File: CaptureSound.cpp
//
// Desc: The CaptureSound sample shows how to use DirectSoundCapture to capture 
//       sound into a wave file 
//
// Copyright (c) 1999-2000 Microsoft Corp. All rights reserved.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// File: DirectSurfaceWrite.cpp
//
// Desc: This sample demonstrates how to animate sprites using
//       DirectDraw.  The samples runs in full-screen mode.  Pressing any
//       key will exit the sample.
//
// Copyright (c) 1999-2000 Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#define STRICT
#include <windows.h>
#include <basetsd.h>
#include <commdlg.h>
#include <mmreg.h>
#include <dxerr8.h>
#include <dsound.h>


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int sound_level = 0;
int sound_gain = 0;

int max_power;
int avg_power;
int min_power;


#define power_history_len 30
int power_history[power_history_len];
int power_index = 0;

#include "resource.h"
#include "ddutil.h"
#include "dxutil.h"

#include <direct.h>

#include "bomb.h"
#include "defs.h"
#include "sound.h"


extern "C" {
FILE *log_fp = NULL;
int frame_count = 0;
int init_time;
  int display_help = 0;
}


static DWORD start_time;

void begin_timer() {
	start_time = GetTickCount();
}
double end_timer () {
	DWORD now = GetTickCount();
	return (now - start_time)/1000.0;
}

void init_random() {
	srand(GetTickCount());
}

int mouse_x, mouse_y, mouse_down;


#define KB_SIZE 10
int key_buffer[KB_SIZE];
int kb0, kb1;
int win_getkey()
{
  int r;
  if (kb0 != kb1) {
    r = key_buffer[kb0];
    kb0 = ++kb0 % KB_SIZE;
    return r;
  }
  return 0;	
}

void
message(char *s) {
  MessageBox( NULL, s, "Bomb", MB_OK | MB_ICONERROR );
}

float m_fFPS = 0.0f;

#define help_line_length 100
char **help_text = NULL;
int help_line_count;
int help_top_line = 0;



//-----------------------------------------------------------------------------
// Function-prototypes
//-----------------------------------------------------------------------------
HRESULT InitDirectSound( GUID* pDeviceGuid );
HRESULT FreeDirectSound();

VOID    GetWaveFormat( WAVEFORMATEX* pwfx );
HRESULT CreateCaptureBuffer( WAVEFORMATEX* pwfxInput );
HRESULT InitNotifications();
HRESULT StartOrStopRecord( BOOL bStartRecording );
HRESULT RecordCapturedData();



int screen_width;
int screen_height;
int screen_bpp;



//-----------------------------------------------------------------------------
// Defines, constants, and global variables
//-----------------------------------------------------------------------------
#define NUM_REC_NOTIFICATIONS  64
#define MAX(a,b)        ( (a) > (b) ? (a) : (b) )

#define SAFE_DELETE(p)  { if(p) { delete (p);     (p)=NULL; } }
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }

LPDIRECTSOUNDCAPTURE       g_pDSCapture         = NULL;
LPDIRECTSOUNDCAPTUREBUFFER g_pDSBCapture        = NULL;
LPDIRECTSOUNDNOTIFY        g_pDSNotify          = NULL;
HINSTANCE                  g_hInst              = NULL;
GUID                       g_guidCaptureDevice  = GUID_NULL;
BOOL                       g_bRecording;
WAVEFORMATEX               g_wfxInput;
DSBPOSITIONNOTIFY          g_aPosNotify[ NUM_REC_NOTIFICATIONS + 1 ];  
HANDLE                     g_hNotificationEvent; 
BOOL                       g_abInputFormatSupported[16];
DWORD                      g_dwCaptureBufferSize;
DWORD                      g_dwNextCaptureOffset;
DWORD                      g_dwNotifySize;



CDisplay*            g_pDisplay           = NULL;
CSurface*            g_pSpriteSurface     = NULL;  
CSurface*            g_pTextSurface       = NULL;  
RECT                 g_rcViewport;          
RECT                 g_rcScreen;
BOOL                 g_bActive            = FALSE; 



//-----------------------------------------------------------------------------
// Function-prototypes
//-----------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
HRESULT WinInit( HINSTANCE hInst, int nCmdShow, HWND* phWnd, HACCEL* phAccel );
HRESULT InitDirectDraw( HWND hWnd );
HRESULT DrawSprite();
VOID    FreeDirectDraw();
HRESULT ProcessNextFrame();
HRESULT DisplayFrame();
HRESULT RestoreSurfaces();

unsigned char pixel_buf[200][320];



extern "C" {

  extern char **image_names;
  extern int image_dir_len;
  extern int bomb_exit_flag;

  void init_sound() {
	sound_present = 1;
  }

  void exit_sound() {
  }

  int get_sound() {
    return sound_level;
  }
}





//-----------------------------------------------------------------------------
// Name: WinMain()
// Desc: Entry point for the application.  Since we use a simple dialog for 
//       user interaction we don't need to pump messages.
//-----------------------------------------------------------------------------
INT APIENTRY WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR pCmdLine, 
                      INT nCmdShow )
{
  HRESULT hr;
  DWORD dwResult;
  MSG   msg;
  BOOL  bDone;
  HWND     hWnd;
  HACCEL   hAccel;

  if (enable_logging) {
    log_fp = fopen("d:\\log", "w");
    if (NULL == log_fp) {
      exit(1);
    }
    init_time = GetTickCount();
    fprintf(log_fp, "bomb log begin now=%d\n", init_time);
    fflush(log_fp);
  }


  {
    char buf[MAX_PATH];
    sprintf(buf, "%ssuck\\*", DATA_DIR);
    WIN32_FIND_DATA fd;
    HANDLE fh = FindFirstFile(buf, &fd);
    if (INVALID_HANDLE_VALUE == fh) {
      MessageBox( NULL, "Error opening suck directory.   Do not move the executable away from"
		  " its data file, or check the value of DATA_DIR in defs.h.  Exiting.", 
		  "Bomb", MB_OK | MB_ICONERROR );
      PostQuitMessage( 0 );
    }
    image_names = (char**)malloc(1);
    do {
      if ('.' != fd.cFileName[0]) {
	image_names = (char**)realloc(image_names, sizeof(char*)*(1+image_dir_len));
	image_names[image_dir_len++] = strdup(fd.cFileName);
      }
    } while (FindNextFile(fh, &fd));
    FindClose(fh);
  }


  {
    char buf[MAX_PATH];
    sprintf(buf, "%smanual.txt", DATA_DIR);
    FILE *manf = fopen(buf, "r");
    if (NULL == manf) {
      PostQuitMessage( 0 );
    }
    help_text = (char**)malloc(1);
    help_line_count = 0;
    char line_buf[help_line_length];
    while (fgets(line_buf, help_line_length, manf)) {
      line_buf[strlen(line_buf)-1] = 0;
      help_text = (char**)realloc(help_text, sizeof(char*)*(1+help_line_count));
      help_text[help_line_count++] = strdup(line_buf);
    }
    fclose(manf);
  }

  g_hInst = hInst;
  g_hNotificationEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

  // Init DirectSound
  if( FAILED( hr = InitDirectSound( &g_guidCaptureDevice ) ) )
    {
      DXTRACE_ERR( TEXT("InitDirectSound"), hr );
      MessageBox( NULL, "Error initializing DirectSound.   Exiting.", 
		  "Bomb", MB_OK | MB_ICONERROR );
      PostQuitMessage( 0 );
    }

  // set the format
  if (1) {
    ZeroMemory( &g_wfxInput, sizeof(g_wfxInput));
    g_wfxInput.wFormatTag = WAVE_FORMAT_PCM;

    GetWaveFormat( &g_wfxInput );

    if( FAILED( hr = CreateCaptureBuffer( &g_wfxInput ) ) ) {
      FreeDirectSound();
      MessageBox( NULL, "CreateCaptureBuffer failed, DirectX8 missing?", "Bomb", MB_OK | MB_ICONERROR );
      return DXTRACE_ERR( TEXT("CreateCaptureBuffer"), hr );
    }
  }


  g_bRecording = FALSE;

  if (1) {
    fb.p = &pixel_buf[0][0];
    fb.width = 320;
    fb.height = 200;
    fb.stride = 320;

    srand( GetTickCount() );

    kb1 = kb0 = 0;

    if( FAILED( WinInit( hInst, nCmdShow, &hWnd, &hAccel ) ) )
      return DXTRACE_ERR( TEXT("WinInit"), hr );
	
    bomb_init();
    if( FAILED( InitDirectDraw( hWnd ) ) )
      {
	SAFE_DELETE( g_pDisplay );

	MessageBox( hWnd, TEXT("DirectDraw init failed, exiting. "), TEXT("Bomb"), 
		    MB_ICONERROR | MB_OK );
	return DXTRACE_ERR( TEXT("InitDirectDraw"), hr );
      }
  }

  StartOrStopRecord( TRUE );

  bDone = FALSE;

  int iters = 0;
  while( !bDone && !bomb_exit_flag ) {

    // baffles me why we cannot wait for 0 ms timeout ??!!  what's
    // totally weird is that if i replace the while loop around
    // PeekMessage with a conditional, then this is making us run at
    // 50fps instead of 60. could also be the slow loop in DrawSprite
    dwResult = MsgWaitForMultipleObjects( 1, &g_hNotificationEvent, 
					  FALSE, 1, QS_ALLEVENTS );

    switch( dwResult ) {
    case WAIT_OBJECT_0 + 0:
      if( FAILED( hr = RecordCapturedData() ) )
	{
	  DXTRACE_ERR( TEXT("RecordCapturedData"), hr );
	  MessageBox( NULL, "Error handling DirectSound notifications. "
		      "Exiting.", "Bomb", 
		      MB_OK | MB_ICONERROR );
	  bDone = TRUE;
	}
      break;

    case WAIT_OBJECT_0 + 1:
      while ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ) { 

	if (WM_QUIT == msg.message) {
	  bDone = TRUE;
	  break;
	}
	TranslateMessage(&msg);
	DispatchMessage(&msg);
      }
      break;
    case WAIT_TIMEOUT:
      if (enable_logging) {
	fprintf(log_fp, "now=%9d frame=%5d sound=%5d\n", GetTickCount(), frame_count++, sound_level);
      }

      ProcessNextFrame();
      break;
    }
  }

  if (enable_logging) {
    fprintf(log_fp, "bomb log end %d %d now=%d\n", bDone, bomb_exit_flag, init_time);
    fflush(log_fp);
  }

  // Stop the capture and read any data that was not caught by a notification
  StartOrStopRecord( FALSE );

  // Clean up everything
  FreeDirectDraw();
  FreeDirectSound();

  CloseHandle( g_hNotificationEvent );

  return TRUE;
}




//-----------------------------------------------------------------------------
// Name: InitDirectSound()
// Desc: Initilizes DirectSound
//-----------------------------------------------------------------------------
HRESULT InitDirectSound( GUID* pDeviceGuid )
{
    HRESULT hr;

    ZeroMemory( &g_aPosNotify, sizeof(DSBPOSITIONNOTIFY) * 
                               (NUM_REC_NOTIFICATIONS + 1) );
    g_dwCaptureBufferSize = 0;
    g_dwNotifySize        = 0;

    // Initialize COM
    if( FAILED( hr = CoInitialize(NULL) ) )
        return DXTRACE_ERR( TEXT("CoInitialize"), hr );


    // Create IDirectSoundCapture using the preferred capture device
    if( FAILED( hr = DirectSoundCaptureCreate( NULL, &g_pDSCapture, NULL ) ) )
        return DXTRACE_ERR( TEXT("DirectSoundCaptureCreate"), hr );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: FreeDirectSound()
// Desc: Releases DirectSound 
//-----------------------------------------------------------------------------
HRESULT FreeDirectSound()
{
    // Release DirectSound interfaces
    SAFE_RELEASE( g_pDSNotify );
    SAFE_RELEASE( g_pDSBCapture );
    SAFE_RELEASE( g_pDSCapture ); 

    // Release COM
    CoUninitialize();

    return S_OK;
}

VOID GetWaveFormat( WAVEFORMATEX* pwfx )
{
    pwfx->nSamplesPerSec = 44100;
    pwfx->wBitsPerSample = 16;
    pwfx->nChannels = 1;
    pwfx->nBlockAlign = pwfx->nChannels * ( pwfx->wBitsPerSample / 8 );
    pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
}

//-----------------------------------------------------------------------------
// Name: CreateCaptureBuffer()
// Desc: Creates a capture buffer and sets the format 
//-----------------------------------------------------------------------------
HRESULT CreateCaptureBuffer( WAVEFORMATEX* pwfxInput )
{
    HRESULT hr;
    DSCBUFFERDESC dscbd;

    SAFE_RELEASE( g_pDSNotify );
    SAFE_RELEASE( g_pDSBCapture );

    // Set the notification size based on the rate;
    int sample_rate = 64;  // blue screen if not a power of 2!???
    g_dwNotifySize = MAX( 1024, pwfxInput->nAvgBytesPerSec / sample_rate );
    g_dwNotifySize -= g_dwNotifySize % pwfxInput->nBlockAlign;   

    // Set the buffer sizes 
    g_dwCaptureBufferSize = g_dwNotifySize * NUM_REC_NOTIFICATIONS;

    SAFE_RELEASE( g_pDSNotify );
    SAFE_RELEASE( g_pDSBCapture );

    // Create the capture buffer
    ZeroMemory( &dscbd, sizeof(dscbd) );
    dscbd.dwSize        = sizeof(dscbd);
    dscbd.dwBufferBytes = g_dwCaptureBufferSize;
    // maybe this would be good
    // dscbd.dwFlags = DSCBCAPS_WAVEMAPPED;
    dscbd.lpwfxFormat   = pwfxInput; // Set the format during creatation

    if( FAILED( hr = g_pDSCapture->CreateCaptureBuffer( &dscbd, 
                                                        &g_pDSBCapture, 
                                                        NULL ) ) )
        return DXTRACE_ERR( TEXT("CreateCaptureBuffer"), hr );

    g_dwNextCaptureOffset = 0;

    if( FAILED( hr = InitNotifications() ) )
        return DXTRACE_ERR( TEXT("InitNotifications"), hr );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: InitNotifications()
// Desc: Inits the notifications on the capture buffer which are handled
//       in WinMain()
//-----------------------------------------------------------------------------
HRESULT InitNotifications()
{
    HRESULT hr; 

    if( NULL == g_pDSBCapture )
        return E_FAIL;

    // Create a notification event, for when the sound stops playing
    if( FAILED( hr = g_pDSBCapture->QueryInterface( IID_IDirectSoundNotify, 
                                                    (VOID**)&g_pDSNotify ) ) )
        return DXTRACE_ERR( TEXT("QueryInterface"), hr );

    // Setup the notification positions
    for( INT i = 0; i < NUM_REC_NOTIFICATIONS; i++ )
    {
        g_aPosNotify[i].dwOffset = (g_dwNotifySize * i) + g_dwNotifySize - 1;
        g_aPosNotify[i].hEventNotify = g_hNotificationEvent;             
    }
    
    // Tell DirectSound when to notify us. the notification will come in the from 
    // of signaled events that are handled in WinMain()
    if( FAILED( hr = g_pDSNotify->SetNotificationPositions( NUM_REC_NOTIFICATIONS, 
                                                            g_aPosNotify ) ) )
        return DXTRACE_ERR( TEXT("SetNotificationPositions"), hr );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: StartOrStopRecord()
// Desc: Starts or stops the capture buffer from recording
//-----------------------------------------------------------------------------
HRESULT StartOrStopRecord( BOOL bStartRecording )
{
    HRESULT hr;

    if( bStartRecording )
    {
        // Create a capture buffer, and tell the capture 
        // buffer to start recording   
        if( FAILED( hr = CreateCaptureBuffer( &g_wfxInput ) ) )
            return DXTRACE_ERR( TEXT("CreateCaptureBuffer"), hr );

        if( FAILED( hr = g_pDSBCapture->Start( DSCBSTART_LOOPING ) ) )
            return DXTRACE_ERR( TEXT("Start"), hr );
    }
    else
    {
        // Stop the capture and read any data that 
        // was not caught by a notification
        if( NULL == g_pDSBCapture )
            return S_OK;

        // Stop the buffer, and read any data that was not 
        // caught by a notification
        if( FAILED( hr = g_pDSBCapture->Stop() ) )
            return DXTRACE_ERR( TEXT("Stop"), hr );

        if( FAILED( hr = RecordCapturedData() ) )
            return DXTRACE_ERR( TEXT("RecordCapturedData"), hr );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: RecordCapturedData()
// Desc: Copies data from the capture buffer to the output buffer 
//-----------------------------------------------------------------------------
HRESULT RecordCapturedData() 
{
  HRESULT hr;
  VOID*   pbCaptureData    = NULL;
  DWORD   dwCaptureLength;
  VOID*   pbCaptureData2   = NULL;
  DWORD   dwCaptureLength2;
  VOID*   pbPlayData       = NULL;
  DWORD   dwReadPos;
  DWORD   dwCapturePos;
  LONG lLockSize;

  if( NULL == g_pDSBCapture )
    return S_FALSE;

  if( FAILED( hr = g_pDSBCapture->GetCurrentPosition( &dwCapturePos, &dwReadPos ) ) )
    return DXTRACE_ERR( TEXT("GetCurrentPosition"), hr );

  lLockSize = dwReadPos - g_dwNextCaptureOffset;
  if( lLockSize < 0 )
    lLockSize += g_dwCaptureBufferSize;

  // Block align lock size so that we are always write on a boundary
  lLockSize -= (lLockSize % g_dwNotifySize);

  if( lLockSize == 0 ) {
    return S_FALSE;
  }

  // Lock the capture buffer down
  if( FAILED( hr = g_pDSBCapture->Lock( g_dwNextCaptureOffset, lLockSize, 
					&pbCaptureData, &dwCaptureLength, 
					&pbCaptureData2, &dwCaptureLength2, 0L ) ) ) {
    return DXTRACE_ERR( TEXT("Lock"), hr );
  }



  if (1) {
    int i;
    int current_level;
    double tot = 0.0;
    int nsamps = dwCaptureLength / 2;
    signed short *p = (signed short *) pbCaptureData;
    for (i = 0; i < nsamps; i++) {
      double t = p[i];
      tot += t * t;
    }
    current_level = (int)
      (pow(1.2, (double)sound_gain) *
       (sqrt(tot / (65536.0 * dwCaptureLength))));
	
    max_power = 0;
    min_power = current_level;
    int tot_power = 0;
    power_history[power_index++] = current_level;
    if (power_history_len == power_index) power_index = 0;
    for (i = 0; i < power_history_len; i++) {
      if (power_history[i] > max_power)
	max_power = power_history[i];
      if (power_history[i] < min_power)
	min_power = power_history[i];
      tot_power += power_history[i];
    }
    avg_power = (tot_power / power_history_len);
    sound_level = current_level - min_power;
    max_power = max_power - min_power;
  }

  // Move the capture offset along
  g_dwNextCaptureOffset += dwCaptureLength; 
  g_dwNextCaptureOffset %= g_dwCaptureBufferSize; // Circular buffer

  if( pbCaptureData2 != NULL )
    {
      // this never happens as far as i know
      // so don't bother computing sound_level.

      // Move the capture offset along
      g_dwNextCaptureOffset += dwCaptureLength2; 
      g_dwNextCaptureOffset %= g_dwCaptureBufferSize; // Circular buffer
    }

  // Unlock the capture buffer
  g_pDSBCapture->Unlock( pbCaptureData,  dwCaptureLength, 
			 pbCaptureData2, dwCaptureLength2 );


  return S_OK;
}





//-----------------------------------------------------------------------------
// Name: WinInit()
// Desc: Init the window
//-----------------------------------------------------------------------------
HRESULT WinInit( HINSTANCE hInst, int nCmdShow, HWND* phWnd, HACCEL* phAccel )
{
    WNDCLASS wc;
    HWND     hWnd;
    HACCEL   hAccel;

    // Register the Window Class
    wc.lpszClassName = TEXT("Bomb");
    wc.lpfnWndProc   = MainWndProc;
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon( hInst, MAKEINTRESOURCE(IDI_MAIN) );
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszMenuName  = NULL;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;

    if( RegisterClass( &wc ) == 0 )
        return E_FAIL;

    // Load keyboard accelerators
    hAccel = LoadAccelerators( hInst, MAKEINTRESOURCE(IDR_MAIN_ACCEL) );

    // Create and show the main window
    hWnd = CreateWindowEx( 0, TEXT("Bomb"), TEXT("Bomb"),
                           WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
  	                       CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL );
    if( hWnd == NULL )
    	return E_FAIL;

    ShowWindow( hWnd, nCmdShow );
    UpdateWindow( hWnd );

    *phWnd   = hWnd;
    *phAccel = hAccel;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: InitDirectDraw()
// Desc: Create the DirectDraw object, and init the surfaces
//-----------------------------------------------------------------------------
HRESULT InitDirectDraw( HWND hWnd )
{
  HRESULT        hr;

  g_pDisplay = new CDisplay();
  if ( !FAILED( hr = g_pDisplay->CreateFullScreenDisplay( hWnd, 320, 200, 32 ) ) ) {
    screen_width = 320;
    screen_height = 200;
	screen_bpp = 32;
  } else if ( !FAILED( hr = g_pDisplay->CreateFullScreenDisplay( hWnd, 320, 200, 24 ) ) ) {
	screen_width = 320;
	screen_height = 200;
	screen_bpp = 24;
  } else if ( !FAILED( hr = g_pDisplay->CreateFullScreenDisplay( hWnd, 640, 480, 32 ) ) ) {
    screen_width = 640;
    screen_height = 480;
	screen_bpp = 32;
  } else if ( !FAILED( hr = g_pDisplay->CreateFullScreenDisplay( hWnd, 640, 480, 24 ) ) ) {
    screen_width = 640;
    screen_height = 480;
	screen_bpp = 24;
  } else {
      MessageBox( hWnd, TEXT("This display card does not support 320x200x32 or 320x200x24 or 640x480x32 or 640x480x24. "),
		  TEXT("Bomb"), MB_ICONERROR | MB_OK );
      return hr;
  }

  // Create a DirectDrawSurface for this bitmap
  if( FAILED( hr = g_pDisplay->CreateSurface( &g_pSpriteSurface, screen_width, screen_height) ) )
    return hr;

  if( FAILED( hr = DrawSprite() ) )
    return hr;

  return S_OK;
}




//-----------------------------------------------------------------------------
// Name: DrawSprite()
// Desc: Draws a pattern of colors to a DirectDraw surface by directly writing
//       to the surface memory.  This function was designed to work only in 
//       16-bit color.
//-----------------------------------------------------------------------------
HRESULT DrawSprite()
{
  DDSURFACEDESC2 ddsd;
  HRESULT        hr;

  bomb_work();

  LPDIRECTDRAWSURFACE7 pDDS = g_pSpriteSurface->GetDDrawSurface();

  ZeroMemory( &ddsd,sizeof(ddsd) );
  ddsd.dwSize = sizeof(ddsd);

  // Lock the surface to directly write to the surface memory 
  if( FAILED( hr = pDDS->Lock( NULL, &ddsd, DDLOCK_WAIT, NULL ) ) ) {
    return hr;
  }
 
  DWORD* pDDSColor = (DWORD*) ddsd.lpSurface;

  DWORD clut[256];
  DWORD iY;
  int j = 0;
  for ( int i = 0; i < 256; i++) {
    // pixel format = XRGB (blue low bits).
    clut[i] = current_cmap[j] | (current_cmap[j+1]<<8) | (current_cmap[j+2]<<16) ;
	j += 3;
  }
  if (480 == screen_height) {
    if (32 == screen_bpp) {
      for( iY = 0; iY < 200; iY++ ) {
	DWORD *pDDSColor0 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 40) * ddsd.lPitch );
	DWORD *pDDSColor1 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 41) * ddsd.lPitch );
	unsigned char *scanin = &pixel_buf[iY][0];
	for( DWORD iX = 0; iX < 320; iX++ ) {
	  DWORD v = clut[*scanin++];
	  pDDSColor0[0] = v;
	  pDDSColor0[1] = v;
	  pDDSColor1[0] = v;
	  pDDSColor1[1] = v;
	  pDDSColor0+=2;
	  pDDSColor1+=2;
	}
      }
    } else {
      for( iY = 0; iY < 200; iY++ ) {
	double cbuf[240];
	BYTE *pDDSColor0 = (BYTE*)&cbuf[0];
	unsigned char *scanin = &pixel_buf[iY][0];
	DWORD iX;
	for( iX = 0; iX < 320; iX++ ) {
	  BYTE *v = (BYTE*)&clut[*scanin++];
	  BYTE r = v[0];
	  BYTE g = v[1];
	  BYTE b = v[2];
	  pDDSColor0[0] = r;
	  pDDSColor0[1] = g;
	  pDDSColor0[2] = b;
	  pDDSColor0[3] = r;
	  pDDSColor0[4] = g;
	  pDDSColor0[5] = b;
	  pDDSColor0+=6;
	}
	double *cbuf0 = (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 40) * ddsd.lPitch);
	double *cbuf1 = (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 41) * ddsd.lPitch);
	for ( iX = 0; iX < 240; iX++ ) {
	  cbuf0[iX] = cbuf[iX];
	  cbuf1[iX] = cbuf[iX];
	}
      }
    }
    if (32 == screen_bpp) {
      for( iY = 0; iY < 20; iY++ ) {
	DWORD *pDDSColor0 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 0) * ddsd.lPitch );
	DWORD *pDDSColor1 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 1) * ddsd.lPitch );
	DWORD *pDDSColor2 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 440) * ddsd.lPitch );
	DWORD *pDDSColor3 = (DWORD*) ( (BYTE*) ddsd.lpSurface +  (2 * iY + 441) * ddsd.lPitch );
	unsigned char *scanin0 = &pixel_buf[20-iY][0];
	unsigned char *scanin1 = &pixel_buf[198-iY][0];
	for( DWORD iX = 0; iX < 320; iX++ ) {
	  DWORD v0 = clut[*scanin0++];
	  DWORD v1 = clut[*scanin1++];
	  pDDSColor0[0] = v0;
	  pDDSColor0[1] = v0;
	  pDDSColor1[0] = v0;
	  pDDSColor1[1] = v0;
	  pDDSColor2[0] = v1;
	  pDDSColor2[1] = v1;
	  pDDSColor3[0] = v1;
	  pDDSColor3[1] = v1;
	  pDDSColor2+=2;
	  pDDSColor3+=2;
	  pDDSColor0+=2;
	  pDDSColor1+=2;
	}
      }
    } else {
      for( iY = 0; iY < 20; iY++ ) {
	double cbuf0[240];
	double cbuf1[240];
	BYTE *pDDSColor0 = (BYTE*)&cbuf0[0];
	BYTE *pDDSColor1 = (BYTE*)&cbuf1[0];
	unsigned char *scanin0 = &pixel_buf[20-iY][0];
	unsigned char *scanin1 = &pixel_buf[198-iY][0];
	DWORD iX;
	for( iX = 0; iX < 320; iX++ ) {
	  BYTE *v0 = (BYTE*) &clut[*scanin0++];
	  BYTE *v1 = (BYTE*) &clut[*scanin1++];
	  BYTE r0 = v0[0];
	  BYTE g0 = v0[1];
	  BYTE b0 = v0[2];
	  BYTE r1 = v1[0];
	  BYTE g1 = v1[1];
	  BYTE b1 = v1[2];
	  pDDSColor0[0] = r0;
	  pDDSColor0[1] = g0;
	  pDDSColor0[2] = b0;
	  pDDSColor0[3] = r0;
	  pDDSColor0[4] = g0;
	  pDDSColor0[5] = b0;

	  pDDSColor1[0] = r0;
	  pDDSColor1[1] = g0;
	  pDDSColor1[2] = b0;
	  pDDSColor1[3] = r0;
	  pDDSColor1[4] = g0;
	  pDDSColor1[5] = b0;

	  pDDSColor0+=6;
	  pDDSColor1+=6;
	}
	double *cbuf2 =  (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 0) * ddsd.lPitch);
	double *cbuf3 =  (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 1) * ddsd.lPitch);
	double *cbuf4 =  (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 440) * ddsd.lPitch);
	double *cbuf5 =  (double*)((BYTE*) ddsd.lpSurface +  (2 * iY + 441) * ddsd.lPitch);
	for( iX = 0; iX < 240; iX++ ) {
	  cbuf2[iX] = cbuf0[iX];
	  cbuf3[iX] = cbuf1[iX];
	  cbuf4[iX] = cbuf0[iX];
	  cbuf5[iX] = cbuf1[iX];
	}
      }
    }
  } else if (200 == screen_height) {

    if (32 == screen_bpp) {
      for( iY = 0; iY < 200; iY++ ) {
	DWORD *pDDSColor = (DWORD*) ( (BYTE*) ddsd.lpSurface +  iY * ddsd.lPitch );
	unsigned char *scanin = &pixel_buf[iY][0];
	for( DWORD iX = 0; iX < 320; iX++ ) {
	  *pDDSColor++ = clut[*scanin++];
	}

	// Multiply ddsd.lPitch by iY to figure out offset needed to access 
	// the next scan line on the surface. 
	pDDSColor = (DWORD*) ( (BYTE*) ddsd.lpSurface + ( iY + 1 ) * ddsd.lPitch );
      }
    } else {
      for( iY = 0; iY < 200; iY++ ) {
	BYTE *pDDSColor;
	unsigned char *scanin = &pixel_buf[iY][0];
	double cbuf[120];
	int iX;
	pDDSColor = (BYTE *)&cbuf[0];
	for( iX = 0; iX < 320; iX++ ) {
	  BYTE *v = (BYTE*) &clut[*scanin++];
	  BYTE r = v[0];
	  BYTE g = v[1];
	  BYTE b = v[2];
	  pDDSColor[0] = r;
	  pDDSColor[1] = g;
	  pDDSColor[2] = b;
	  pDDSColor += 3;
	}
	double *cbuf1 = (double*) ((BYTE*)ddsd.lpSurface +  iY * ddsd.lPitch);
	for ( iX = 0; iX < 120 ; iX++ ) {
	  cbuf1[iX] = cbuf[iX];
	}

	// Multiply ddsd.lPitch by iY to figure out offset needed to access 
	// the next scan line on the surface. 
	pDDSColor = (BYTE*) ddsd.lpSurface + ( iY + 1 ) * ddsd.lPitch;
      }
    }
  }

  pDDS->Unlock(NULL); 

  return S_OK;
}




//-----------------------------------------------------------------------------
// Name: FreeDirectDraw()
// Desc: Release all the DirectDraw objects
//-----------------------------------------------------------------------------
VOID FreeDirectDraw()
{
    SAFE_DELETE( g_pSpriteSurface );
    SAFE_DELETE( g_pDisplay );
}




//-----------------------------------------------------------------------------
// Name: MainWndProc()
// Desc: The main window procedure
//-----------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch (msg) {
  case WM_COMMAND:
    switch( LOWORD(wParam) ) {
    case IDM_EXIT:
      // Received key/menu command to exit app
      PostMessage( hWnd, WM_CLOSE, 0, 0 );
      return 0L;
    }
    break; // Continue with default processing
			
  case WM_MOUSEMOVE:
    mouse_down = 1 && (MK_LBUTTON & wParam);
    mouse_x = (LOWORD(lParam));
    mouse_y = (HIWORD(lParam));
    return 0L;

  case WM_CHAR:
	  {
    int key = (int)wParam;
    if ('?' == key) {
      display_help = !display_help;
    } else {
      key_buffer[kb1] = key;
      kb1 = ++kb1 % KB_SIZE;
      // if the buffer is full, throw out the oldest char
      if (kb0 == kb1)
	kb0 = ++kb0 % KB_SIZE;
    }
	  }
    return 0L;

  case WM_KEYDOWN:
    if (VK_HELP == wParam || VK_F1 == wParam) {
      display_help = !display_help;
    } else if (display_help) {
      switch (wParam) {
      case VK_PRIOR:
	help_top_line-=9;
	break;
      case VK_NEXT:
	help_top_line+=9;
	break;
      case VK_UP:
	help_top_line--;
	break;
      case VK_DOWN:
	help_top_line++;
	break;
      }
      if (help_top_line < 0) help_top_line = 0;
      if (help_top_line > (help_line_count-5)) help_top_line = (help_line_count-5);
      
    } else {
      switch (wParam) { 
      case VK_UP: 
	sound_gain++;
	break; 
      case VK_DOWN: 
	sound_gain--;
	break;
      }
    }
    break;

  case WM_SETCURSOR:
    // Hide the cursor in fullscreen 
    SetCursor( NULL );
    return TRUE;

  case WM_SIZE:
    // Check to see if we are losing our window...
    if( SIZE_MAXHIDE==wParam || SIZE_MINIMIZED==wParam )
      g_bActive = FALSE;
    else
      g_bActive = TRUE;
    break;

  case WM_SYSCOMMAND:
    // Prevent moving/sizing and power loss in fullscreen mode
    switch( wParam )
      {
      case SC_MOVE:
      case SC_SIZE:
      case SC_MAXIMIZE:
      case SC_MONITORPOWER:
	return TRUE;
      }
    break;
            
  case WM_DESTROY:
    // Cleanup and close the app
    PostQuitMessage( 0 );
    return 0L;
  }

  return DefWindowProc(hWnd, msg, wParam, lParam);
}




//-----------------------------------------------------------------------------
// Name: ProcessNextFrame()
// Desc: Move the sprites, blt them to the back buffer, then 
//       flips the back buffer to the primary buffer
//-----------------------------------------------------------------------------
HRESULT ProcessNextFrame()
{
  HRESULT hr;

  // Display the sprites on the screen
  if( FAILED( hr = DisplayFrame() ) )
    {
      if( hr != DDERR_SURFACELOST ) {

	return hr;
      }

      // The surfaces were lost so restore them 
      RestoreSurfaces();
    }

  return S_OK;
}




HRESULT
DrawText(char *text, int line) {
  HRESULT hr;
  char *s, c;
  int h = 0;
  s = text;
  while (c = *s++) {
    if (c == ' ') {
      h += 7;
      continue;
    } else if (c == '\t') {
      h += 56;
      continue;
    } else
      break;
  }
  s--;
  if( FAILED( hr = g_pDisplay->CreateSurfaceFromText( &g_pTextSurface, NULL, s, 
						      RGB(0,0,0), RGB(255, 255, 0) ) ) )
    return hr;
  g_pDisplay->Blt( 10+h, line*16, g_pTextSurface, NULL );

  SAFE_DELETE( g_pTextSurface );
  return S_OK;
}

//-----------------------------------------------------------------------------
// Name: DisplayFrame()
// Desc: Blts a the sprites to the back buffer, then flips the 
//       back buffer onto the primary buffer.
//-----------------------------------------------------------------------------
HRESULT DisplayFrame()
{
  HRESULT hr;

  if (FAILED(hr = DrawSprite())) {
    return hr;
  }

  // Fill the back buffer with black, ignoring errors until the flip
  g_pDisplay->Clear( 0 );
  g_pDisplay->Blt( 0, 0, g_pSpriteSurface, NULL );

  if (display_fps) {
    static FLOAT fLastTime = 0.0f;
    static DWORD dwFrames  = 0L;

    FLOAT fTime = DXUtil_Timer( TIMER_GETABSOLUTETIME );
    ++dwFrames;

    // Update the scene stats once per second
    if( fTime - fLastTime > 1.0f )
      {
	m_fFPS    = dwFrames / (fTime - fLastTime);
	fLastTime = fTime;
	dwFrames  = 0L;
      }

    char buf[100];
    char *notes = (200 == screen_height) ? "" : " (incompatible graphics card)";
    
    sprintf(buf, "%03d %03d %03d %9.4g fps%s",
	    sound_level, max_power, min_power, (double)m_fFPS, notes);

    DrawText(buf, 0);
  }
  
  if (display_help) {
    int lines_per_page = (480 == screen_height) ? 24 : 10;
    for (int i = 0; i < lines_per_page; i++) {
      int line = help_top_line + i;
      if (line < 0 || line >= help_line_count)
	continue;
      DrawText(help_text[line], i+2);
    }
  }    

  // We are in fullscreen mode, so perform a flip and return 
  // any errors like DDERR_SURFACELOST
  if( FAILED( hr = g_pDisplay->Present() ) ) {
    return hr;
  }

  return S_OK;
}




//-----------------------------------------------------------------------------
// Name: RestoreSurfaces()
// Desc: Restore all the surfaces, and redraw the sprite surfaces.
//-----------------------------------------------------------------------------
HRESULT RestoreSurfaces()
{
  HRESULT hr;

    if (enable_logging) {
  fprintf(log_fp, "rs\n");
  fflush(log_fp);
    }

  if (DD_OK != g_pDisplay->GetDirectDraw()->TestCooperativeLevel()) {
    if (enable_logging) {
    fprintf(log_fp, "not ready yet\n");
    fflush(log_fp);
    }
    return S_OK;
  }
    

  if( FAILED( hr = g_pDisplay->GetDirectDraw()->RestoreAllSurfaces() ) ) {
    if (enable_logging) {
    fprintf(log_fp, "e0\n");
    fflush(log_fp);
    }
    return hr;
  }

  // No need to re-create the surface, just re-draw it.
  if( FAILED( hr = DrawSprite() ) ) {
    if (enable_logging) {
      fprintf(log_fp, "e2\n");
      fflush(log_fp);
    }
    return hr;
  }

  return S_OK;
}
