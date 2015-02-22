
#include <assert.h>
#include <string>
#include <math.h>
#include <map>

#include "etwork/marshal.h"

namespace marshaller {

inline std::string operator+( std::string const & left, int right ) {
  char buf[24];
  sprintf( buf, "%d", right );
  return left + std::string( buf );
}

inline std::string operator+( std::string const & left, size_t right ) {
  char buf[24];
  sprintf( buf, "%lu", right );
  return left + std::string( buf );
}

inline std::string operator+( std::string const & left, double right ) {
  char buf[24];
  sprintf( buf, "%f", right );
  return left + std::string( buf );
}
//  IntMarshaller can store an integer with some 
//  minimum and maximum value, using the minimum 
//  number of bytes required to store a value in 
//  that range.
class IntMarshaller : public IMarshaller {
  public:
    IntMarshaller( int min, int max ) : IMarshaller( 0 ) {
      setRange( min, max );
    }
    IntMarshaller() : IMarshaller( 0 ) {
      min_ = max_ = bytes_ = 0;
    }
    void setRange( int min, int max ) {
      min_ = min;
      max_ = max;
      bytes_ = 1;
      while( bytes_ < sizeof(int) ) {
        if( 1<<(bytes_*8) > (max_-min_) ) {
          break;
        }
        ++bytes_;
      }
      //  validate assumptions about signed/unsigned casting.
      assert( (int)((unsigned int)1 << (sizeof(int)*8-1)) < 0 );
    }
    int min_, max_;
    unsigned char bytes_;

    virtual size_t marshal( void const * src, Block & dst ) {
      int v = *(int const *)src;
      if( v < min_ || v > max_ ) {
        throw std::invalid_argument( std::string( "IntMarshaller argument " ) + v + " is out of bounds: [" +
            min_ + "-" + max_ + "]" );
      }
      if( dst.left() < bytes_ ) return 0;
      unsigned char * d = dst.cur();
      //  I assume unsigned/signed casting works as bit 
      //  interpretation here (and doesn't clamp).
      unsigned int val = v;
      for( unsigned char c = bytes_; c > 0; --c ) {
        d[c-1] = val & 0xff;
        val >>= 8;
      }
      dst.seek( dst.pos() + bytes_ );
      return bytes_;
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      if( src.left() < bytes_ ) return 0;
      unsigned char * s = src.cur();
      unsigned int ret = 0;
      for( unsigned char i = 0; i < bytes_; ++i ) {
        ret = (ret << 8) | s[i];
      }
      //  I assume that unsigned int cast to signed uses bit 
      //  interpretation rather than clamping.
      ret += min_;
      *(int*)dst = ret;
      if( *(int *)dst < min_ || *(int *)dst > max_ ) {
        throw std::invalid_argument( std::string( "IntMarshaller demarshal " ) +
            *(int *)dst + " is out of bounds: [" + min_ + "-" + max_ + "]" );
      }
      src.seek( src.pos() + bytes_ );
      return bytes_;
    }
    virtual void construct( void * memory ) {
      *(int*)memory = 0;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(int);
    }
    virtual size_t maxMarshalledSize() {
      return bytes_;
    }
};

class UintMarshaller : public IMarshaller {
  public:
    UintMarshaller( int bits ) : IMarshaller( 0 ) {
      setBits( bits );
    }
    UintMarshaller() : IMarshaller( 0 ) {
      bits_ = bytes_ = 0;
    }
    void setBits( int bits ) {
      bits_ = bits;
      bytes_ = 1;
      while( bytes_ < sizeof(unsigned int) ) {
        if( bytes_*8 >= bits ) {
          break;
        }
        ++bytes_;
      }
    }
    int bits_;
    unsigned char bytes_;

