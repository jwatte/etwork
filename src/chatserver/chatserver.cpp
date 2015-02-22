//  chatserver.cpp
//  The Win32 GUI for the chat server sample.
//  This file runs the dialogs, and defers to the chathost.cpp 
//  file for networking and user management.

#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <string>

#include "etwork/etwork.h"
#include "etwork/timer.h"
#include "resource.h"
#include "chathost.h"



//  Always a good idea to remember your hinstance.
HINSTANCE appInstance_;
//  The chathost is global (I'm only serving one logical server)
IChatHost * chatHost_;
//  A dialog parameter, because Windows dialogs are easiest with globals.
bool dialogUdp;

//  Some error happened -- display to the user and exit.
static void error( char const * str )
{
  ::MessageBox( 0, str, "Chatserver Error", MB_OK );
  exit( 1 );
}

//  A simple wrapper for running as specified dialog as modal, 
//  and returning the result.
INT_PTR RunDialog( int dlgId, DLGPROC proc )
{
  return DialogBox( appInstance_, MAKEINTRESOURCE(dlgId), 0, proc );
}

//  DialogProc used for the setup dialog (specify port, etc).
BOOL CALLBACK HostDialogProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
  switch( message ) {

    //  Set up the dialog -- fill in control values.
    case WM_INITDIALOG: {
      RECT r;
      ::GetWindowRect( hwnd, &r );
      ::SetWindowPos( hwnd, HWND_TOP, r.left+300, r.top+100, r.right-r.left, r.bottom-r.top, 0 );
      ::SetDlgItemText( hwnd, IDC_PORTNUM, "11001" );
      ::CheckDlgButton( hwnd, IDC_UDP, dialogUdp );
    }
    break;

    //  Some dialog button was activated.
    case WM_COMMAND: {

      switch( LOWORD( wparam ) ) {

        //  OK means we close the dialog, returning the port number 
        //  and extracting the other settings.
        case IDOK: {
          wparam = GetDlgItemInt( hwnd, IDC_PORTNUM, 0, TRUE );
          dialogUdp = ::IsDlgButtonChecked( hwnd, IDC_UDP ) != FALSE;
          ::EndDialog( hwnd, wparam );
        }
        break;

        //  CANCEL just means go away.
        case IDCANCEL: {
          ::EndDialog( hwnd, 0 );
        }
        break;

        default:
        break;
      }
    }
    break;

    default:
    break;
  }
  return FALSE;
}

//  The chathost uses a generation count to tell us about 
//  changes to the make-up of the user list. When it has 
//  changed, I detect it by comparing to the last value.
static size_t lastGeneration_;

//  Timer to measure the passage of time.
etwork::Timer timer_;

//  lastTime_ is used for re-generating the user list 
//  every so often, even if nobody logged in/out.
static double lastTime_;


