
#if !defined( etwork_marshal_h )
//! \internal header guard
#define etwork_marshal_h

#include <typeinfo.h>
#include <vector>
#include <string>

//! \file marshal.h
//!
//! Marshalling is the process of taking data structures in memory 
//! and packing them for transmission over a network connection 
//! (or, for that matter, to a file or other serial storage).
//! Etwork provides a light-weight marshalling API intended to make 
//! exchanging messages of moderate complexity simple and painless.
//!
//! For each data structure type for which you want to support 
//! marshalling, you implement and register an instance of IMarshaller.
//! There are several helper macros and functions to make that easy, 
//! including a way to describe the specifics of your data structure 
//! using format strings.

//! \internal marshaller implementation namespace
namespace marshaller {
  class IMarshalResolve;
  class MarshalManager;
}
//! \internal forward declaration
class ETWORK_API IMarshaller;

//! \addtogroup messaging Messaging APIs
//! @{

//! Use a Block to wrap raw, un-managed chunks of memory. A Block 
//! will not (by default) allocate or de-allocate memory; it is just 
//! a thin wrapper around a pointer and a size.
class ETWORK_API Block {
  public:
    //! This version of the constructor uses the memory you supply, 
    //! and will not dispose of the memory when done.
    Block( void * base, size_t size );
    //! This version of the constructor allocates memory (and will 
    //! dispose of it when done).
    Block( size_t size );
    //! Make the Block go away.
    ~Block();

    //! Where is the current pointer at?
    unsigned char * cur();
    //! \copydoc Block::cur()
    unsigned char const * cur() const;
    //! How much is left between the current pointer and the end of the buffer?
    size_t left() const;
    //! Where is the current pointer, as an offset from the start pointer?
    size_t pos() const;
    //! Re-position the current pointer to some offset within the buffer.
    //! \param pos must be between 0 and size(), inclusive.
    void seek( size_t pos );
    //! Read up to "size" amount of data from the buffer, advancing the current 
    //! pointer. Return number of bytes read.
    //! \param outBuf Where read data will be copied into.
    //! \param size The maximum amount of data to copy.
    //! \note Less than size may be copied, if there is less data between the 
    //! current position and the end of the buffer.
    size_t read( void * outBuf, size_t size );
    //! Write up to "size" amount of data into the buffer, advancing the current 
    //! pointer. Return number of bytes written.
    //! \param inBuf What data to write into the buffer.
    //! \param size The maximum amount of data to copy.
    //! \note Less than size may be copied, if there is less space between the 
    //! current position and the end of the buffer.
    size_t write( void const * inBuf, size_t size );

    //! \return a pointer to the beginning of the buffer.
    unsigned char * begin();
    //! \copydoc Block::begin()
    unsigned char const * begin() const;
    //! \return a pointer to the end of the buffer (i e, the first non-writable 
    //! byte after the buffer block).
    unsigned char * end();
    //! \copydoc Block::end()
    unsigned char const * end() const;
    //! \return the size of the buffer, in bytes.
    size_t size() const;

    /*! Support writing by using operator overloading.
  \code
  Block a( buffer, bufferSize );
  int theInt = 4711;
  a << Block( &theInt, sizeof(theInt) ); // transfers theInt into the block (no endian fixup!)
  \endcode
      \note There is no way to see whether this write fails 
      other than calling eof() on the destination.
      \param o is the buffer to append to the end of this buffer.
    */
    Block & operator<<( Block const & o );

    /*! Support reading by using operator overloading.
  \code
  Block a( buffer, bufferSize );
  char x[10];
  Block b( x, 10 );
  a >> b; // transfers up to 10 bytes from a to b
  \endcode
      \note There is no way to see how much could be read, other than 
      calling eof() on me (or destination) to see whether there was too 
      little space in destination. Also note that 
      eof() will return true after this operation even if some data was 
      transferred (but not all); this is different from the semantics 
      of read().
      \param o is the buffer to write data to.
    */
    Block & operator>>( Block & o );

    //! \return TRUE if you have attempted to read when the position is 
    //! already at the eof(), or if you have attempted to use operator>>() 
    //! for more data than could be transferred.
    //!
    //! Also return TRUE if you have attempted to write any data that got 
    //! at least partially truncated.
    //!
    //! Cleared to FALSE by calling seek().
    //!
    //! \note The difference in semantic between a partial read() and a 
    //! partial operator>>()!
    bool eof() const;

  private:
    Block( Block const & o );   //!< \internal Not implemented
    Block& operator=( Block const & o );  //!< \internal Not implemented

