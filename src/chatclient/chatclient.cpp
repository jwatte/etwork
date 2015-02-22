
//  chatclient.cpp
//  This file is an example of how to write a client program 
//  using the Etwork networking message API.
//  It is intended to communicate with a server running 
//  the "chatserver.cpp/chathost.cpp" code, over TCP or UDP.

#include "etwork/etwork.h"
#include "etwork/timer.h"
#include "etwork/errors.h"
#include "resource.h"

#include "../chatserver/chathost.h"   //  for the message constants

#include <string>
#include <deque>
#include <algorithm>

//  Some day, I'll move the chat user interface to a header of 
//  its own, rather than mash it together with the Win32 code 
//  in this file.
//  This is not a public interface, so using STL in it is OK.
class IChatUser {
  public:
    //  Shut down the connection to the chat server.
    virtual void dispose() = 0;
    //  Service the server connection. Return text that has 
    //  arrived from the server.
    virtual bool poll( double time, std::deque< std::string > & text ) = 0;
    //  Send text to the server as chat.
    virtual void sendText( std::string const & text ) = 0;
};

namespace impl {
  //  Implement the ChatUser protocol, to manage the connection 
  //  to a single chat server.
  class ChatUser : public IChatUser {
    public:
      //  because this is an implementation, intended to live in a CPP file 
      //  (where users won't see it; they'll only see the IChatUser interface), 
      //  I can make all my members public.
      ChatUser();
      ~ChatUser();
      bool open( short port, char const * host, char const * name, bool reliable );
      void sendLogin();

      //  Implement IChatUser
      virtual void dispose() { delete this; }
      virtual bool poll( double time, std::deque< std::string > & text );
      virtual void sendText( std::string const & text );

      ISocketManager * mgr_;    //  the networking system
      ISocket * socket_;        //  the specific connection
      double lastIdle_;         //  when I last got an idle message
      etwork::Timer time_;      //  the time base I'm using
      std::string name_;        //  the user name I'm using
      bool loggedIn_;           //  whether I'm logged in
  };
}
using namespace impl;

//  clear out internal state
ChatUser::ChatUser()
{
  mgr_ = 0;
  socket_ = 0;
  lastIdle_ = 0;
  loggedIn_ = false;
}

ChatUser::~ChatUser()
{
  //  close the connection
  if( socket_ ) {
    socket_->dispose();
  }
  //  close the network instance
  if( mgr_ ) {
    mgr_->dispose();
  }
}

//  Open the connection (if possible)
bool ChatUser::open( short port, char const * host, char const * name, bool reliable )
{
  if( !mgr_ ) {
    EtworkSettings settings;
    //  set up some settings
    settings.reliable = reliable;
    settings.accepting = false;   //  I don't want to accept incoming connections
    settings.port = 0;            //  Use any port -- I'm a client.
    settings.debug = true;
    settings.keepalive = 4.5;
    settings.timeout = 20;
    mgr_ = CreateEtwork( &settings );
    if( !mgr_ ) {
      return false;
    }
  }
  //  Can't open() when I'm already open.
  if( socket_ ) {
    return false;
  }
  //  create the socket, connecting it to the server (this is blocking)
  if( !mgr_->connect( host, port, &socket_ ) || socket_ == 0 ) {
    return false;
  }
  //  set up the client name (limiting the length)
  int len = (int)strlen( name );
  if( len > 31 ) {
    len = 31;
  }
  name_ = name;
  //  send the login packet
  sendLogin();
  return true;
}

void ChatUser::sendLogin()
{
  //  Format of login packet: msgCode + name
  unsigned char buf[200];
  buf[0] = MsgLogin;
  memcpy( &buf[1], name_.c_str(), name_.length() );
  socket_->write( buf, name_.length()+1 );
  double now = time_.seconds();
  //  last time I did something was now!
  lastIdle_ = now;
}

