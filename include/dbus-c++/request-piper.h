/*
 *
 *  D-Bus++ - C++ bindings for D-Bus
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __REQUEST_PIPER_H
#define __REQUEST_PIPER_H

#include "dbus.h"
#include "eventloop.h" //for DefaultMutex
#include "message.h" //for Message
#include "util.h" //for Slot
#include <vector>
#include <utility>

/**

  DBUS HANDLING REQUESTS IN A WORKER THREAD
  -----------------------------------------

  The RequestPiper class assists in sending requests from the
  per-process singleton distributer thread to another thread in the
  same process to be serviced: your "Service Worker Thread".

  This approach is advantageous because blocking the dispatcher thread
  prevents your DBus process from handling other requests or receiving
  DBUS signals while your dispatcher thread is blocked.

  The RequestPiper subclass, your service, will need to update the
  _methods map to point any methods that need to be handled by the
  Service Worker Thread to use the RequestPiper::_Forwarding_stub
  rather than the generated "glue" stub which calls your "real"
  service class method.

  For an example of utilizing the above mechanism, see
  echo-server-mt.cpp in the echo_mt example.

  Your RequestPiper subclass instance needs to know the correct,
  original method to call once your Service Worker Thread receives
  notification via the pipe and request queue, so prior to remapping
  to the _Forwarding_stub you must copy the original __methods table
  map into the origMethodTable instance variable.

  During runtime, when your re-mapped method is called, (1) Diagram A
  below, the _Forwarding_stub defers processing of the request by
  adding the DBus-C++ CallMessage request object to the request_queue,
  (2) in the below diagram, and the service worker thread is alerted
  by using the RequestPipe as the notification mechanism, (3) in the
  diagram below. Finally the initial dispatcher thread processing
  finishes by calling Object::return_later() method which tells DBus
  C++ that this call will responded to in the future.

  Your worker thread is altered to queued requests by characters
  written to the request_pipefd, (4) in the following diagram, so
  your worker thread must monitor the pipe via poll()/select or it can
  use RequestPiper::worker_thread() as your worker thread loop, which
  will automatically call RequestPiper::check_pipe_request() when data
  is available.

  If you are doing your own poll()/select() use get_request_read_fd()
  to find the FD to monitor, then when data is available for that FD,
  use RequestPiper::check_pipe_request() will read the request pipe
  data and process the pending requests in the queue, (5) in the below
  diagram.

  At this point your normal DBus C++ stub implementation will be
  called in the worker thread.

  Once the stub is finished, the response is placed into the
  response_queue, (6) in the diagram below, and then the
  response_n_signal_pipe is written, (7).

  At that point the dispatcher thread, which is monitoring the
  response_n_signal_pipe, will read the data off the
  response_n_signal_pipe, (8), and pop the response from the
  response_queue (9). The response is then sent via normal DBus C++
  mechanisms (10).

        +---<--------{ request_pipefd }---<-------+
        |                                         |
        |   +-------<{ request_queue }<--------+  |      +----<  DBus Client Request
        |   |                                  |  |      |
      4 v   v 5                              2 ^  ^ 3    v 1
  [ Service Worker Thread ]               [ Dispatcher Thread ]
      7 v   v 6                              9 ^  ^ 8    v 10
        |   |                                  |  |      |
        |   +-------->{ response_queue }>------+  |      +----> DBus Client Response
        |                                         |
        +---->---{ response_n_signal_pipe }--->---+

  Diagram A: Request Processing Using Dispatcher Thread and Worker Thread


  DBUS SIGNAL SENDING
  -------------------

  The RequestPiper class also overrides _emit_signal to ensure that
  worker thread is not blocked by any outgoing signal back pressure.

  When _emit_signal is called in the worker thread the signal message
  is placed into the signal_queue, (1) in Diagram B below, and then
  the response_n_signal_pipe is written, (2).

  At that point the dispatcher thread, which is monitoring the
  response_n_signal_pipe, will read the data off the
  response_n_signal_pipe, (3), and pop the signal from the
  signal_queue (4). The signal is then sent via _emit_signal() in the
  dispatcher thread, and is send to the DBus daemon as a signal (5).


  [ Service Worker Thead ]            [ Dispatcher Thread ]
      2 v   v 1                          4 ^  ^ 3    v 5
        |   |                              |  |      |
        |   +----->{ signal_queue }>-------+  |      +----> DBus Signal
        |                                     |
        +--->--{ response_n_signal_pipe }-->--+

  Diagram B: Signal Sending From Worker Thread

 */

namespace DBus
{

// so that code can transition to the additional required parameter.
class DXXAPI RequestPiper
    : public ObjectAdaptor
{
public:
    RequestPiper(Connection &connection, const std::string&  server_path);
    /* can't use default argument for dispatcher_thread since
      pthread_t has no portable NULL pthread_t equivalent, so we can't
      cleanly specify a constant to represent "default to current
      thread" argument value.
    */
    RequestPiper(Connection &connection, const std::string&  server_path, pthread_t dispatcher_thread);
    void worker_thread(void);
    void do_send(CallMessage& msg, Message& res, Tag* tag);
    void do_dispatch(CallMessage& msg, Message& res, Tag* tag);

    void start_pipe(BusDispatcher& dispatcher);
    void stop_pipe(BusDispatcher& dispatcher);
    void check_pipe_request(void);
    void dispatcher_pipe_handler(void *buffer, unsigned int nbyte);
    int get_request_read_fd(void) const;

    // stub for subclass use
    Message _Forwarding_stub(const CallMessage &call);
    Message _call_orig_method(const CallMessage &call);
    // since we replace the original registration, we need to remember it elsewhere...
    MethodTable origMethodTable;

protected:
    virtual void _emit_signal(SignalMessage &sig);

private:

    std::vector< std::pair<CallMessage, Tag*> > request_queue;
    DefaultMutex request_mutex;

    //Tag* will be passed via pipe
    std::vector< std::pair<CallMessage, Message > > response_queue;
    DefaultMutex response_n_signal_mutex;
    Pipe* response_n_signal_pipe;
    void* response_n_signal_bytes;

    std::vector< SignalMessage > signal_queue;

    void _create_pipe(void);

    void process_pipe_request(void);
    int request_pipefd[2];
    pthread_t _dispatcher_thread;
};

} /* namespace DBus */
#endif //__REQUEST_PIPER_H

