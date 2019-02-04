#ifndef __DEMO_ECHO_SERVER_MT_H
#define __DEMO_ECHO_SERVER_MT_H

#include <dbus-c++/dbus.h>
#include <dbus-c++/request-piper.h>
#include "echo-server-mt-glue.h"
#include <vector>
#include <utility>

class EchoServer
:public org::freedesktop::DBus::EchoDemo_adaptor,
    public DBus::IntrospectableAdaptor,
    public DBus::PropertiesAdaptor,
    public ::DBus::RequestPiper
{
public:

  EchoServer(DBus::Connection &connection);

  std::map< std::string, std::string > Info();

  int32_t Random();

  std::string Hello(const std::string &name);

  DBus::Variant Echo(const DBus::Variant &value);

  std::vector< uint8_t > Cat(const std::string &file);

  int32_t Sum(const std::vector<int32_t> & ints);
};

#endif//__DEMO_ECHO_SERVER_MT_H
