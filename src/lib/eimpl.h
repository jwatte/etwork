
#if !defined( eimpl_h )
#define eimpl_h

#include "etwork/locker.h"
#include "etwork/errors.h"

#define ASSERT(x) \
  if(x);else assert_failure(#x,__FILE__,__LINE__)

#define IS_SOCKET_ERROR(x) \
  ((x) == INVALID_SOCKET)


namespace etwork {
  namespace impl {

    extern bool gDebugging;
    extern bool wsOpen;
    extern Lock gethostLock;  //  gethostbyname is not thread safe
    extern IErrorNotify * gErrorNotify;

    void assert_failure( char const * expr, char const * file, int line );

    std::string get_error_string( int ecode );

    //  return true if there's some chance of going on
    bool wsa_error( int wsaErr, ErrorArea area );
    bool wsa_error_from( ISocket * sock, int wsaErr, ErrorArea area );
    bool wsa_error_from( ISocket * sock, ISocketManager * mgr, int wsaErr, ErrorArea area );
    bool etwork_error_from( ISocket * sock, ISocketManager * mgr, EtworkError err );
    bool etwork_info_from( ISocketManager * mgr, ErrorInfo info );
    void etwork_log( ISocket * sock, ErrorSeverity sev, char const * text, ... );
  }
}


#endif  //  eimpl_h

