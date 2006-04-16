#include <boost/function.hpp>
#include <boost/any.hpp>
#include <boost/shared_array.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <vector>
#include <map>
#include <stdexcept>
#include <string>

class FDMultiplexerException : public std::runtime_error
{
public:
  FDMultiplexerException(const std::string& str) : std::runtime_error(str)
  {}
};


/** Very simple FD multiplexer, based on callbacks and boost::any parameters
    As a special service, this parameter is kept around and can be modified, 
    allowing for state to be stored inside the multiplexer.

    It has some "interesting" semantics
*/

class FDMultiplexer
{
protected:
  typedef boost::function< void(int, boost::any&) > callbackfunc_t;
  struct Callback
  {
    callbackfunc_t d_callback;
    boost::any d_parameter;
    struct timeval d_ttd;
  };

public:
  FDMultiplexer() : d_inrun(false)
  {}
  virtual ~FDMultiplexer()
  {}

  virtual int run(struct timeval* tv) = 0;

  //! Add an fd to the read watch list - currently an fd can only be on one list at a time!
  virtual void addReadFD(int fd, callbackfunc_t toDo, boost::any parameter=boost::any())
  {
    this->addFD(d_readCallbacks, fd, toDo, parameter);
  }

  //! Add an fd to the write watch list - currently an fd can only be on one list at a time!
  virtual void addWriteFD(int fd, callbackfunc_t toDo, boost::any parameter=boost::any())
  {
    this->addFD(d_writeCallbacks, fd, toDo, parameter);
  }

  //! Remove an fd from the read watch list. You can't call this function on an fd that is closed already!
  /** WARNING: references to 'parameter' become invalid after this function! */
  virtual void removeReadFD(int fd)
  {
    this->removeFD(d_readCallbacks, fd);
  }

  //! Remove an fd from the write watch list. You can't call this function on an fd that is closed already!
  /** WARNING: references to 'parameter' become invalid after this function! */
  virtual void removeWriteFD(int fd)
  {
    this->removeFD(d_writeCallbacks, fd);
  }

  virtual void setReadTTD(int fd, struct timeval tv, int timeout)
  {
    if(!d_readCallbacks.count(fd))
      throw FDMultiplexerException("attempt to timestamp fd not in the multiplexer");
    tv.tv_sec += timeout;
    d_readCallbacks[fd].d_ttd=tv;
  }

  virtual std::vector<std::pair<int, boost::any> > getTimeouts(const struct timeval& tv)
  {
    std::vector<std::pair<int, boost::any> > ret;
    for(callbackmap_t::iterator i=d_readCallbacks.begin(); i!=d_readCallbacks.end(); ++i)
      if(i->second.d_ttd.tv_sec && boost::tie(tv.tv_sec, tv.tv_usec) > boost::tie(i->second.d_ttd.tv_sec, i->second.d_ttd.tv_usec)) 
	ret.push_back(std::make_pair(i->first, i->second.d_parameter));
    return ret;
  }

  typedef FDMultiplexer* getMultiplexer_t();
  typedef std::multimap<int, getMultiplexer_t*> FDMultiplexermap_t;

  static FDMultiplexermap_t& getMultiplexerMap()
  {
    static FDMultiplexermap_t theMap;
    return theMap;
  }
  
  virtual std::string getName() = 0;

protected:
  typedef std::map<int, Callback> callbackmap_t;
  callbackmap_t d_readCallbacks, d_writeCallbacks;

  virtual void addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, boost::any parameter)=0;
  virtual void removeFD(callbackmap_t& cbmap, int fd)=0;
  bool d_inrun;
  callbackmap_t::iterator d_iter;
};

class SelectFDMultiplexer : public FDMultiplexer
{
public:
  SelectFDMultiplexer()
  {}
  virtual ~SelectFDMultiplexer()
  {}

  virtual int run(struct timeval* tv);

  virtual void addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, boost::any parameter);
  virtual void removeFD(callbackmap_t& cbmap, int fd);
  std::string getName()
  {
    return "select";
  }
};

