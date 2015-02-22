
#include "etwork/etwork.h"
#include "etwork/buffer.h"
#include "etwork/errors.h"
#include "etwork/notify.h"
#include "etwork/marshal.h"

#include <assert.h>
#include <stdio.h>
#include <string>
#include <math.h>

#if defined( NDEBUG )
#pragma warning( disable: 4101 )  //  unreferenced local variable
#endif


void TestEtworkCreate()
{
  EtworkSettings es;
  es.accepting = true;
  es.port = 11147;
  ISocketManager * sm = CreateEtwork( &es );
  sm->dispose();
}

void TestEtworkBuffer()
{
  etwork::Buffer b( 1000, 3000, 10 );
  b.put_message( "hello, world!", 13 );
  b.put_message( "1234567890", 10 );
  char buf[100];
  assert( b.space_used() == 10+13 );
  int r = b.get_data( buf, 100 );
  assert( r == 10+13+2+2 );
  assert( !b.space_used() );
  assert( r == 2 + 13 + 2 + 10 );
  assert( buf[0] == 0 && buf[1] == 13 );
  assert( !strncmp( &buf[2], "hello, world!", 13 ) );
  assert( buf[2+13] == 0 && buf[3+13] == 10 );
  assert( !strncmp( &buf[2+13+2], "1234567890", 10 ) );
  r = b.put_data( buf, 10+13+2+2 );
  assert( r == 10+13+2+2 );
  assert( b.space_used() == 10+13 );
  assert( b.get_message( buf, 100 ) == 13 );
  assert( !strncmp( buf, "hello, world!", 13 ) );
  assert( b.space_used() == 10 );
  assert( b.get_message( buf, 100 ) == 10 );
  assert( !strncmp( buf, "1234567890", 10 ) );
  assert( b.get_message( buf, 100 ) == -1 );
  assert( b.space_used() == 0 );
}

void TestEtworkBufferEvil()
{
  etwork::Buffer b( 10, 20, 5 );
  assert( b.put_message( "1234567890", 10 ) == 10 );
  assert( b.put_message( "", 0 ) == 0 );
  assert( b.space_used() == 10 );
  assert( b.put_message( "1234567890-", 11 ) == -1 );
  assert( b.space_used() == 10 );
  char buf[100];
  assert( b.get_data( buf, 100 ) == 10+2+2 );
  assert( b.space_used() == 0 );
  assert( b.put_data( buf, 1 ) == 1 );
  assert( b.put_data( &buf[1], 1 ) == 1 );
  assert( b.put_data( &buf[2], 9 ) == 9 );
  assert( b.space_used() == 0 );
  assert( b.put_data( &buf[11], 2 ) == 2 );
  assert( b.space_used() == 10 );
  assert( b.get_message( buf, 9 ) == -1 );
  assert( b.space_used() == 10 );
  assert( b.get_message( buf, 10 ) == 10 );
  assert( b.space_used() == 0 );
  assert( b.get_message( buf, 10 ) == -1 );
  assert( b.put_data( &buf[13], 1 ) == 1 );
  assert( b.space_used() == 0 );
  assert( b.get_message( buf, 100 ) == 0 );
  assert( b.get_message( buf, 100 ) == -1 );
}

void TestEtworkTcp()
{
  EtworkSettings es;
  es.accepting = true;
  es.reliable = true;
  es.port = 11147;
  ISocketManager * sm = CreateEtwork( &es );
  ISocket * s1;
  int i = sm->connect( "127.0.0.1", 11147, &s1 );
  assert( i == 1 );
  ISocket * active[4];
  i = sm->poll( 0.1, active, 4 );
  assert( i == 0 );
  i = sm->accept( active, 4 );
  assert( i == 1 );
  ISocket * s2 = active[0];
  i = s1->write( "hello, world!\n", 14 );
  assert( i == 14 );
  i = s1->write( "", 0 );
  assert( i == 0 );
  char buf[200];
  assert( s2->read( buf, 200 ) == -1 );
  i = sm->poll( 0.1, active, 4 );
  assert( i == 2 );           //  s2 received data, s1 wrote data
  assert( active[0] == s2 || active[1] == s2 );
  assert( active[0] == s1 || active[1] == s1 );
  assert( s2->read( buf, 200 ) == 14 );
  assert( !strncmp( buf, "hello, world!\n", 14 ) );
  assert( s2->read( buf, 200 ) == 0 );
  assert( s2->read( buf, 200 ) == -1 );
  s2->dispose();
  s1->write( "X", 2 );
  assert( !s1->closed() );
  i = sm->poll( 0.1, active, 4 );
  assert( i == 1 );
  i = sm->poll( 0.1, active, 4 );
  if( !s1->closed() ) {
    i = sm->poll( 0.1, active, 4 );
  }
  assert( s1->closed() );
  s1->dispose();
  sm->dispose();
}