    unsigned char * buf_; //!< \internal buffer base
    size_t size_;         //!< \internal buffer size
    size_t pos_;          //!< \internal current pos
    bool deleteIt_;       //!< \internal delete pointer at end
    bool atEof_;          //!< \internal whether EOF condition exists
};

//! The IMarhshalManager class organizes all structure data types that can 
//! be marshalled and demarshalled in the system.
//! Get the IMarshalManager to manage marshalling and de-marshalling 
//! for all your data types. There is only one instance of this 
//! interface.
//! \code
//! MyStruct s1, s2;
//! FillIn( s1 );
//! Buffer b( 100 );
//! IMarshalManager::instance()->marshal( s1, b );
//! b.seek( 0 );
//! IMarshalManager::instance()->demarshal( s2, b );
//! assert( s1 == s2 );
//! \endcode
//! Typically, you will create the actual marshallers through the 
//! MARSHAL_BEGIN_TYPE() macro and friends. See the source in the 
//! test.cpp file for more information about how it works in practice.
class IMarshalManager {
  public:

    //! Call IMarshalManager::startup() once in your program, after control 
    //! has reached main(), but before you attempt to get any marshallers or 
    //! marshal/de-marshal anything. This will ensure that marshallers that 
    //! reference other marshallers will be properly resolved. If this cannot 
    //! be done, a non-NULL error string is returned from startup(), describing 
    //! the problem.
    //! \return NULL for success, or a C string describing the problem for 
    //! error cases.
    static ETWORK_API char const * startup();

    //! \return the IMarshalManager singleton instance for this 
    //! process. \note that the marshaller is stateless, and thus really 
    //! doesn't need to be separated, even if you use more than one logical 
    //! process in the same physical process.
    static ETWORK_API IMarshalManager * instance();

    //! Use marshal() to marshal any datatype for which you have registered 
    //! a marshaller using the MARSHAL_BEGIN_TYPE() and MARSHAL_END_TYPE() 
    //! macros.
    //! \param src is the data structure to marshal.
    //! \param o is the buffer to marshal into.
    //! \return TRUE if marshal was successful (and fit into buffer); 
    //! false otherwise (and leaves buffer position where it was).
    template< class T > bool marshal( T const & src, Block & o );
    //! Use demarshal() to demarshal any datatype for which you have registered 
    //! a marshaller using the MARSHAL_BEGIN_TYPE() and MARSHAL_END_TYPE() 
    //! macros.
    //! \param dst is the data structure to demarshal into.
    //! \param o is the buffer to marshal out of.
    //! \return TRUE if marshal was successful; 
    //! false otherwise (and leaves buffer position where it was).
    template< class T > bool demarshal( T & dst, Block & o );

    //! Register a specific marshaller for a specific type name.
    //! \param type is the typeid().name() string for the type.
    //! \note The string "type" must be valid for the duration of your 
    //! program. You should make arrangements for making it so, if you 
    //! define custom type names. (typeid().name() is already static, 
    //! so that's valid)
    //! \param id is the numeric Id for the marshalled type. 
    //! It must be 0, to avoid associating an Id, or <em>positive</em> and unique for the type.
    //! \param m is the marshaller instance.
    //! \note You don't typically need to use this function.
    virtual void setMarshaller( char const * type, int id, marshaller::IMarshalResolve * m ) = 0;
    //! \return the specific marshaller registered for the type in question.
    //! \param type is the typeid().name() string for the type.
    //! \note You don't typically need to use this function.
    virtual IMarshaller * marshaller( char const * type ) = 0;
    //! \return the specific marshaller registered for the id in question.
    //! \param id is the previously registered id for the marshaller (must not be 0).
    //! \note You don't typically need to use this function.
    virtual IMarshaller * marshaller( int id ) = 0;

    //! \return how many structures are known to the marshal manager.
    //! \note This can be used as a primitive protocol versioning mechanism, 
    //! if you never remove any structures, only add new ones.
    virtual int countMarshallers() = 0;
};