    virtual size_t marshal( void const * src, Block & dst ) {
      unsigned int v = *(unsigned int const *)src;
      if( v > (unsigned int)((1ULL<<bits_)-1) ) {
        throw std::invalid_argument( std::string( "IntMarshaller argument " ) + v + 
            " is out of bounds: [0-" + (unsigned int)((1ULL<<bits_)-1) + "]" );
      }
      if( dst.left() < bytes_ ) return 0;
      unsigned char * d = dst.cur();
      //  I assume unsigned/signed casting works as bit 
      //  interpretation here (and doesn't clamp).
      unsigned int val = v;
      for( unsigned char c = bytes_; c > 0; --c ) {
        d[c-1] = val & 0xff;
        val >>= 8;
      }
      dst.seek( dst.pos() + bytes_ );
      return bytes_;
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      if( src.left() < bytes_ ) return 0;
      unsigned char * s = src.cur();
      unsigned int ret = 0;
      for( unsigned char i = 0; i < bytes_; ++i ) {
        ret = (ret << 8) | s[i];
      }
      //  I assume that unsigned int cast to signed uses bit 
      //  interpretation rather than clamping.
      *(unsigned int*)dst = ret;
      if( *(unsigned int *)dst > (unsigned int)((1ULL<<bits_)-1) ) {
        throw std::invalid_argument( std::string( "UintMarshaller demarshal " ) +
            ret + " is out of bounds: [0-" + (unsigned int)((1ULL << bits_)-1) + "]" );
      }
      src.seek( src.pos() + bytes_ );
      return bytes_;
    }
    virtual void construct( void * memory ) {
      *(unsigned int*)memory = 0;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(unsigned int);
    }
    virtual size_t maxMarshalledSize() {
      return bytes_;
    }
};

//  IntMarshaller can store an integer with some 
//  minimum and maximum value, using the minimum 
//  number of bytes required to store a value in 
//  that range.
class Uint64Marshaller : public IMarshaller {
  public:
    Uint64Marshaller( int bits ) : IMarshaller( 0 ) {
      setBits( bits );
    }
    Uint64Marshaller() : IMarshaller( 0 ) {
      bits_ = bytes_ = 0;
    }
    void setBits( int bits ) {
      assert( bits >= 0 && bits <= 64 );
      bits_ = bits;
      bytes_ = (bits + 7) / 8;
    }
    int bits_;
    unsigned char bytes_;

    virtual size_t marshal( void const * src, Block & dst ) {
      unsigned long long v = *(unsigned long long const *)src;
      if( v > (unsigned long long)((1ULL << bits_)-1ULL) ) {
        throw std::invalid_argument( std::string( "Uint64Marshaller argument " ) + (double)v + " is out of bounds: [" +
            0 + "-" + (double)((1ULL<<bits_)-1) + "]" );
      }
      if( dst.left() < bytes_ ) return 0;
      unsigned char * d = dst.cur();
      //  I assume unsigned/signed casting works as bit 
      //  interpretation here (and doesn't clamp).
      unsigned long long val = v;
      for( unsigned char c = bytes_; c > 0; --c ) {
        d[c-1] = (unsigned char)(val & 0xff);
        val >>= 8;
      }
      dst.seek( dst.pos() + bytes_ );
      return bytes_;
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      if( src.left() < bytes_ ) return 0;
      unsigned char * s = src.cur();
      unsigned long long ret = 0;
      for( unsigned char i = 0; i < bytes_; ++i ) {
        ret = (ret << 8) | s[i];
      }
      //  I assume that unsigned int cast to signed uses bit 
      //  interpretation rather than clamping.
      *(unsigned long long *)dst = ret;
      src.seek( src.pos() + bytes_ );
      return bytes_;
    }
    virtual void construct( void * memory ) {
      *(unsigned long long*)memory = 0;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(unsigned long long);
    }
    virtual size_t maxMarshalledSize() {
      return bytes_;
    }
};

class FloatMarshaller : public IMarshaller {
  public:
    FloatMarshaller( float min, float max, float prec ) : 
        IMarshaller( 0 ), min_( min ), max_( max ), prec_( prec ) {
      double n = ceil(double(max_-min_)/prec_)+1;
      if( n > (1U<<(sizeof(int)*8-1))-1 ) {
        throw std::invalid_argument( "FloatMarhsaller can only deal with up to 31 bits of range." );
      }
      int_.setRange( 0, (int)n );
    }
    float min_, max_, prec_;
    IntMarshaller int_;