void TestEtworkUdp()
{
  EtworkSettings es1;
  es1.accepting = true;
  es1.reliable = false;
  es1.port = 11147;
  ISocketManager * sm1 = CreateEtwork( &es1 );
  assert( sm1 != 0 );

  EtworkSettings es2;
  es2.accepting = true;
  es2.reliable = false;
  es2.port = 11148;
  ISocketManager * sm2 = CreateEtwork( &es2 );
  assert( sm2 != 0 );

  ISocket * s1 = 0;
  int i = sm1->connect( "127.0.0.1", 11148, &s1 );
  assert( i == 1 );
  ISocket * active[4];
  i = sm1->poll( 0.1, active, 4 );
  assert( i == 1 ); // the newly connected socket

  i = sm2->poll( 0.1, active, 4 );
  assert( i == 0 );
  ISocket * s2 = 0;
  i = sm2->accept( &s2, 1 );
  assert( i == 1 );

  char buf[200];
  assert( s2->read( buf, 200 ) == -1 );   //  can't receive data until accepted
  i = s2->write( "hello, world!\n", 14 );
  assert( i == 14 );
  i = s2->write( "xyzzy", 6 );
  assert( i == 6 );

  i = sm1->poll( 0.1, active, 4 );
  assert( i == 1 );   //  s1 is receiving greeting message
  assert( active[0] == s1 );
  i = s1->read( buf, 200 );
  assert( i == 0 );
  i = s1->read( buf, 200 );
  assert( i == -1 );
  i = sm2->poll( 0.1, active, 4 );
  assert( i == 1 );   //  s2 just sent data
  i = sm1->poll( 0.1, active, 4 );
  assert( i == 1 );   //  s1 received it
  assert( s2->read( buf, 200 ) == -1 );
  i = s1->read( buf, 200 );
  assert( i == 14 );
  assert( !strncmp( buf, "hello, world!\n", 14 ) );
  i = s1->read( buf, 200 );
  assert( i == 6 );
  assert( !strncmp( buf, "xyzzy", 6 ) );

  //  UDP doesn't give us notice when it closes
  s1->dispose();
  s2->dispose();
  sm1->dispose();
  sm2->dispose();
}

class ErrorNotify : public IErrorNotify {
  public:
    ErrorInfo error_;
    ErrorNotify() {
      clear();
    }
    virtual void onSocketError( ErrorInfo const & info ) {
      error_ = info;
    }
    void clear() {
      error_.socket = 0;
      error_.osError = 0;
      error_.error = 0;
    }
};

void TestEtworkErrors()
{
  ISocketManager * mgr;
  ErrorNotify en;
  SetEtworkErrorNotify( &en );
  EtworkSettings st;
  st.port = 0;
  st.accepting = true;
  mgr = CreateEtwork( &st );
  assert( mgr == 0 );
  assert( (int)en.error_.error != 0 );
  SetEtworkErrorNotify( 0 );
}

class SocketNotify : public INotify {
  public:
    SocketNotify() {
      notified_ = false;
    }
    void clear() {
      notified_ = false;
    }
    void onNotify() {
      notified_ = true;
    }
    bool notified_;
};

void TestEtworkNotify()
{
  EtworkSettings st;
  st.accepting = true;
  st.port = 61234;
  ISocketManager * mgr = CreateEtwork( &st );
  assert( mgr != 0 );
  ISocket * active[2];
  mgr->poll( 0.01, active, 2 );
  ISocket * sock = 0;
  int r = mgr->connect( "127.0.0.1", 61234, &sock );
  assert( r == 1 );
  mgr->poll( 0.01, active, 2 );
  ISocket * sock2 = 0;
  assert( mgr->accept( &sock2, 1 ) == 1 );
  SocketNotify n1, n2;
  SetEtworkSocketNotify( sock, &n1 );
  SetEtworkSocketNotify( sock2, &n2 );
  r = mgr->poll( 0.01, active, 2 );
  assert( r == 0 );
  assert( !n1.notified_ );
  assert( !n2.notified_ );
  r = sock->write( "hello", 5 );
  assert( r == 5 );
  r = mgr->poll( 0.01, active, 2 );
  assert( r == 0 );
  r = mgr->poll( 0.01, active, 2 );
  assert( r == 0 );
  assert( n1.notified_ ); //  because it wrote
  assert( n2.notified_ ); //  because it received
  n1.clear();
  n2.clear();
  r = mgr->poll( 0.01, active, 2 );
  assert( r == 0 );
  assert( !n1.notified_ );
  assert( !n2.notified_ );
  sock->dispose();
  sock2->dispose();
  mgr->dispose();
}

void TestBlock()
{
  char abuf[32];
  Block a( abuf, 32 );
  Block b( 40 );
  b << a;
  assert( b.pos() == 32 );
  assert( b.left() == 8 );
  b << a;
  assert( b.eof() );
  b.seek( 0 );
  assert( !b.eof() );
  assert( b.read( abuf, 32 ) == 32 );
  assert( !b.eof() );
  assert( b.read( abuf, 32 ) == 8 );
  assert( !b.eof() );
  assert( b.read( abuf, 32 ) == 0 );
  assert( b.eof() );
  assert( a.begin() == (unsigned char *)abuf );
  assert( a.end() == (unsigned char *)&abuf[32] );
}