//! Implement IMarhsaller for each data type you want to support 
//! serializing and de-serializing. Then register the typename of 
//! your marshalled type with an IMarshalManager.
//!
//! There are helper macros, including MARSHAL_BEGIN_TYPE() and 
//! MARSHAL_END_TYPE(), which makes registration of marshallers 
//! fairly painless.
class ETWORK_API IMarshaller {
  public:
    //! You implement marshal() to convert a data structure to a serialized format.
    //! \param src points at the data structure to marshal.
    //! \param dst points at the buffer to marshal into.
    //! \return how many bytes were put into the buffer, or 0 for failure.
    virtual size_t marshal( void const * src, Block & dst ) = 0;
    //! You implement demarshal() to convert a serialized format to actual instance 
    //! data. "dst" will have been properly constructed already.
    //! \param src contains data that you can read. It may contain more data 
    //! than you wrote initially, so if you need a variable size, you must provide 
    //! that as part of your marshalled format.
    //! \param dst Where to demarshal data to.
    //! \return Number of bytes used out of src, or 0 on failure. You must 
    //! destroy the instance if you have created it if you fail to demarshal.
    virtual size_t demarshal( Block & src, void * dst ) = 0;
    /*! Construct an instance of your type in the memory pointed at.
        Typically, you call placement new.
  \code
  // construct() for std::string marshaller
  void StdStringMarshaller::construct( void * memory ) {
    new( memory ) std::string();
  }
  \endcode
        \param memory The place to create the instance.
    */
    virtual void construct( void * memory ) = 0;
    /*! Destruct a previously constructed instance; typically by calling 
        in-place delete. The instance was previously constructed by a call 
        to demarshal() or construct().
  \code
  // destruct() for std::string marshaller
  void StdStringMarshaller::destruct( void * memory ) {
    ((std::string *)memory)->~std::string();
  }
  \endcode
        \param memory The place where the instance to be destroyed lives.
    */
    virtual void destruct( void * memory ) = 0;
    //! \return the maximum size of an instance in memory. Typically, just 
    //! sizeof(YourStruct).
    virtual size_t instanceSize() = 0;
    //! \return the maximum size of a marshalled representation of your 
    //! instance. For variable-size data, you must pick an appropriate upper 
    //! limit, and stick to it.
    virtual size_t maxMarshalledSize() = 0;

    //! Get the id registered for this marshaller.
    //! \return The id of this marshaller as registered with the IMarshalManager, 
    //! or 0 if it's not an id-registered interface.
    int id();
    //! Get the typename registered for this marshaller.
    //! \return The name of the marshaller as registered with the IMarshalManager.
    char const * name();
  protected:
    //! \internal Constructor for IMarshaller; clears to NULL.
    //! \internal \param typeName is the type name of this marshaller, and 
    //! must be valid through the duration of your program.
    IMarshaller( char const * typeName );
    //! \internal Virtual destructor for IMarshaller.
    virtual ~IMarshaller();
    //! \internal Configure the id returned by id().
    void setId( int id );
    //! \internal Configure the name returned by name().
    //! \internal \param name this pointer must be valid for the duration of your program.
    void setName( char const * name );
  private:
    friend class marshaller::MarshalManager;
    //! \internal Storage of id.
    int id_;
    //! \internal Storage of name.
    char const * name_;
};