    virtual size_t marshal( void const * src, Block & dst ) {
      float f = *(float const *)src;
      if( f < min_ || f > max_ ) {
        throw std::invalid_argument( std::string( "FloatMarshaller argument " ) + f + " is out of bounds." );
      }
      int i = int( (double(f)-min_)/prec_ );
      return int_.marshal( &i, dst );
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      int i;
      //  int demarshalling takes care of range checking
      size_t siz = int_.demarshal( src, &i );
      if( !siz ) {
        return 0;
      }
      *(float *)dst = float( double(i)*prec_ + min_ );
      return siz;
    }
    virtual void construct( void * memory ) {
      *(float*)memory = 0;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(float);
    }
    virtual size_t maxMarshalledSize() {
      return int_.bytes_;
    }
};

class DoubleMarshaller : public IMarshaller {
  public:
    DoubleMarshaller() : 
        IMarshaller( 0 ), int_(64) {
    }
    Uint64Marshaller int_;

    virtual size_t marshal( void const * src, Block & dst ) {
      return int_.marshal( (unsigned long long *)src, dst );
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      return int_.demarshal( src, dst );
    }
    virtual void construct( void * memory ) {
      *(double*)memory = 0;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(double);
    }
    virtual size_t maxMarshalledSize() {
      return int_.bytes_;
    }
};

class BoolMarshaller : public IMarshaller {
  public:
    BoolMarshaller() : IMarshaller( typeid(bool).name() ) {}

    virtual size_t marshal( void const * src, Block & dst ) {
      char c = *(bool *)src ? 1 : 0;
      if( dst.left() < 1 ) return 0;
      return dst.write( &c, 1 );
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      char c;
      if( !src.read( &c, 1 ) ) return 0;
      *(bool *)dst = (c != 0);
      return 1;
    }
    virtual void construct( void * memory ) {
      *(bool*)memory = false;
    }
    virtual void destruct( void * memory ) {
    }
    virtual size_t instanceSize() {
      return sizeof(bool);
    }
    virtual size_t maxMarshalledSize() {
      return sizeof(char);
    }
};

class StringMarshaller : public IMarshaller {
  public:
    StringMarshaller( size_t maxSize ) :
      IMarshaller( typeid( std::string ).name() ),
      int_( 0, (int)maxSize ),
      maxSize_( maxSize )
    {
    }

    IntMarshaller int_;
    size_t maxSize_;

