
#include "sockimpl.h"
#include <stdarg.h>

using namespace etwork;
using namespace etwork::impl;

#if !defined( NDEBUG )
bool etwork::impl::gDebugging = true;
#else
bool etwork::impl::gDebugging = false;
#endif

bool etwork::impl::wsOpen = false;

Lock etwork::impl::gethostLock; //!< gethostbyname is not thread safe

IErrorNotify * etwork::impl::gErrorNotify;

void SetEtworkErrorNotify( IErrorNotify * notify )
{
  gErrorNotify = notify;
}


static EtworkError GetWsaError( int wsaError, ErrorArea area )
{
  ErrorOption eo = EO_unknown_error;
  ErrorSeverity es = ES_error;
  switch( wsaError ) {
  case WSAEINTR:
    break;
  case WSAEACCES:         eo = EO_already_in_use;
    break;
  case WSAEFAULT:         eo = EO_invalid_parameters;
    break;
  case WSAEINVAL:         eo = EO_invalid_parameters;
    break;
  case WSAEMFILE:         eo = EO_out_of_resources; es = ES_catastrophe;
    break;
  case WSAEWOULDBLOCK:    es = ES_warning;
    break;
  case WSAEINPROGRESS:    es = ES_warning;
    break;
  case WSAEALREADY:       eo = EO_invalid_parameters;
    break;
  case WSAENOTSOCK:       eo = EO_invalid_parameters;
    break;
  case WSAEDESTADDRREQ:   eo = EO_invalid_parameters; es = ES_catastrophe;
    break;
  case WSAEMSGSIZE:       eo = EO_out_of_resources; es = ES_warning;
    break;
  case WSAEPROTOTYPE:     eo = EO_invalid_parameters;
    break;
  case WSAENOPROTOOPT:    eo = EO_invalid_parameters;
    break;
  case WSAEPROTONOSUPPORT:  eo = EO_unsupported_platform; es = ES_catastrophe;
    break;
  case WSAESOCKTNOSUPPORT:  eo = EO_unsupported_platform; es = ES_catastrophe;
    break;
  case WSAEOPNOTSUPP:     eo = EO_invalid_parameters;
    break;
  case WSAEPFNOSUPPORT:   eo = EO_unsupported_platform; es = ES_catastrophe;
    break;
  case WSAEAFNOSUPPORT:   eo = EO_unsupported_platform; es = ES_catastrophe;
    break;
  case WSAEADDRINUSE:     eo = EO_already_in_use;
    break;
  case WSAEADDRNOTAVAIL:  eo = EO_invalid_parameters;
    break;
  case WSAENETDOWN:       eo = EO_out_of_resources;
    break;
  case WSAENETUNREACH:    eo = EO_bad_address;
    break;
  case WSAENETRESET:      eo = EO_out_of_resources;
    break;
  case WSAECONNABORTED:   eo = EO_peer_timeout;
    break;
  case WSAECONNRESET:     eo = EO_peer_dropped;
    break;
  case WSAENOBUFS:        eo = EO_out_of_resources;
    break;
  case WSAEISCONN:        eo = EO_invalid_parameters; es = ES_catastrophe;
    break;
  case WSAENOTCONN:       eo = EO_invalid_parameters; es = ES_catastrophe;
    break;
  case WSAESHUTDOWN:      eo = EO_invalid_parameters;
    break;
  case WSAETIMEDOUT:      eo = EO_peer_timeout;
    break;
  case WSAECONNREFUSED:   eo = EO_peer_refused;
    break;
  case WSAEHOSTDOWN:      eo = EO_peer_timeout;
    break;
  case WSAEHOSTUNREACH:   eo = EO_bad_address;
    break;
  case WSAEPROCLIM:       eo = EO_out_of_resources; es = ES_catastrophe;
    break;
  case WSASYSNOTREADY:    eo = EO_unsupported_platform; es = ES_catastrophe;
    break;
  case WSAVERNOTSUPPORTED:  eo = EO_unsupported_version; es = ES_catastrophe;
    break;
  case WSANOTINITIALISED: es = ES_catastrophe;
    break;
  case WSAEDISCON:        eo = EO_peer_dropped;
    break;
  case WSATYPE_NOT_FOUND:
    break;
  case WSAHOST_NOT_FOUND: eo = EO_bad_address;
    break;
  case WSATRY_AGAIN:      eo = EO_bad_address;
    break;
  case WSANO_RECOVERY:    eo = EO_bad_address;
    break;
  case WSANO_DATA:        eo = EO_bad_address;
    break;
  case WSA_INVALID_HANDLE:  eo = EO_invalid_parameters; es = ES_catastrophe;
    break;
  case WSA_INVALID_PARAMETER: eo = EO_invalid_parameters; es = ES_catastrophe;
    break;
  case WSA_IO_INCOMPLETE:
    break;
  case WSA_IO_PENDING:
    break;
  case WSA_NOT_ENOUGH_MEMORY: eo = EO_out_of_resources;
    break;
  case WSA_OPERATION_ABORTED: eo = EO_peer_dropped;
    break;
  case WSASYSCALLFAILURE:
    break;
  default:
    {
      char buf[1024];
      _snprintf( buf, 1024, "Unknown system error code in GetWsaError(): %d\n", wsaError );
      buf[1023] = 0;
      OutputDebugString( buf );
    }
    break;
  }

  return EtworkError( es, area, eo );
}


