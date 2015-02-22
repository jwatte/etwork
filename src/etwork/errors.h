
#if !defined( etwork_errors_h )
//! \internal Header guard
#define etwork_errors_h

#include "etwork/etwork.h"

class IErrorNotify;
enum ErrorSeverity;
enum ErrorArea;
enum ErrorOption;

//! \addtogroup API Core API
//! @{

//! \file errors.h
//!
//! Errors.h defines the error reporting API for Etwork. 
//!
//! To get errors for a specific Etwork instance, specify an 
//! IErrorNotify instance in the EtworkSettings struct. To get 
//! errors for all Etwork instances, set up the IErrorNotify in 
//! a call to SetEtworkErrorNotify().
//!
//! Errors consist of a severity, an area, and an option.
//!
//! <dl>
//! <dt>Severity</dt><dd>How critical the error is (or whether
//! it's just an informational message).</dd>
//! <dt>Area</dt><dd>Whence in the Etwork library and usage life 
//! cycle the error stems.</dd>
//! <dt>Option</dt><dd>Typically, an error code specific to the 
//! area of the error.</dd>
//! </dl>
//! \see ErrorSeverity, ErrorArea and ErrorOption.

//! If you call SetEtworkErrorNotify, the given error notification will 
//! be used for errors where there is no obvious subsystem instance to 
//! blame within the Etwork code. This notify will also be the default 
//! for each subsystem that specifies NULL for EtworkSettings::notify.
//!
//! @param notify is the interface to notify about errors. Set to NULL 
//! to stop notification of global errors.
ETWORK_API void SetEtworkErrorNotify( IErrorNotify * notify );

//! EtworkError captures information about a failure. It encapsulates 
//! a severity of a failure, an area of a 
//! failure, and specific options/informations about the failure.
class ETWORK_API EtworkError {
  public:
    //! Create an error that actually means "no error".
    EtworkError();
    //! Create an error with the given severity, area and option.
    EtworkError( int severity, int area, int option );
    //! Copy an error.
    EtworkError( EtworkError const & );
    //! Destroy an error.
    ~EtworkError();
    //! Copy an error.
    EtworkError & operator=( EtworkError const & );
    //! Re-inflate an error that was previously turned into an integer.
    EtworkError & operator=( int err );
    //! Turn an error into an integer that you can store somewhere.
    operator int() const;
    //! Return the severity out of the error. Higher severities are worse. See enum ErrorSeverity.
    int severity() const;
    //! Return the area of the error. See enum ErrorArea.
    int area() const;
    //! Return specific information about the error. See enum ErrorOption.
    int option() const;
    //! Compare this error to another error.
    bool operator==( EtworkError const & o ) const;
    //! Return TRUE if this error is "no error".
    bool operator!() const;
    //! Return TRUE if this error represents some level of error.
    operator bool() const;
    //! Retrieve the error text of this particular error.
    char const * c_str() const;
    //! Set the error text explicitly.
    void setText( char const * text );
  private:
    int error_;
    mutable char * text_;
};

//! ErrorInfo is delivered to your IErrorNotify::onSocketError() callback.
//! This allows you to get much finer grained error reporting resolution 
//! than the works-or-not level of reporting that you get from the base 
//! API.
struct ErrorInfo {
  EtworkError error;            //!< The error, as understood by Etwork.
  int osError;                  //!< An underlying OS error code (WSAE... on WIN32 or E... on UNIX).
  ISocket * socket;             //!< The socket that generated the error (if known, else NULL).
};

//! IErrorNotify is an interface you can implement to receive error 
//! notification on a per-event, per-socket basis. Because it's an 
//! interface, you can implement it once per ISocketManager you create, 
//! or once (as a singleton) within your application. Using IErrorNotify 
//! is entirely optional.
//!
//! You assign EtworkSettings::notify this interface before you call 
//! CreateEtwork() if you want to get error notifications.
class IErrorNotify {
  public:
    //! onSocketError() is called to notify you about some error. 
    //! @param info contains information about the error.
    virtual void onSocketError( ErrorInfo const & info ) = 0;
};

//! Specify how bad an error is.
enum ErrorSeverity {
  ES_note = 0x00000000,       //!< Error can be ignored.
  ES_warning = 0x04000000,    //!< Error was worked around by library.
  ES_error = 0x08000000,      //!< Error can be worked around by user code.
  ES_catastrophe = 0x0c000000,//!< Error cannot be recovered from.
  ES_internal = 0x10000000,   //!< Error is internal to Etwork.

  ES_mask = 0x7f000000,       //!< Mask out the severity
};

//! Specify where an error is coming from.
enum ErrorArea {
  EA_init = 0x010000,         //!< Error comes from system setup.
  EA_address = 0x020000,      //!< Error comes from address resolution.
  EA_connect = 0x030000,      //!< Error comes from trying to create/accept connections.
  EA_buffer = 0x040000,       //!< Error comes from buffering.
  EA_session = 0x050000,      //!< Error comes from session management.
  EA_dispose = 0x060000,      //!< Error comes from teardown/close.
  EA_unknown = 0x070000,      //!< Error comes from unknown cause.

  EA_mask = 0xff0000,         //!< Mask out the area.
};

//! Specify what a specific error is.
enum ErrorOption {
  EO_no_error,                //!< Everything's hunky-dory.
  EO_unknown_error,           //!< Some error that's not otherwise specified.
  EO_unsupported_version,     //!< The version of Etwork was wrong.
  EO_unsupported_platform,    //!< The version of WinSock was wrong.
  EO_invalid_parameters,      //!< Queue size less than max message, write for more than max message, etc.
  EO_buffer_full,             //!< Out of queuing space; messages dropped.
  EO_out_of_resources,        //!< Underlying infrastructure failure. Sockets, memory, ...
  EO_bad_address,             //!< Specified address does not exist.
  EO_already_in_use,          //!< Specified port is already in use, etc.
  EO_peer_refused,            //!< Can't actually connect to peer.
  EO_peer_dropped,            //!< The peer dropped the connection.
  EO_peer_timeout,            //!< The peer hasn't responded for a long time.
  EO_peer_violation,          //!< The peer is violating the framing protocol.
  EO_internal_error,          //!< Something went wrong internally (call order, etc).

  EO_mask = 0xffff,           //!< Mask out the peer.
};

//! @}

#endif  //  etwork_errors_h