    virtual size_t marshal( void const * src, Block & dst ) {
      //  make sure the string is correct in size
      size_t s = (*(std::string const *)src).length();
      if( s > maxSize_ ) {
        throw std::invalid_argument( std::string( "StringMarshaller argument is too long: " ) +
            s + ">" + maxSize_ + "." );
      }
      if( dst.left() < int_.bytes_ + s ) {
        return 0;
      }
      int i = (int)s;
      int_.marshal( &i, dst );
      char const * ptr = &(char &)*(*(std::string const *)src).begin();
      dst.write( ptr, s );
      return s + int_.bytes_;
    }
    virtual size_t demarshal( Block & src, void * dst ) {
      int i = 0;
      size_t pos = src.pos();
      //  IntMarshaller does range checking.
      if( !int_.demarshal( src, &i ) ) {
        return 0;
      }
      if( src.left() < (size_t)i ) {
        src.seek( pos );
        return 0;
      }
      (*(std::string *)dst).assign( src.cur(), src.cur() + i );
      src.seek( src.pos() + i );
      return int_.bytes_ + i;
    }
    virtual void construct( void * memory ) {
      new( memory ) std::string();
    }
    virtual void destruct( void * memory ) {
      using std::string;
      ((std::string *)memory)-> ~ string();
    }
    virtual size_t instanceSize() {
      return sizeof(std::string);
    }
    virtual size_t maxMarshalledSize() {
      return int_.bytes_ + maxSize_;
    }
};

IMarshaller * TypeMarshal::resolve( IMarshalManager * manager )
{
  //  Alternative implementation possibility:
  //  1) Create a new TypeMarshal instance, copied from this.
  //  2) Sort the descs_ to get null marshallers last (this 
  //      gives better packing for raw/native types).
  //  3) Instantiate each marshaller in the new copy, based 
  //      on the specific manager.
  //  This would let us instantiate more than one manager, 
  //  potentially with different marshalling methods for 
  //  each type.
  size_t mSize = 0;
  size_t memSize = 0;
  size_t maxMemSize = 0;
  size_t cnt = descs_.size();
  for( size_t a = 0; a < cnt; ++a ) {
    MemberDesc & md = descs_[a];
    if( md.marshaller_ == NULL ) {
      //  this call will handle recursive prepares.
      md.marshaller_ = manager->marshaller( md.type_ );
      if( !md.marshaller_ ) {
        throw std::logic_error( std::string( "Marshaller for type " ) + name() + " uses type " +
            md.type_ + " which isn't defined (or is recursively used)." );
      }
    }
    size_t maxMarSize = md.marshaller_->maxMarshalledSize();
    assert( maxMarSize > 0 );
    mSize += maxMarSize;
    size_t mmSize = md.marshaller_->instanceSize();
    assert( mmSize > 0 );
    size_t maxMem = md.offset_ + mmSize;
    if( maxMem > memSize ) {
      memSize = maxMem;
    }
    if( mmSize > maxMemSize ) {
      maxMemSize = mmSize;
    }
  }
  //  alignment is 1, 2 or 4 (this may have 64-bit compatibility problems
  //  depending on your specific data structures and alignment).
  if( maxMemSize >= 3 ) maxMemSize = 4;
  assert( maxMemSize == 1 || maxMemSize == 2 || maxMemSize == 4 );
  //  round up to instance of alignment
  instanceSize_ = (memSize + (maxMemSize-1)) & -(ptrdiff_t)maxMemSize;
  maxMarshalledSize_ = mSize;
  return this;
}

size_t TypeMarshal::marshal( void const * src, Block & dst )
{
  unsigned char const * s = (unsigned char const *)src;
  size_t pos = dst.pos();
  size_t cnt = descs_.size();
  for( size_t a = 0; a < cnt; ++a ) {
    MemberDesc & md = descs_[a];
    size_t sz = md.marshaller_->marshal( s+md.offset_, dst );
    if( !sz ) {
      dst.seek( pos );
      return 0;
    }
  }
  return dst.pos()-pos;
}

size_t TypeMarshal::demarshal( Block & src, void * dst )
{
  unsigned char * d = (unsigned char *)dst;
  size_t pos = src.pos();
  size_t cnt = descs_.size();
  for( size_t a = 0; a < cnt; ++a ) {
    MemberDesc & md = descs_[a];
    size_t s = md.marshaller_->demarshal( src, d + md.offset_ );
    if( !s ) {
      src.seek( pos );
      return 0;
    }
  }
  return src.pos()-pos;
}

//  here's a thought: I could use marshallers 
//  templated on the parameters, instead of storing 
//  them here and needing to instantiate copies.
MarshalOp & MarshalOp::addInt( char const * name, size_t offset, int min, int max )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(int).name();
  md.marshaller_ = new IntMarshaller( min, max );
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addUint( char const * name, size_t offset, int bits )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(unsigned int).name();
  md.marshaller_ = new UintMarshaller( bits );
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addUint64( char const * name, size_t offset, int bits )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(unsigned long long).name();
  md.marshaller_ = new Uint64Marshaller( bits );
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addFloat( char const * name, size_t offset, float min, float max, float prec )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(float).name();
  md.marshaller_ = new FloatMarshaller( min, max, prec );
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addDouble( char const * name, size_t offset )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(double).name();
  md.marshaller_ = new DoubleMarshaller();
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addBool( char const * name, size_t offset )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(bool).name();
  md.marshaller_ = new BoolMarshaller();
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addString( char const * name, size_t offset, size_t maxSize )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeid(std::string).name();
  md.marshaller_ = new StringMarshaller( maxSize );
  it_->descs_.push_back( md );
  return *this;
}

MarshalOp & MarshalOp::addSomeType( char const * typeName, char const * name, size_t offset )
{
  MemberDesc md;
  md.name_ = name;
  md.offset_ = offset;
  md.type_ = typeName;
  md.marshaller_ = 0;   //  will resolve later, in resolve()
  it_->descs_.push_back( md );
  return *this;
}