//  I'm given time to do what needs to be done.
bool ChatUser::poll( double time, std::deque< std::string > & text )
{
  double now = time_.seconds();
  ISocket * active[10];
  mgr_->poll( time, active, 10 );
  //  I'll always check the socket, even if poll() doesn't claim activity.
  if( socket_->closed() ) {
    text.push_back( "Server closed connection." );
    return false;
  }
  unsigned char buf[2000];
  int r;
  //  read out each message from the socket.
  //  The value "2000" is intended to be bigger than the actual 
  //  max message size used by the connection.
  while( (r = socket_->read( buf, 2000 )) > 0 ) {
    if( r == 0 ) {
      continue; //  a keepalive
    }
    //  deal with each kind of message I might get on the connection
    switch( buf[0] ) {
    
      case MsgTextFromServer: {
        //  this is some text to display.
        text.push_back( std::string( (char const *)&buf[1], r-1 ) );
        //  additionally, I know server will only send text to me 
        //  if I'm logged in -- so make sure I know that!
        loggedIn_ = true;
      }
      break;
      
      case MsgLoginAccepted: {
        //  This is acknowledgement of my existence.
        loggedIn_ = true;
      }
      break;
      
      case MsgNoLogin: {
        //  Oops! I couldn't log in.
        text.push_back( "Login rejected. Try again." );
      }
      break;
    }
  }
  //  Every so often, try again.
  if( lastIdle_ < now-10 ) {
    if( !loggedIn_ ) {
      //  If I'm not logged in, try again.
      text.push_back( "Attempting re-login." );
      sendLogin();
    }
    else {
      //  Send a keepalive, to keep the server from kicking me.
      //  Etwork could do this automatically, by setting "keepalive" to some value
      //  in the settings struct.
      socket_->write( "", 0 );
    }
    lastIdle_ = now;
  }
  return true;
}

//  Send text to the server as a chat message.
void ChatUser::sendText( std::string const & text )
{
  //  I don't actually test whether I'm logged in.
  unsigned char buf[ 2000 ];
  strncpy( (char *)&buf[1], text.c_str(), 1999 );
  buf[1999] = 0;
  buf[0] = MsgTextToServer;
  //  just send the message
  socket_->write( buf, text.length()+1 );
  lastIdle_ = time_.seconds();
}


//  Create a connection to a specific chat server.
IChatUser * NewChatUser( short port, char const * host, char const * name, bool reliable )
{
  ChatUser * cc = new ChatUser();
  if( !cc->open( port, host, name, reliable ) ) {
    cc->dispose();
    return 0;
  }
  return cc;
}


//  globals for the Win32 part of the chat client (UI, command line)
HINSTANCE appInstance_;
bool dialogUdp = false;
short dialogPort = 11001;
char dialogHost[128] = "127.0.0.1";
char userName[32] = "l-class user";
IChatUser * chatUser_;

//  convenient error display function
static void error( char const * str )
{
  ::MessageBox( 0, str, "Chatclient Error", MB_OK );
  exit( 1 );
}

//  Simple wrapper to run a dialog.
INT_PTR RunDialog( int dlgId, DLGPROC proc )
{
  return DialogBox( appInstance_, MAKEINTRESOURCE(dlgId), 0, proc );
}

//  Another global for what the chat window is 
//  (used during display of error messages).
static HWND chatWindow;

class RuntimeNotify : public IErrorNotify {
  public:
    //  Etwork will call me back with a socket error message, 
    //  because I set a socket error handler.
    virtual void onSocketError( ErrorInfo const & info ) {
      ::SendMessage( ::GetDlgItem( chatWindow, IDC_CHATLOG ), 
          LB_ADDSTRING, 0, (LPARAM)info.error.c_str() );
    }
};
RuntimeNotify runtimeNotify;

//  When connectd, I use a dialog with the ChatUserProc dialog proc.
BOOL CALLBACK ChatUserProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
  switch( message ) {

    case WM_INITDIALOG: {
      // Fill out the dialog with appropriate data.
      chatWindow = hwnd;                      //  remember the specific dialog handle
      ::SetTimer( hwnd, 0, 100, 0 );          //  make sure we get called every so often
      ::SendMessage( hwnd, WM_SETTEXT, 0, (LPARAM)userName ); //  change title of dialog
      SetEtworkErrorNotify( &runtimeNotify ); //  tell Etwork to funnel errors to me.
    }
    return true;

    case WM_TIMER: {
      //  it's time to see whether there's something to do
      std::deque< std::string > input;
      if( !chatUser_->poll( 0.01, input ) ) {   //  if poll fails, go away
        ::EndDialog( hwnd, 0 );
      }
      //  put the received messages into the text list
      HWND list = ::GetDlgItem( hwnd, IDC_CHATLOG );
      for( std::deque< std::string >::iterator ptr = input.begin(), end = input.end();
          ptr != end; ++ptr ) {
        ::SendMessage( list, LB_ADDSTRING, 0, (LPARAM)(*ptr).c_str() );
      }
      //  prune scroll-back if necessary (to avoid a bloated list)
      INT_PTR cnt = ::SendMessage( list, LB_GETCOUNT, 0, 0 );
      if( cnt > 105 ) { //  do some hysteresis
        while( cnt > 100 ) {
          ::SendMessage( list, LB_DELETESTRING, 0, 0 );
          --cnt;
        }
      }
    }
    return true;

    case WM_COMMAND: {
      //  button press -- which button?
      switch( LOWORD( wparam ) ) {

        case IDOK: {
          //  OK means "send"
          char msg[200];
          //  extract the text (and clear the input control)
          ::GetDlgItemText( hwnd, IDC_TEXT, msg, 200 );
          ::SetDlgItemText( hwnd, IDC_TEXT, "" );
          msg[199] = 0;
          //  send it to the server
          chatUser_->sendText( msg );
        }
        break;

        case IDCANCEL: {
          //  cancel means "quit"
          ::EndDialog( hwnd, 0 );
        }
        break;

      }
    }
    break;

  }
  return false;
}

