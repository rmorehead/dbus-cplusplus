#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "echo-client.h"
#include <dbus-c++/eventloop.h>

#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <cstring>

using namespace std;

static const char *ECHO_SERVER_NAME = "org.freedesktop.DBus.Examples.Echo";
static const char *ECHO_SERVER_PATH = "/org/freedesktop/DBus/Examples/Echo";
#define ITERATION_COUNT 30

EchoClient::EchoClient(DBus::Connection &connection, const char *path, const char *name)
  : DBus::ObjectProxy(connection, path, name)
{
}

void EchoClient::Echoed(const DBus::Variant &value)
{
  cout << "<echoed>!";
}

void EchoClient::SumSignal(const int32_t& sum, const uint64_t& when)
{
    cout << "Signal: Sum " << sum << " When " << when << endl;
}

static const size_t THREADS = 3;

static ::DBus::DefaultMutex spin_mutex;
static int spin = THREADS;

EchoClient *g_client = NULL;

DBus::Pipe *thread_pipe_list[THREADS];
static pthread_t threads[THREADS];


DBus::BusDispatcher dispatcher;
DBus::DefaultTimeout *timeout;

void finish_thread(void) {
    //wait for all threads to be donew
    size_t i;
    for (i = 0; i < THREADS; ++i)
    {
        pthread_join(threads[i], NULL);
    }
    dispatcher.leave();
}


void *greeter_thread(void *arg)
{
  char idstr[16];
  size_t i = (size_t) arg;
  for (int j = 0 ; j < ITERATION_COUNT ; j++) {
      snprintf(idstr, sizeof(idstr), "%i", j);
      cout << "Writing to thread pipe " << i << " " << idstr << endl;
      thread_pipe_list[i]->write(idstr, strlen(idstr) + 1);
      cout << idstr << " done (" << i << ")" << endl;
  }
  return NULL;
}

void niam(int sig)
{
    bool do_finish = false;

    spin_mutex.lock();
    if (spin != 0) {
        spin = 0;
        do_finish = true;
    }
    spin_mutex.unlock();

    if (!do_finish) {
        return;
    }
    finish_thread();
}

static void
dec_spin(void) {
    bool do_finish = false;

    spin_mutex.lock();
    if (spin > 0) {
        spin --;
        if (spin == 0) {
            do_finish = true;
        }
    }
    spin_mutex.unlock();

    if (do_finish) {
        finish_thread();
    }
}

void handler1(const void *data, void *buffer, unsigned int nbyte)
{
  char *str = (char *) buffer;
  int in_num = atoi((char*)buffer);

  cout << "in_num1: " << in_num << ", size: " << nbyte << endl;
  cout << "call1: " << g_client->Hello(str) << endl;
  if (ITERATION_COUNT <= (in_num + 1)) {
      dec_spin();
  }
}

void handler2(const void *data, void *buffer, unsigned int nbyte)
{
  char *str = (char *) buffer;
  int in_num = atoi((char*)buffer);
  cout << "in_num2: " << in_num << ", size: " << nbyte << endl;
  cout << "call2: " << g_client->Hello(str) << endl;
  if (ITERATION_COUNT <= (in_num + 1)) {
      dec_spin();
  }
}

void handler3(const void *data, void *buffer, unsigned int nbyte)
{
  int in_num = atoi((char*)buffer);
  cout << "int_num3: " << in_num << ", size: " << nbyte << endl;
  std::vector<int32_t> ints;
  ints.push_back(5);
  ints.push_back(in_num);
  cout << "call3: " << g_client->Sum(ints) << endl;
  if (ITERATION_COUNT <= (in_num + 1)) {
      dec_spin();
  }
}

int main()
{
  size_t i;

  signal(SIGTERM, niam);
  signal(SIGINT, niam);

  DBus::_init_threading();

  DBus::default_dispatcher = &dispatcher;

  // increase DBus-C++ frequency
  new DBus::DefaultTimeout(100, false, &dispatcher);

  DBus::Connection conn = DBus::Connection::SessionBus();

  EchoClient client(conn, ECHO_SERVER_PATH, ECHO_SERVER_NAME);
  g_client = &client;

  thread_pipe_list[0] = dispatcher.add_pipe(handler1, NULL);
  thread_pipe_list[1] = dispatcher.add_pipe(handler2, NULL);
  thread_pipe_list[2] = dispatcher.add_pipe(handler3, NULL);
  for (i = 0; i < THREADS; ++i)
  {
      pthread_create(threads + i, NULL, greeter_thread, (void *) i);
  }

  dispatcher.enter();

  for (i = 0; i < THREADS; ++i) {
      dispatcher.del_pipe(thread_pipe_list[i]);
  }

  cout << "terminating" << endl;
  return 0;
}