//  UserListProc is the dialog proc running the actual 
//  hosting dialog (which displays connected users). The 
//  networking is run as a WM_TIMER callback from within 
//  this dialog.
BOOL CALLBACK UserListProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
  switch( message ) {

    //  Set up parameters in the dialog.
    case WM_INITDIALOG: {
      RECT r;
      //  Move the dialog out.
      ::GetWindowRect( hwnd, &r );
      ::SetWindowPos( hwnd, HWND_TOP, r.left+250, r.top+100, r.right-r.left, r.bottom-r.top, 0 );
      //  Queue a WM_TIMER that lets us run the network about every 100 ms.
      ::SetTimer( hwnd, 0, 100, 0 );
    }
    break;

    //  Time has come to actually poll the network.
    case WM_TIMER: {
      //  Let the chat host do its thing.
      chatHost_->poll( 0.01 );
      //  If it's time to re-do the user list (some user connected, or 
      //  it's just been a while), then do it.
      if( chatHost_->clientGeneration() != lastGeneration_ ||
        (lastTime_ < timer_.seconds()-10) ) {
        //  Remember our current settings, so we can detect changes again later.
        lastTime_ = timer_.seconds();
        lastGeneration_ = chatHost_->clientGeneration();
        //  Re-build the list.
        size_t numClients = chatHost_->countClients();
        HWND list = ::GetDlgItem( hwnd, IDC_USERLIST );
        LRESULT cnt = ::SendMessage( list, LB_GETCOUNT, 0, 0 );
        //  Remove all current items.
        ::SendMessage( list, LB_RESETCONTENT, 0, 0 );
        //  Configure tab stops, to allow nice columns in the list.
        int tabstops[4] = { 60, 140, 200, 220 };
        ::SendMessage( list, LB_SETTABSTOPS, 4, (LPARAM)tabstops );
        //  Add items to the list box for each user.
        for( size_t i = 0; i < numClients; ++i ) {
          ChatClient info;
          chatHost_->getClient( i, &info );
          char buf[200];
          _snprintf( buf, 200, "%s\t%s\t%d\t%f",
              info.name, info.address, info.numMessages, info.lastReceive );
          buf[199] = 0;
          LRESULT lr = ::SendMessage( list, LB_ADDSTRING, 0, (LPARAM)buf );
          //  lr is the index at which the item was added
          assert( lr >= 0 );
#if !defined( NDEBUG )
          std::string s( "client: " );
          char x[20];
          s += buf;
          s += " at index ";
          s += itoa( (int)lr, x, 10 );
          s += "\n";
          OutputDebugString( s.c_str() );
#endif
        }
      }
    }
    break;

    //  Some button was activated.
    case WM_COMMAND: {

      switch( LOWORD( wparam ) ) {

        //  Kick the selected user (if any selected).
        case IDC_KICK: {
          HWND list = ::GetDlgItem( hwnd, IDC_USERLIST );
          int item = (int)::SendMessage( list, LB_GETCURSEL, 0, 0 );
          //  Is there a selected item?
          if( item != LB_ERR ) {
            //  Extract the name of the selected user
            int len = (int)::SendMessage( list, LB_GETTEXTLEN, 0, 0 );
            char * c = new char[ len+1 ];
            ::SendMessage( ::GetDlgItem( hwnd, IDC_USERLIST ), LB_GETTEXT, item, (LPARAM)c );
            c[len+1] = 0;
            //  name is the first text, terminated by tab.
            char * p = strchr( c, '\t' );
            if( p ) {
              *p = 0;
            }
            //  Let the chat host deal with kicking the user.
            chatHost_->kickUser( c );
            delete[] c;
          }
        }
        break;

        //  OK is the QUit button.
        case IDOK: {
          ::EndDialog( hwnd, wparam );
        }
        return TRUE;

        default:
        break;
      }
    }
    break;

    default:
    break;
  }
  return FALSE;
}


//  WinMain:
//  Extract command line parameters.
//  Run the configuration dialog box if necessary.
//  Report errors and quit, or
//  Run the main dialog that deals with hosting.
int __stdcall WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
  appInstance_ = hInstance;
  int port = 0;
  //  udp=1 for using unreliable transport
  char const * udp = strstr( lpCmdLine, "udp=" );
  if( udp ) {
    dialogUdp = atoi( udp+4 ) != 0;
  }
  //  port=1234 to serve on a specific port
  char const * param = strstr( lpCmdLine, "port=" );
  if( param ) {
    port = atoi( param+5 );
  }
  else {
    //  If the port wasn't specified, run the options dialog.
    port = (int)RunDialog( IDD_HOSTDIALOG, HostDialogProc );
    if( port == 0 ) {
      return 1;
    }
  }
  if( port < 1 || port > 65535 ) {
    error( "Port value must be between 1 and 65535 (inclusive)." );
  }
  //  chatHost_ deals with all the networking and user management.
  chatHost_ = NewChatHost( (short)port, !dialogUdp );
  if( !chatHost_ ) {
    error( "Could not host chat server on the selected port." );
  }
  //  The modal dialog actually runs most of the program.
  RunDialog( IDD_USERLIST, UserListProc );
  //  Clean up and go home. (Too much clean-up is harmful, as the 
  //  kernel will do a good job of releasing all resources anyway)
  chatHost_->dispose();
  return 0;
}