//  When figuring out where to connect to, the dialog uses this dialog proc
BOOL CALLBACK ConnectProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
  switch( message ) {

    case WM_INITDIALOG: {
      //  fill out the fields with the defaults from globals (command line)
      ::SetDlgItemText( hwnd, IDC_HOSTNAME, dialogHost );
      ::SetDlgItemText( hwnd, IDC_NAME, userName );
      ::SetDlgItemInt( hwnd, IDC_PORTNUM, dialogPort, false );
      ::CheckDlgButton( hwnd, IDC_UDP, dialogUdp );
    }
    return true;

    case WM_COMMAND: {
      //  button press
      switch( LOWORD( wparam ) ) {

        case IDOK: {
          //  extract the values
          ::GetDlgItemText( hwnd, IDC_HOSTNAME, dialogHost, sizeof(dialogHost) );
          dialogHost[sizeof(dialogHost)-1] = 0;
          ::GetDlgItemText( hwnd, IDC_NAME, userName, sizeof(userName) );
          userName[sizeof(userName)-1] = 0;
          dialogPort = ::GetDlgItemInt( hwnd, IDC_PORTNUM, 0, false );
          dialogUdp = ::IsDlgButtonChecked( hwnd, IDC_UDP ) != FALSE;
          ::EndDialog( hwnd, dialogPort );
        }
        break;

        case IDCANCEL: {
          //  changed my mind -- go away
          ::EndDialog( hwnd, 0 );
        }
        break;

      }
    }
    return true;

  }
  return false;
}

class ErrorNotify : public IErrorNotify {
  public:
    //  Etwork will call me back with errors during set-up
    //  (until the next dialog has finished creating).
    void onSocketError( const ErrorInfo & info ) {
      if( info.error.severity() > ES_warning ) {
        error( info.error.c_str() );
      }
    }
};
ErrorNotify baseNotify;

int __stdcall WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
  //  Make sure I hear about all Etwork errors.
  SetEtworkErrorNotify( &baseNotify );
  //  save off global arguments and parse command line
  appInstance_ = hInstance;
  int port = 0;
  int gotNum = 0;   //  count the number of command line arguments that matter
  //  connect on a port?
  char const * param = strstr( lpCmdLine, "port=" );
  if( param ) {
    dialogPort = atoi( param+5 );
    ++gotNum;
  }
  char const * host = 0;
  char const * end = 0;
  //  connect to a host?
  param = strstr( lpCmdLine, "host=" );
  if( param ) {
    host = param+5;
    end = strchr( host, ' ' );
    if( !end ) end = host+strlen(host);
    if( end > host+sizeof(dialogHost)-1 ) {
      end = host+sizeof(dialogHost)-1;
    }
    strncpy( dialogHost, host, end-host );
    dialogHost[end-host] = 0;
    ++gotNum;
  }
  char const * user = 0;
  //  use a specific user name?
  param = strstr( lpCmdLine, "user=" );
  if( param ) {
    user = param+5;
    end = strchr( user, ' ' );
    if( !end ) end = user+strlen(user);
    if( end > user+sizeof(userName)-1 ) {
      end = user+sizeof(userName)-1;
    }
    strncpy( userName, user, end-host );
    userName[end-user] = 0;
    ++gotNum;
  }
  //  use UDP or TCP?
  char const * udp = strstr( lpCmdLine, "udp=" );
  if( udp ) {
    dialogUdp = atoi( udp+4 ) != 0;
    //  this doesn't count as a gotNum, because I can connect anyway
  }
  if( gotNum != 3 ) {
    //  if I didn't get port, host and username, ask the user with a dialog
    port = (int)RunDialog( IDD_CONNECT, ConnectProc );
    if( port < 1 || port > 65535 || dialogHost[0] == 0 || userName[0] == 0 ) {
      error( "Port value must be between 1 and 65535 (inclusive).\n" 
          "Host name must not be empty.\n"
          "User name must not be empty." );
    }
  }
  //  create the connection to the server
  chatUser_ = NewChatUser( (short)dialogPort, dialogHost, userName, !dialogUdp );
  if( !chatUser_ ) {
    error( "Could not connect to chat server on the selected address." );
  }
  //  run the "connected" dialog with the chat list box; 
  //  this will actually service the network connection.
  RunDialog( IDD_CHATUSER, ChatUserProc );
  chatUser_->dispose();
  return 0;
}

