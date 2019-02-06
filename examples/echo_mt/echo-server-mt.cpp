#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "echo-server-mt.h"
#include <dbus-c++/interface.h> // for register_method
#include <dbus-1.0/dbus/dbus-protocol.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static const char *ECHO_SERVER_NAME = "org.freedesktop.DBus.Examples.Echo";
static const char *ECHO_SERVER_PATH = "/org/freedesktop/DBus/Examples/Echo";

# define register_method_sub(interface, method, callback) \
    EchoDemo_adaptor::_methods[ method ] =                             \
        new ::DBus::Callback< interface, ::DBus::Message, const ::DBus::CallMessage &>(this, & EchoServer :: callback);

EchoServer::EchoServer(DBus::Connection &connection)
    : ::DBus::RequestPiper(connection, ECHO_SERVER_PATH)
{

    ::DBus::debug_log("re-registering method\n");
    //remap to forwarding stub

    ::DBus::RequestPiper::origMethodTable = EchoDemo_adaptor::_methods;

    ::DBus::debug_log("Registering using methods %p\n", &(EchoDemo_adaptor::_methods));
    ::DBus::MethodTable::iterator it;
    for(it = EchoDemo_adaptor::_methods.begin(); it != EchoDemo_adaptor::_methods.end(); it++) {
        // iterator->first = key
        // iterator->second = value
        // Repeat if you also want to iterate through the second map.
        register_method_sub(EchoServer, it->first, _Forwarding_stub);
    }
}

int32_t EchoServer::Random()
{
  return rand();
}

std::string EchoServer::Hello(const std::string &name)
{
  return "Hello " + name + "!";
}

DBus::Variant EchoServer::Echo(const DBus::Variant &value)
{
  this->Echoed(value);

  return value;
}

std::vector< uint8_t > EchoServer::Cat(const std::string &file)
{
  FILE *handle = fopen(file.c_str(), "rb");

  if (!handle) throw DBus::Error("org.freedesktop.DBus.EchoDemo.ErrorFileNotFound", "file not found");

  uint8_t buff[1024];

  size_t nread = fread(buff, 1, sizeof(buff), handle);

  fclose(handle);

  return std::vector< uint8_t > (buff, buff + nread);
}


// Sum is implemented to demonstrate how to utilize the RequestPiper
// to both hand off sending results to yet another thread, as well as
// sending a signal from the non-dispatcher thread.
static void other_thread_sum(void* data);

// The Tag subclass is utilized because the new thread should NOT be
// started until we know the Tag utilized by return_later is registered, else the
// new thread might not be able to return the final result because the tag is not
// yet registered.

// In this case we use tag_registered to start the thread.
class TagThreadStarter : public DBus::Tag {
public:
    TagThreadStarter(const std::vector<int32_t>& ints, EchoServer& echo_server):
        _ints(ints),
        _echo_server(echo_server) {};

    //safe to start thread since tag has been registered
    virtual void tag_registered(void) const {
        ::pthread_t sum_pthread;
        //create and detach thread
        ::pthread_create(&sum_pthread, NULL,
                         (void* (*)(void*))&other_thread_sum, const_cast<TagThreadStarter*>(this));
        // detach the thread so it doesn't have to be joined and will cleanup itself.
        ::pthread_detach(sum_pthread);
    }

    const std::vector<int32_t> _ints;
    EchoServer& _echo_server;
};

// This does the work in the new thread
static void other_thread_sum(void* data) {
    TagThreadStarter* call_data (reinterpret_cast<TagThreadStarter*>(data));
    int32_t sum = 0;

    const std::vector<int32_t>& ints(call_data->_ints);

    for (size_t i = 0; i < ints.size(); ++i)
        sum += ints[i];

    call_data->_echo_server.SumSignal(sum, (int64_t)time(NULL));
    const ::DBus::CallMessage* call_msg = call_data->_echo_server.find_continuation_call_message(call_data);
    ::DBus::debug_log("%s find call_msg %p for tag %p", __func__, call_msg, call_data);
    ::DBus::ReturnMessage reply(*call_msg);
    ::DBus::MessageIter wi = reply.writer();
    wi << sum;
    call_data->_echo_server.return_now(call_data, reply);
    delete call_data;
}

int32_t EchoServer::Sum(const std::vector<int32_t>& ints)
{
  TagThreadStarter* call_data = new TagThreadStarter(ints, *this);
  return_later(call_data);
}



std::map< std::string, std::string > EchoServer::Info()
{
  std::map< std::string, std::string > info;
  char hostname[HOST_NAME_MAX];

  gethostname(hostname, sizeof(hostname));
  info["hostname"] = hostname;
  info["username"] = getlogin();

  return info;
}


void worker_thread_wrapper(EchoServer* server) {
    server->worker_thread();
}

DBus::BusDispatcher dispatcher;

void niam(int sig)
{
  dispatcher.leave();
}

int main()
{
  signal(SIGTERM, niam);
  signal(SIGINT, niam);

  DBus::default_dispatcher = &dispatcher;

  DBus::Connection conn = DBus::Connection::SessionBus();
  conn.request_name(ECHO_SERVER_NAME);

  EchoServer server(conn);
  server.start_pipe(dispatcher);

  pthread_t worker_pthread;
  pthread_create(&worker_pthread, NULL, (void* (*)(void*))&worker_thread_wrapper, &server);
  dispatcher.enter();
  server.stop_pipe(dispatcher);
  pthread_join(worker_pthread, NULL);
  return 0;
}