std::string etwork::impl::get_error_string( int err )
{
  std::string error_;
  if( err == 0 ) {
    error_ = "";
  }
  else {
    char buf[2048];
    int l = ::FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM, 0, err, 0, buf, 2048, 0 );
    if( l < 0 || l > 2047 ) {
      l = 2047;
    }
    buf[l] = 0;
    error_ = buf;
    size_t pos;
    while( (pos = error_.find( '\n' )) != std::string::npos ) {
      error_.erase( pos, 1 );
    }
    while( (pos = error_.find( '\r' )) != std::string::npos ) {
      error_.erase( pos, 1 );
    }
    while( (pos = error_.find( '.' )) != std::string::npos ) {
      error_.erase( pos, 1 );
    }
  }
  return error_;
}


void etwork::impl::assert_failure( char const * expr, char const * file, int line )
{
  char str[2048];
  _snprintf( str, 2048, "Etwork: %s(%d): Assertion Failed: %s\n", 
      file, line, expr );
  str[2047] = 0;
  if( gErrorNotify ) {
    ErrorInfo info;
    info.error = EtworkError( ES_internal, EA_unknown, EO_unknown_error );
    info.osError = -1;
    info.socket = 0;
    info.error.setText( str );
    gErrorNotify->onSocketError( info );
  }
  else {
    OutputDebugString( str );
    if( gDebugging ) {
      if( ::MessageBox( NULL, str, "Etwork: Assert Failure", MB_OKCANCEL | MB_ICONSTOP ) 
          == IDCANCEL ) {
        OutputDebugString( "User pressed CANCEL on assert dialog; hitting breakpoint.\n" );
        __asm { int 3 }
      }
    }
  }
}


EtworkError::EtworkError()
{
  error_ = 0;
  text_ = 0;
}

EtworkError::EtworkError( int severity, int area, int option )
{
  ASSERT( (severity&ES_mask) == severity );
  ASSERT( (area&EA_mask) == area );
  ASSERT( (option&EO_mask) == option );
  error_ = severity | area | option;
  text_ = 0;
}

EtworkError::EtworkError( EtworkError const & o )
{
  error_ = o.error_;
  if( o.text_ ) {
    text_ = ::strdup( o.text_ );
  }
  else {
    text_ = 0;
  }
}

EtworkError::~EtworkError()
{
  ::free( text_ );
}

EtworkError & EtworkError::operator=( EtworkError const & o )
{
  if( text_ ) {
    ::free( text_ );
  }
  new( (void *)this ) EtworkError( o );
  return *this;
}

EtworkError & EtworkError::operator=( int err )
{
  error_ = err;
  return *this;
}

EtworkError::operator int() const
{
  return error_;
}

int EtworkError::severity() const
{
  return error_ & ES_mask;
}

int EtworkError::area() const
{
  return error_ & EA_mask;
}

int EtworkError::option() const
{
  return error_ & EO_mask;
}

bool EtworkError::operator==( EtworkError const & o ) const
{
  return error_ == o.error_;
}

bool EtworkError::operator!() const
{
  return !error_;
}

EtworkError::operator bool() const
{
  return !!error_;
}

static char const * severity_str( int sev )
{
  switch( sev & ES_mask ) {
    case ES_internal: return "internal error";
    case ES_catastrophe: return "catastrophic error"; 
    case ES_error: return "runtime error"; 
    case ES_warning: return "runtime warning"; 
    case ES_note: return "runtime note"; 
    default: return "illegal severity code"; 
  }
}

static char const * area_str( int area )
{
  switch( area & EA_mask ) {
    case EA_unknown: return "unknown area";
    case EA_dispose: return "dispose";
    case EA_session: return "session";
    case EA_buffer: return "buffer";
    case EA_connect: return "connection";
    case EA_address: return "address";
    case EA_init: return "initialization";
    default: return "illegal area code";
  }
}

static char const * option_str( int option )
{
  switch( option & EO_mask ) {
    case EO_no_error: return "no error";
    case EO_unknown_error: return "unknown error";
    case EO_unsupported_version: return "unsupported version";
    case EO_unsupported_platform: return "unsupported platform";
    case EO_invalid_parameters: return "invalid parameters";
    case EO_buffer_full: return "buffer full";
    case EO_out_of_resources: return "out of resources";
    case EO_bad_address: return "bad address";
    case EO_already_in_use: return "already in use";
    case EO_peer_refused: return "peer refused connection";
    case EO_peer_dropped: return "peer dropped connection";
    case EO_peer_timeout: return "peer timed out";
    case EO_peer_violation: return "peer violated protocol";
    default: return "illegal option code";
  }
}