struct MarshalTest {
  float f;
  bool b;
  std::string s;
  int i;
};

struct AMarshalTest2 {
  int i;
  MarshalTest mt;
};

MARSHAL_BEGIN_TYPE( AMarshalTest2 )   //  make sure it's alphabetically before MarshalTest
  MARSHAL_INT( i, 0, 2 )
  MARSHAL_TYPE( MarshalTest, mt )
MARSHAL_END_TYPE( AMarshalTest2, 2 )

MARSHAL_BEGIN_TYPE( MarshalTest )
  MARSHAL_FLOAT( f, -1, 1, 0.01f )
  MARSHAL_INT( i, 0, 200 )
  MARSHAL_STRING( s, 200 )
  MARSHAL_BOOL( b )
MARSHAL_END_TYPE( MarshalTest, 1 )

void TestMarshal()
{
  char const * err = IMarshalManager::startup();
  assert( !err );
  Block buf( 1000 );
  MarshalTest mt;
  mt.f = -0.5;
  mt.b = true;
  mt.s = "hello, world!";
  mt.i = 200;
  //  Each call to Marshaller<MarhsalTest>() creates a new 
  //  marshaller instance. In reality, we use the registry 
  //  to get the tester class.
  size_t s = Marshaller< MarshalTest >().marshal( &mt, buf );
  assert( s == 17 );
  buf.seek( 0 );
  char mem[ sizeof( MarshalTest ) ];
  memset( mem, 0, sizeof( mem ) );
  Marshaller< MarshalTest >().construct( mem );
  s = Marshaller< MarshalTest >().demarshal( buf, mem );
  assert( s == 17 );
  MarshalTest * mtp = (MarshalTest *)mem;
  assert( ::fabsf( mtp->f - -0.5f ) < 0.005f );
  assert( mtp->b == true );
  assert( mtp->s == "hello, world!" );
  assert( mtp->i == 200 );
  Marshaller< MarshalTest >().destruct( mem );

  buf.seek( 0 );
  bool b = IMarshalManager::instance()->marshal( mt, buf );
  assert( b );
  assert( buf.pos() == 17 );
  buf.seek( 0 );
  b = IMarshalManager::instance()->demarshal( mt, buf );
  assert( b );
  assert( buf.pos() == 17 );
}

struct AcceptPacket {
  unsigned int user_;
  unsigned int expiry_;
  unsigned int protoCount_;
};
MARSHAL_BEGIN_TYPE( AcceptPacket )
  MARSHAL_INT( user_, 0, 1000 )
  MARSHAL_INT( expiry_, 0, 30000 )
  MARSHAL_INT( protoCount_, 0, 10000 )
MARSHAL_END_TYPE( AcceptPacket, 0x13 )

struct Uint64Packet {
  unsigned long long uint64_;
};
MARSHAL_BEGIN_TYPE( Uint64Packet )
  MARSHAL_UINT64( uint64_, 64 )
MARSHAL_END_TYPE( Uint64Packet, 0x14 )

void TestMarshalBugs()
{
  //  I had a bug provoked by the AcceptPacket
  AcceptPacket ap;
  memset(&ap, 0, sizeof(ap));
  ap.user_ = 100;
  ap.expiry_ = 100;
  ap.protoCount_ = 4;
  {
    Block b(200);
    IMarshalManager::instance()->marshal(ap, b);
    memset(&ap, 0, sizeof(ap));
    b.seek(0);
    IMarshalManager::instance()->demarshal(ap, b);
  }
  assert( ap.user_ == 100 );
  assert( ap.expiry_ == 100 );
  assert( ap.protoCount_ == 4 );

  //  I had a bug provoked by a single uint64 field
  Uint64Packet up;
  memset(&up, 0, sizeof(up));
  up.uint64_ = 1234;
  {
    Block b(200);
    IMarshalManager::instance()->marshal(up, b);
    memset(&up, 0, sizeof(up));
    b.seek(0);
    IMarshalManager::instance()->demarshal(up, b);
  }
  assert( up.uint64_ == 1234 );

  IMarshaller *m = IMarshalManager::instance()->marshaller(typeid(Uint64Packet).name());
  assert(m);
  assert(m->id() == 0x14);
}

int main()
{
  fprintf( stderr, "Testing Etwork...\n" );
  TestEtworkCreate();
  TestEtworkBuffer();
  TestEtworkBufferEvil();
  TestEtworkTcp();
  TestEtworkUdp();
  TestEtworkErrors();
  TestEtworkNotify();
  TestBlock();
  TestMarshal();
  TestMarshalBugs();
  fprintf( stderr, "All tests passed!\n" );
  return 0;
}
