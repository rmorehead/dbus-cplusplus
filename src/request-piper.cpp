#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Project */
#include <dbus-c++/interface.h> // for register_method
#include <dbus-1.0/dbus/dbus-protocol.h>
#include <dbus-c++/request-piper.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

using namespace DBus;

RequestPiper::RequestPiper(Connection &connection, const std::string&  server_path, const std::string& ia_name)
    : ObjectAdaptor(connection, server_path),
    InterfaceAdaptor(ia_name),
    response_bytes_count(0)
{
    //remap to forwarding stub

    debug_log("Creating pipe()\n");
    if (0 != pipe(request_pipefd)) {
        debug_log("pipe failed\n");
        exit(1);
    }
    debug_log("created request_pipefd[0] %i %p this %p\n", request_pipefd[0], request_pipefd, this);
    debug_log("created request_pipefd[1] %i %p this %p\n", request_pipefd[1], request_pipefd, this);
}

Message RequestPiper::_Forwarding_stub(const CallMessage &call){
    // If worker thread invoke the real think, else put in forwarding queue
    debug_log("Forwarding stub called\n");
    Tag* later_tag = new Tag();
    request_mutex.lock();
    request_queue.push_back(std::make_pair(CallMessage(call, false), later_tag));
    request_mutex.unlock();

    debug_log("preparing to call return_later for tag %p\n", later_tag);

    // Write a char to wake up the far side.
    debug_log("Wiring to request_pipefd[1] %i this %p\n", request_pipefd[1], this);
    int res = write(request_pipefd[1], "x", 1);
    if (res != 1) {
        debug_log("Failed to write\n");
        //write error return value here...
    }

    /* return_later() throws an exception which records the
      "continuation" Since this same thread will delete the
      continuation when the response pipe is written/read by the
      dispatcher/eventloop thread, is OK if the worker thread is has created the
      response before the continuation is written.
    */
    debug_log("Calling return_later for tag %p\n", later_tag);
    return_later(later_tag); //this throws exception
    debug_log("Should never get here\n");
}

void RequestPiper::do_dispatch(CallMessage& msg, Message& res, Tag* tag) {
    debug_log("server: do_dispatch() %p\n", tag);
    response_mutex.lock();
    response_queue.push_back(std::make_pair(CallMessage(msg, false), Message(res, false)));
    response_mutex.unlock();
    //wake up by sending tag
    debug_log("server: Writing tag %p to pipe\n", tag);
    response_pipe->write(&tag, sizeof(tag));
    debug_log("server: Done writing tag %p to pipe\n", tag);
}

void RequestPiper::do_send(CallMessage& msg, Message& res, Tag* tag) {
    debug_log("do_send() Looking up continuation for tag %p\n", tag);
    ObjectAdaptor::Continuation *my_cont = find_continuation(tag);
    debug_log("do_send() Found %p for tag %p\n", my_cont, tag);

    try {
        debug_log("do_send() copying data\n");
        res.reader().copy_data(my_cont->writer());
        debug_log("do_send() calling return_now.\n");
        return_now(my_cont);
        debug_log("do_send() deleting tag.\n");
        delete tag;
        debug_log("do_send() done deleting tag.\n");
    } catch (Error &e) {
        debug_log("do_send() DBus Exception.\n");
        ErrorMessage em(msg, e.name(), e.message());
        em.reader().copy_data(my_cont->writer());
        return_now(my_cont);
        delete tag;
    }
    debug_log("do_send() leaving normally.\n");
}

void RequestPiper::process_pipe_request(void) {

        //process the message if possible
        request_mutex.lock();
        if (!request_queue.size()) {
            request_mutex.unlock();
            return;
        }

        debug_log("%s About to unpack dbus call msg %i size %i\n", __FUNCTION__, request_queue[0].first.serial(),
                (int)request_queue.size());

        CallMessage msg = request_queue[0].first;
        Tag* tag = request_queue[0].second;
        request_queue.erase(request_queue.begin());
        request_mutex.unlock();

        debug_log("stolen code...\n");
        // stolen code
        const char *name = msg.member();
        try {
            MethodTable::iterator mi = origMethodTable.find(name);
            if (mi != origMethodTable.end())
            {
                Message res = mi->second.call(msg);
                do_dispatch(msg, res, tag);
            }
            else
            {
                Message res = ErrorMessage(msg, DBUS_ERROR_UNKNOWN_METHOD, name);
                do_dispatch(msg, res, tag);
            }
        }
        catch (Error &e)
        {
            ErrorMessage em(msg, e.name(), e.message());
            do_dispatch(msg, em, tag);
        }

}

void RequestPiper::check_pipe_request(void) {
    // Read from and process incoming request on pipe
    char buf_char;
    while (request_pipefd[0] != -1 && 1 != read(request_pipefd[0], &buf_char, 1)) {
        if (errno != EINTR) {
            debug_log("read failed errno %i\n", errno);
            return;
        }
    }
    debug_log("processing message in worker thread\n");
    process_pipe_request();
}

void RequestPiper::worker_thread(void) {
    //worker thread if not using poll/select
    while (request_pipefd[0] != -1) {
        debug_log("worker_thread reading request_pipefd[0] %i %p this %p\n", request_pipefd[0], request_pipefd, this);
        check_pipe_request();
    }
}

void RequestPiper::dispatcher_pipe_handler(void *buffer, unsigned int nbyte) {
    size_t read_offset = 0;
    Tag* curTag = NULL;

    // The pipe code ensures that the callback will be in same "chunk"
    // size as written, so we just need to confirm that this true.
    if (sizeof(curTag) != nbyte) {
        debug_log("%s expected size %i, received size %i\n",
                __FUNCTION__, (int)sizeof(curTag), (int)nbyte);
        return;
    }

    memcpy(&curTag, buffer, nbyte);

    // we have curTag filled out sufficiently, now we can pop the
    // associated values off of the response_queue
    response_mutex.lock();
    debug_log("About to unpack dbus call msg %i size %i\n", response_queue[0].first.serial(),
            (int)response_queue.size());
    CallMessage msg(response_queue[0].first);
    debug_log("About to unpack dbus response call msg %i\n", response_queue[0].second.serial());
    Message res(response_queue[0].second);
    response_queue.erase(response_queue.begin());
    response_mutex.unlock();
    debug_log("Sending dispatcher_pipe_handler %p %i\n", buffer, nbyte);
    do_send(msg, res, curTag);
}

void dispatcher_pipe_handler_wrapper(const void *data, void *buffer, unsigned int nbyte) {
    void* mutable_data = const_cast<void*>(data);
    RequestPiper* server = static_cast<RequestPiper*>(mutable_data);
    server->dispatcher_pipe_handler(buffer, nbyte);
}

void RequestPiper::start_pipe(BusDispatcher& dispatcher) {
    response_pipe = dispatcher.add_pipe (dispatcher_pipe_handler_wrapper, this);
}

void RequestPiper::stop_pipe(BusDispatcher& dispatcher) {
  if (response_pipe) {
      dispatcher.del_pipe(response_pipe);
      response_pipe = NULL;
  }
  //close the request pipe also
  close(request_pipefd[0]);
  request_pipefd[0] = -1;
  close(request_pipefd[1]);
  request_pipefd[1] = -1;
}

int RequestPiper::get_request_read_fd(void) const
{
    return request_pipefd[0];
}
