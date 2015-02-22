/*!
    \mainpage Etwork Documentation
 
    <div class="text">
      I've written a small networking library, which aims to make it easy to get 
      messages from point A to point B (and possibly points C, D and E as well), 
      and back again. This library deals with some idiosynchrasies of the WinSock 
      networking library, and also makes packet-based messaging over TCP easy 
      (it deals with receiving half a packet header, etc).
    </div>

    <div class="text">
      You can download it <a href="http://www.speakeasy.net/~hplus/etwork-download.php">here</a>.
    </div>

    <div class="text">
      Here's a quick run-down on how to use the \ref API "Core API". There are also 
      some \ref messaging "Messaging APIs" for forming binary messages to send, 
      which includes marshalling and demarshalling support, and a thin \ref Support "Support API" 
      with locks and things.
    </div>

    <div class="text">
      To add it to your Visual Studio project, make sure that the "etwork.h" header 
      can be found by your sources (the "Additional Include Paths" option under the 
      C++ compiler options), and that the "libetwork.lib" library can be found by 
      the linker (the "Additional Library Paths" option for the Linker options). 
      Include "etwork/etwork.h" in your program, and make sure it links against 
      libetwork.lib (or libetwork_d.lib, for your debug build).
    </div>

    <div class="text">
      To use it, you first create a socket manager. This will open up a port for 
      incoming connections, and initialize networking as necessary.
    </div>

\code
#include "etwork/etwork.h"

  EtworkSettings settings;
  settings.port = 7654;
  settings.accepting = true; // make this 'false' if a client
  ISocketManager * mgr = CreateEtwork( &settings );
  if( !mgr ) { fail_horribly(); }
\endcode

    <div class="text">
      The socket manager needs to be serviced every so often; typically each time 
      through your main loop. If you want to sleep until something happens, you 
      can pass a timeout to the poll() function; else you can pass 0 to just check 
      status and return.
    </div>

\code
  while( true ) {
    ISocket * happening[10];
    int i = mgr->poll( 0.01, happening, 10 );
    for( int j = 0; j < i; ++j ) {
      // Do something with socket happening[j]. You 
      // have previously seen this socket from accept().
      do_something_with_active_socket( happening[j]->data_ );
    }
    i = mgr->accept( happening, 10 );
    for( int j = 0; j < i; ++j ) {
      // happening[j] is a new client attempting to connect.
      // Hang on to this socket until you no longer want to 
      // speak to the other side. You can associate some data 
      // with the socket, to make it easier to manage it.
      happening[j]->data_ = some_data_I_attach_to_the_connection();
    }
  }
\endcode

    <div class="text">
      To send a message to the other end, use write() on the ISocket. To receive 
      any message that was sent from the other end, use read(). To check whether 
      a socket is closed on the other end (and you should thus give it up), call 
      closed().
    </div>

    <div class="text">
      Etwork differs from raw WinSock2 in that each call to write() on a socket 
      will result in exactly one return from read() for as many bytes on the other 
      end (if in reliable mode, which is the default). This makes messaging much 
      more convenient, as messages won't "run together" or be received in halves.
    </div>

\code
  int r;
  char buf[2000];
  while( (r = sock->read( buf, 2000 )) >= 0 ) {
    if( r == 0 ) continue; // empty message; keepalive
    do_something_with_message( buf, r, sock->data_ );
  }
  if( sock->closed() ) {
    give_up_socket( sock->data_ );
    sock->dispose();
    return;
  }
  sock->write( "hello, world!", 13 );
\endcode

    <div class="text">
      To connect to another machine that's currently listening for connections, 
      use the connect() function on the manager. Note that this function may be 
      blocking, so it's probably not a great thing to do while your program is 
      running interactively; instead you want to establish connections early on, 
      and hang on to them until you're done.
    </div>

\code
  EtworkSettings settings;
  settings.port = 0; // use 0 when not accepting
  settings.accepting = false;
  ISocketManager * mgr = CreateEtwork( &settings );
  if( !mgr ) { fail_horribly(); }
  ISocket * otherEnd = 0;
  // you can use text host names, or dotted-decimal form (12.34.56.78)
  int i = mgr->connect( "some.host.net", 7654, &otherEnd );
  if( i != 1 ) { fail_horribly(); }
  otherEnd->write( "hello, world!", 13 );
\endcode

    <div class="text">
      You still need to poll the socket manager every so often to send queued 
      messages, and receive new messages.
    </div>

    \see SetEtworkErrorNotify() and SetEtworkSocketNotify() for more advanced 
    usage of the core Etwork APIs.
    See also \ref rationale .

    <div class="text">
      If you have questions or comments, please leave them in the 
      <a href="http://www.gamedev.net/community/forums/topic.asp?topic_id=368471">
        Networking and Multiplayer forum </a> on GameDev.net, or mail them to the 
      e-mail address found in the README file in the download archive.
    </div>


\page todo Bugs and Things To Do

<h3>Things To Do</h3>
<ol style="compact">
<li>Port to Linux.
<ol style="compact">
<li>Clean up chat examples.</li>
</ol></li>
<li>Message marshalling protocol.
<ol style="compact">
<li>Local LAN service broadcast.</li>
<li>Global server/matchmaker browser protocol.</li>
<li>Reliable UDP.</li>
<li>Entity-level state replication.</li>
<ol style="compact">
<li>Demo app.</li>
</ol>
<li>Re-do chat on top of messaging.</li>
</ol></li>
</ol>

<h3>Known Bugs</h3>
<table style="border-collapse: collapse; padding: 2 2 2 2;">
<tr><th>Version</th><th>Bug</th><th>Fixed</th></tr>
</table>

    */