char const * EtworkError::c_str() const
{
  if( !text_ ) {
    char buf[2048];
    _snprintf( buf, 2048, "%s in %s: %s",
        severity_str( severity() ), area_str( area() ), option_str( option() ) );
    buf[2047] = 0;
    text_ = ::strdup( buf );
  }
  return text_;
}

void EtworkError::setText( char const * text )
{
  if( text_ ) {
    ::free( text_ );
  }
  if( text ) {
    text_ = ::strdup( text );
  }
  else {
    text_ = 0;
  }
}




bool etwork::impl::wsa_error( int wsaErr, ErrorArea area )
{
  return wsa_error_from( (ISocket *)0, (ISocketManager *)0, wsaErr, area );
}

bool etwork::impl::wsa_error_from( ISocket * sock, int wsaErr, ErrorArea area )
{
  return wsa_error_from( sock, sock ? static_cast< Socket * >( sock )->mgr_ : 0, wsaErr, area );
}

bool etwork::impl::wsa_error_from( ISocket * sock, ISocketManager * mgr, int wsaErr, ErrorArea area )
{
  IErrorNotify * en = gErrorNotify;
  if( mgr ) {
    SocketManager * smgr = static_cast< SocketManager *> ( mgr );
    if( smgr->settings_.notify ) {
      en = smgr->settings_.notify;
    }
  }
  SocketManager * sm = static_cast< SocketManager * >( mgr );
  ErrorInfo info;
  info.error = GetWsaError( wsaErr, area );
  info.osError = wsaErr;
  info.socket = sock;
  if( en ) {
    en->onSocketError( info );
  }
  else if( info.error.severity() >= ES_error || (sm && sm->settings_.debug) || gDebugging ) {
    char buf[2048];
    _snprintf( buf, 2048, "Etwork: Error %d in wsa_error_from(): %s (%s)\n", wsaErr,
        info.error.c_str(), get_error_string( wsaErr ).c_str() );
    buf[2047] = 0;
    OutputDebugString( buf );
  }
  return info.error.severity() < ES_catastrophe;
}

bool etwork::impl::etwork_error_from( ISocket * sock, ISocketManager * mgr, EtworkError err )
{
  IErrorNotify * en = gErrorNotify;
  if( mgr ) {
    SocketManager * smgr = static_cast< SocketManager *> ( mgr );
    if( smgr->settings_.notify ) {
      en = smgr->settings_.notify;
    }
  }
  SocketManager * sm = static_cast< SocketManager * >( mgr );
  ErrorInfo info;
  info.error = err;
  info.osError = 0;
  info.socket = sock;
  if( en ) {
    en->onSocketError( info );
  }
  else if( (sm && sm->settings_.debug) || gDebugging ) {
    char buf[2048];
    _snprintf( buf, 2048, "Etwork: Error in etwork_error_from(): %s\n", info.error.c_str() );
    buf[2047] = 0;
    OutputDebugString( buf );
  }
  return info.error.severity() < ES_catastrophe;
}

bool etwork::impl::etwork_info_from( ISocketManager * mgr, ErrorInfo err )
{
  IErrorNotify * en = gErrorNotify;
  if( mgr ) {
    SocketManager * smgr = static_cast< SocketManager *> ( mgr );
    if( smgr->settings_.notify ) {
      en = smgr->settings_.notify;
    }
  }
  SocketManager * sm = static_cast< SocketManager * >( mgr );
  if( err.socket ) {
    sm = static_cast< SocketManager * >( static_cast< Socket * >( err.socket )->mgr_ );
    if( sm->settings_.notify ) {
      en = sm->settings_.notify;
    }
  }
  if( en ) {
    en->onSocketError( err );
  }
  else if( (sm && sm->settings_.debug) || gDebugging ) {
    char buf[2048];
    _snprintf( buf, 2048, "Etwork: Error in etwork_error_from(): %s (%s)\n", err.error.c_str(),
        get_error_string( err.osError ).c_str() );
    buf[2047] = 0;
    OutputDebugString( buf );
  }
  return err.error.severity() < ES_catastrophe;
}

void etwork::impl::etwork_log( ISocket * sock, ErrorSeverity sev, char const * text, ... )
{
  char buf[2048];
  va_list vl;
  va_start( vl, text );
  _vsnprintf( buf, 2048, text, vl );
  va_end( vl );
  buf[2047] = 0;
  ErrorInfo ei;
  ei.error = EtworkError( sev, EA_unknown, EO_no_error );
  ei.error.setText( buf );
  ei.osError = 0;
  ei.socket = 0;
  etwork_info_from( 0, ei );
}

