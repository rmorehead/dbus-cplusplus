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

int32_t EchoServer::Sum(const std::vector<int32_t>& ints)
{
  int32_t sum = 0;

  for (size_t i = 0; i < ints.size(); ++i) sum += ints[i];

  return sum;
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