class MarshalManager : public IMarshalManager {
  public:
    virtual void setMarshaller( char const * type, int id, marshaller::IMarshalResolve * m );
    virtual IMarshaller * marshaller( char const * type );
    virtual IMarshaller * marshaller( int id );
    virtual int countMarshallers();

    std::map< int, IMarshaller * > intMarshallers_;
    std::map< std::string, IMarshaller * > stringMarshallers_;
    std::map< std::string, std::pair< int, IMarshalResolve * > > toResolve_;

    std::string error_;
    char const * resolve();
};

void MarshalManager::setMarshaller( char const * type, int id, marshaller::IMarshalResolve * m )
{
  if( toResolve_.find( type ) != toResolve_.end() ) {
    throw std::invalid_argument( std::string( "Duplicate marshaller found for type: " ) + type );
  }
  toResolve_[type] = std::pair< int, IMarshalResolve * >( id, m );
}

char const * MarshalManager::resolve()
{
  try {
    while( toResolve_.size() > 0 ) {
      std::string name = toResolve_.begin()->first;
      int id = toResolve_.begin()->second.first;
      IMarshalResolve * r = toResolve_.begin()->second.second;
      toResolve_.erase( toResolve_.begin() );

      //  If this marshaller asks for a marshaller we haven't 
      //  resolve yet, the recursive resolution in marshaller() 
      //  will take care of that. Cyclic dependencies are broken 
      //  by returning 0 in marshaller() (because we don't add 
      //  the marshaller to the name map until it's resolved, but 
      //  remove it from toResolve_ up front).
      IMarshaller * m = r->resolve( this );
      if( !m ) {
        error_ = std::string( "Marshaller for type " ) + name + " failed to resolve.";
        return error_.c_str();
      }
      stringMarshallers_[name] = m;
      if( id != 0 ) {
        intMarshallers_[id] = m;
        m->id_ = id;
      }
    }
  }
  catch( std::logic_error const & le ) {
    //  A cyclic or un-filled dependency is likely to throw a logic_error.
    error_ = le.what();
    return error_.c_str();
  }
  catch( ... ) {
    error_ = "Unknown exception thrown during MarshalManager::resolve().";
    return error_.c_str();
  }
  return 0;
}

IMarshaller * MarshalManager::marshaller( int id )
{
  std::map< int, IMarshaller * >::iterator ptr = intMarshallers_.find( id );
  if( ptr == intMarshallers_.end() ) {
    return 0;
  }
  return (*ptr).second;
}

IMarshaller * MarshalManager::marshaller( char const * type )
{
  try {
    std::map< std::string, IMarshaller * >::iterator ptr = stringMarshallers_.find( type );
    if( ptr == stringMarshallers_.end() ) {
      std::map< std::string, std::pair< int, IMarshalResolve * > >::iterator res = 
          toResolve_.find( type );
      if( res != toResolve_.end() ) {
        //  Recursively resolve this marshaller if asked for by 
        //  some marshaller during resolve.
        std::string name = res->first;
        int id = res->second.first;
        IMarshalResolve * mr = res->second.second;
        toResolve_.erase( res );

        IMarshaller * m = mr->resolve( this );
        stringMarshallers_[name] = m;
        if( id != 0 ) {
          intMarshallers_[id] = m;
        }

        return m;
      }
      else {
        return 0;
      }
    }
    return (*ptr).second;
  }
  catch( std::logic_error const & le ) {
    throw std::logic_error( std::string( "While resolving marshaller for type: " ) + 
        type + "\n" + le.what() );
  }
}

int MarshalManager::countMarshallers()
{
  return (int)stringMarshallers_.size();
}


}   //  end namespace marshaller

IMarshaller::IMarshaller( char const * name )
{
  name_ = name;
  id_ = 0;
}

IMarshaller::~IMarshaller()
{
}

int IMarshaller::id()
{
  return id_;
}

char const * IMarshaller::name()
{
  return name_;
}

char const * IMarshalManager::startup()
{
  return static_cast< marshaller::MarshalManager * >( instance() )->resolve();
}

IMarshalManager * IMarshalManager::instance()
{
  static marshaller::MarshalManager mgr;
  return &mgr;
}

