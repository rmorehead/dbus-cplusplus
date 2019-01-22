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

  The RequestPiper class assists in sending requests from the
  per-process singleton distributer thread to another thread in the
  same process to be serviced: your "Service Worker Thread".

  The RequestPiper subclass, your service, will need to update the
  _methods map to point any methods that need to be handled by the
  Service Worker Thread to use the RequestPiper::_Forwarding_stub
  rather than the generated "glue" stub which calls your "real"
  service class method.

  Your RequestPiper subclass instance needs to know the correct,
  original method to call once your Service Worker Thread receives
  notification via the pipe and request queue, so prior to remapping
  to the _Forwarding_stub you must copy the original __methods table
  map into the origMethodTable instance variable.

  During runtime, when your re-mapped method is called, the
  _Forwarding_stub defers processing of the request by adding the
  DBus-C++ CallMessage request object to the request_queue and the
  service worker thread is alerted by using the RequestPipe as the
  notification mechanism, finally the Object::return_later() method
  tells DBus C++ that this call will responsed to in the future.

  Your worker thread is altered to queued requests by characters
  written to the request_pipe_fd so your worker thread must monitor
  the pipe via poll()/select or it can use
  RequestPiper::worker_thread() as your worker thread loop, which will
  automatically call RequestPiper::check_pipe_request() when data is
  available.

  If you are doing your own poll()/select() use get_request_read_fd()
  to find the FD to monitor, then when data is available for that FD,
  use RequestPiper::check_pipe_request() will read the request pipe
  data and process the pending requests in the queue.

  In a similar manner the response is sent from the servicing thread
  back to the distributer thread, but this is handled via
  check_pipe_request() and DBus::Pipe().

  The numbers in the following diagram indicate data flow order


        +---<--{ response_pipe_fd }--<---+
        |                                |
        |   +---<{ request_queue }<---+  |      +----<  DBus Client Request
        |   |                         |  |      |
      4 v   v 5                     2 ^  ^ 3    v 1
  [ Service Worker Thead ]    [ Dispatcher Thread ]
      7 v   v 6                     9 ^  ^ 8    v 10
        |   |                         |  |      |
        |   +--->{ response_queue }>--+  |      +----> DBus Client Reseponse
        |                                |
        +---->---{ response_pipe }--->---+

 */

namespace DBus
{


class DXXAPI RequestPiper
: public ObjectAdaptor,
    virtual InterfaceAdaptor

{
public:
    RequestPiper(Connection &connection, const std::string&  server_path, const std::string& ia_name);

    void worker_thread(void);
    void do_send(CallMessage& msg, Message& res, Tag* tag);
    void do_dispatch(CallMessage& msg, Message& res, Tag* tag);

    void start_pipe(BusDispatcher& dispatcher);
    void stop_pipe(BusDispatcher& dispatcher);
    void dispatcher_pipe_handler(void *buffer, unsigned int nbyte);
    int get_request_read_fd(void) const;

    // stub for subclass use
    Message _Forwarding_stub(const CallMessage &call);
    // since we replace the original registration, we need to remember it elsewhere...
    MethodTable origMethodTable;
private:


    std::vector< std::pair<CallMessage, Tag*> > request_queue;
    DefaultMutex request_mutex;

    //Tag* will be passed via pipe
    std::vector< std::pair<CallMessage, Message > > response_queue;
    DefaultMutex response_mutex;
    Pipe* response_pipe;
    void* response_bytes;
    size_t response_bytes_count;

    void process_pipe_request(void);
    void check_pipe_request(void);

    int request_pipefd[2];
};

} /* namespace DBus */
#endif //__REQUEST_PIPER_H