//! Use MARSHAL_BEGIN_TYPE(Type) to start describing a specific type 
//! that you wish to be able to marshal. Finish up the description by 
//! using MARHSAL_END_TYPE(Type,Id).
//! \param Type is the type you want to support marshalling for.
//! \see MARSHAL_END_TYPE(), MARSHAL_INT(), MARSHAL_UINT(), MARSHAL_BOOL(), MARSHAL_FLOAT(), MARSHAL_DOUBLE(), MARSHAL_STRING(), MARSHAL_TYPE(), MARSHAL_UINT64()
/*!
  \code
  struct MyStruct {   //  to marshal
    int zeroToTen;
    float degrees;
  };
  MARSHAL_BEGIN_TYPE(MyStruct)
    MARSHAL_INT(zeroToTen,0,10)
    MARSHAL_FLOAT(degrees,0,360,0.1)
  MARSHAL_END_TYPE(MyStruct,1)
  \endcode
*/
#define MARSHAL_BEGIN_TYPE(Type) \
  template<> class Marshaller< Type > : public marshaller::TypeMarshal { \
    public: \
      typedef Type MyType; \
      Marshaller() \
        : marshaller::TypeMarshal( typeid(Type).name() ) \
      { \
        build(); \
      } \
      void build(); \
      static IMarshaller * instance(); \
      virtual void construct( void * memory ) { new( memory ) Type ; } \
      virtual void destruct( void * memory ) { ((Type *)memory)-> ~ Type (); } \
  }; \
  void Marshaller< Type >::build() { \
    description()

//! Use MARSHAL_END_TYPE(Type,Id) to finish describing a specific type to 
//! be marshalled, after starting it with MARSHAL_BEGIN_TYPE().
//! \param Type is the type to marshal.
//! \param Id is the ID of this type on the wire protocol, or 0 if the 
//! type does not make a message of its own on the wire.
//! \note Types with non-0 Id need to have unique Ids; the system will 
//! detect and abort if duplicates are found. Smaller Ids are more 
//! efficient.
#define MARSHAL_END_TYPE(Type,Id) \
    ; \
  } \
  IMarshaller * Marshaller< Type >::instance() { \
    static Marshaller< Type > m; \
    return &m; \
  } \
  static marshaller::MarshalRegistrar< Type > register_ ## Type( Id );

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_INT() describes an integer 
//! field within a struct/class you're marshalling. The field must be of 
//! type int (or binary interpretable as int, such as unsigned int).
//! \param name is the name of the field
//! \param min is the minimum integer value the field will take
//! \param max is the maximum integer value the filed will take (inclusive)
#define MARSHAL_INT(name,min,max) \
  .addInt(#name,offsetof(MyType,name),min,max)

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_UINT() describes an integer 
//! field within a struct/class you're marshalling. The field must be of 
//! type unsigned int (or binary interpretable as unsigned int, such as int).
//! \param name is the name of the field
//! \param bits is the number of bits that will be marshalled
#define MARSHAL_UINT(name,bits) \
  .addUint(#name,offsetof(MyType,name),bits)

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_INT() describes a boolean 
//! field within a struct/class you're marshalling. The field must be of 
//! type bool (native C++ bool type).
//! \param name is the name of the field
#define MARSHAL_BOOL(name) \
  .addBool(#name,offsetof(MyType,name))

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_FLOAT() describes a float 
//! field within a struct/class you're marshalling. The field must be of 
//! type float (32-bit floating point). The float will be marshalled as 
//! a fixed-point value, which will be scaled by prec; thus, really small 
//! values for prec combined with a large range may not actually fit an 
//! integer (nor be perfectly representable in a float).
//! \param name is the name of the field
//! \param min is the minimum value of the float
//! \param max is the maximum value of the float
//! \param prec is the precision (minimum representable delta) of the float
#define MARSHAL_FLOAT(name,min,max,prec) \
  .addFloat(#name,offsetof(MyType,name),min,max,prec)

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_DOUBLE() describes a double 
//! precision floating point field within a struct/class you're marshalling. 
//! The field must be of type double (64-bit floating point). The float will 
//! be marshalled as a binary value.
//! \param name is the name of the field
#define MARSHAL_DOUBLE(name) \
  .addDouble(#name,offsetof(MyType,name))

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_UINT64() describes a uint64
//! field within a struct/class you're marshalling. The field must be of 
//! type unsigned long long, and you can specify the number of bits to be
//! marshalled.
//! \param name is the name of the field
//! \param numBits is the number of bits being marshalled
#define MARSHAL_UINT64(name,numBits) \
  .addUint64(#name,offsetof(MyType,name),numBits)

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_STRING() describes a string 
//! field within a struct/class you're marshalling. The field must be of 
//! type std::string (STL string type of basic characters).
//! \param name is the name of the field
//! \param maxSize is the maximum number of characters in the string
#define MARSHAL_STRING(name,maxSize) \
  .addString(#name,offsetof(MyType,name),maxSize)

//! Used after MARSHAL_BEGIN_TYPE(), MARSHAL_TYPE() describes a typed 
//! field within a struct/class you're marshalling, using a user-defined 
//! marshalling type. The type must have a separate MARSHAL_BEGIN_TYPE() 
//! defined for it within the program, or an error will be detected at 
//! runtime.
//! \param type is the name of the type
//! \param name is the name of the field
#define MARSHAL_TYPE(type,name) \
  .addType<type>(#name,offsetof(MyType,name))


//!@}

//  I can't have "min" and "max" defined here.
//  Use std::min/std::max if you need those functions.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

//! \internal The tag used for Marshaller subclasses.
template< class Type > class Marshaller {};

//! \internal marshaller implementation namespace
namespace marshaller {

  //! \internal Descriptor for a struct member
  struct MemberDesc {
    char const * name_;
    char const * type_;
    size_t offset_;
    IMarshaller * marshaller_;
  };

  //! \internal An interface used to start up all declared marshallers.
  class IMarshalResolve {
    public:
      virtual IMarshaller * resolve( IMarshalManager * mgr ) = 0;
  };

  class MarshalOp;

  //! To make sure that Etwork can be used from a program that uses a different 
  //! flavor of the MS STL, no STL classes can be part of the public API.
  //! Thus, I have to implement a mini-version of vector that I can guarantee 
  //! will not change implementation between versions.
  template< typename T > class public_vector {
    public:
      //! Create a vector of a given type.
      public_vector() {
        data_ = 0;
        logical_ = 0;
        physical_ = 0;
      }
      //! Destroy the vector, and all objects in it.
      ~public_vector() {
        for (size_t i = 0; i < logical_; ++i) {
          (((T *)data_) + i)->~T();
        }
        ::operator delete(data_);
      }
      //! Copy a vector; will copy all objects.
      public_vector(public_vector<T> const &old) {
        logical_ = old.logical_;
        physical_ = logical_;
        data_ = ::operator new(old.logical_ * sizeof(T));
        for (size_t i = 0; i < logical_; ++i) {
          new ((T *)data_ + i) T(((T *)old.data_)[i]);
        }
      }
      //! Copy a vector to this vector; destroys all old objects, 
      //! and copies all objects in the argument.
      //! \param old The vector to copy.
      //! \return This vector, post-copy.
      public_vector<T> &operator=(public_vector<T> const &old) {
        if (&old == this) return *this;
        this->~public_vector<T>();
        new (this) public_vector<T>(old);
        return *this;
      }
      //! \return Number of elements in the vector.
      size_t size() const {
        return logical_;
      }
      //! \return A reference to an element in the vector, or the 
      //! "ghost" element at the end of the vector.
      T &operator[](size_t ix) {
        return ((T *)data_)[ix];
      }
      //! \copydoc public_vector::operator[]()
      T const &operator[](size_t ix) const {
        return ((T const *)data_)[ix];
      }
      //! Add an element to the end of the vector.
      //! \param t The element to add (copy) into the vector.
      void push_back(T const &t) {
        if (logical_ == physical_) {
          size_t nup = ((size_t)(physical_ * 1.25) + 5);
          void *nu = ::operator new(sizeof(T) * nup);
          for (size_t i = 0; i < logical_; ++i) {
            new ((T *)nu + i) T(((T *)data_)[i]);
            ((T *)data_ + i)->~T();
          }
          ::operator delete(data_);
          data_ = nu;
          physical_ = nup;
        }
        new ((T *)data_ + logical_) T(t);
        ++logical_;
      }
    private:
      void *data_;
      size_t logical_;
      size_t physical_;
  };

  //! \internal Used to store member descriptors.
  class ETWORK_API MemberDescVector : public public_vector<MemberDesc> {};
  //! \internal Used to implement the type marshalling macros.
  class ETWORK_API TypeMarshal : public IMarshalResolve, public IMarshaller {
    private:
      friend class MarshalOp;
      MemberDescVector descs_;
      size_t instanceSize_;
      size_t maxMarshalledSize_;

    public:
      TypeMarshal( char const * name ) : IMarshaller( name ), instanceSize_( 0 ), maxMarshalledSize_( 0 ) {}
      MarshalOp description();
      IMarshaller * resolve( IMarshalManager * mgr );

      virtual size_t marshal( void const * src, Block & dst );
      virtual size_t demarshal( Block & src, void * dst );
      virtual size_t instanceSize() { return instanceSize_; }
      virtual size_t maxMarshalledSize() { return maxMarshalledSize_; }
  };

  //! \internal Used to implement the marshalling macros.
  class ETWORK_API MarshalOp {
    public:
      MarshalOp( TypeMarshal * it ) : it_( it ) {}
      MarshalOp & addInt( char const * name, size_t offset, int min, int max );
      MarshalOp & addUint( char const * name, size_t offset, int bits );
      MarshalOp & addUint64( char const *name, size_t offset, int bits );
      MarshalOp & addFloat( char const * name, size_t offset, float min, float max, float prec );
      MarshalOp & addDouble( char const * name, size_t offset );
      MarshalOp & addBool( char const * name, size_t offset );
      MarshalOp & addString( char const * name, size_t offset, size_t maxSize );
      MarshalOp & addSomeType( char const * typeName, char const * name, size_t offset );
      template< class T > MarshalOp & addType( char const * name, size_t offset ) {
        return addSomeType( typeid(T).name(), name, offset );
      }
      TypeMarshal * it_;
  };

  inline MarshalOp TypeMarshal::description() {
    return MarshalOp( this );
  }

  template< class Type > class MarshalRegistrar {
    public:
      MarshalRegistrar( int id ) {
        static Marshaller< Type > m;
        IMarshalManager::instance()->setMarshaller( typeid(Type).name(), id, &m );
      }
  };
}


template< class T > bool IMarshalManager::marshal( T const & src, Block & o )
{
  IMarshaller * m = IMarshalManager::instance()->marshaller( typeid( T ).name() );
  return (m->marshal( &src, o ) != 0);
}

template< class T > bool IMarshalManager::demarshal( T & dst, Block & o )
{
  IMarshaller * m = IMarshalManager::instance()->marshaller( typeid( T ).name() );
  return (m->demarshal( o, &dst ) != 0);
}

#endif  //  etwork_marshal_h
