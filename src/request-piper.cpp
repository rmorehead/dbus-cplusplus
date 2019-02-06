#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Project */
#include <dbus-c++/interface.h> // for register_method
#include <dbus-1.0/dbus/dbus-protocol.h>
#include <dbus-c++/request-piper.h>
#include <dbus-c++/debug.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

using namespace DBus;

RequestPiper::RequestPiper(Connection &connection, const std::string&  server_path)
    : ObjectAdaptor(connection, server_path),
      _dispatcher_thread(pthread_self())
{
    _create_pipe();
}

RequestPiper::RequestPiper(Connection &connection, const std::string&  server_path, pthread_t dispatcher_thread)
    : ObjectAdaptor(connection, server_path),
      _dispatcher_thread(dispatcher_thread) {
    _create_pipe();
}

void RequestPiper::_create_pipe(void) {
    debug_log("Creating pipe()");
    if (0 != pipe(request_pipefd)) {
        debug_log("pipe failed");
        exit(1);
    }
    debug_log("created request_pipefd[0] %i %p this %p", request_pipefd[0], request_pipefd, this);
    debug_log("created request_pipefd[1] %i %p this %p", request_pipefd[1], request_pipefd, this);
}

Message RequestPiper::_Forwarding_stub(const CallMessage &call) {

    // If dispatcher thread invoke the real stub now, else put in
    // forwarding queue
    pthread_t this_thread = pthread_self();

    debug_log("Forwarding stub called");
    Tag* later_tag = new Tag();
    request_mutex.lock();
    request_queue.push_back(std::make_pair(CallMessage(call, false), later_tag));
    /* can release lock here because it doesn't matter if what is written to pipe
       matches what is in queue.
    */

    // Write a char to wake up the far side.
    int res = write(request_pipefd[1], "x", 1);
    if (res != 1) {
        debug_log("Failed to write");
        //write error return value here...
    }

    request_mutex.unlock();

    /* return_later() throws an exception which records the
      "continuation" Since this same thread will delete the
      continuation when the response pipe is written/read by the
      dispatcher/eventloop thread, is OK if the worker thread is has created the
      response before the continuation is written.
    */
    debug_log("Calling return_later for tag %p", later_tag);
    return_later(later_tag); //this throws exception
}

void RequestPiper::do_dispatch(const CallMessage& msg, Message& res, const Tag* tag) {
    debug_log("server: do_dispatch() %p", tag);
    response_n_signal_mutex.lock();
    response_queue.push_back(std::make_pair(CallMessage(msg, false), Message(res, false)));
    //wake up by sending tag
    response_n_signal_pipe->write(&tag, sizeof(tag));
    response_n_signal_mutex.unlock();
}

void RequestPiper::do_send(const CallMessage& msg, Message& res, const Tag* tag) {
    try {
        return_now(tag, res);
        delete tag;
    } catch (Error &e) {
        debug_log("do_send() DBus Exception.");
        ErrorMessage em(msg, e.name(), e.message());
        return_now(tag, em);
        delete tag;
    }
}

Message RequestPiper::_call_orig_method(const CallMessage &msg) {
    // stolen code
    const char *name = msg.member();
    MethodTable::iterator mi = origMethodTable.find(name);
    if (mi != origMethodTable.end())
    {
        Message res = mi->second.call(msg);
        return res;
    }
    else
    {
        Message res = ErrorMessage(msg, DBUS_ERROR_UNKNOWN_METHOD, name);
        return res;
    }
}

void RequestPiper::process_pipe_request(void) {

        //process the message if possible
        request_mutex.lock();
        if (!request_queue.size()) {
            debug_log("Request queue unexpectedly empty");
            request_mutex.unlock();
            return;
        }

        const CallMessage msg = CallMessage(request_queue[0].first, false);
        Tag* tag = request_queue[0].second;
        request_queue.erase(request_queue.begin());
        request_mutex.unlock();


        try {
            Message res = _call_orig_method(msg);
            do_dispatch(msg, res, tag);
        }
        catch (Error &e)
        {
            ErrorMessage em(msg, e.name(), e.message());
            do_dispatch(msg, em, tag);
        }
        catch (ReturnLaterError &rle)
        {
            debug_log("Pushing onto _pipe_continuations, pipe tag tag is %p", rle.tag);
            _pipe_continuations_mutex.lock();
            // use new tag to index, but store old tag in pair
            _pipe_continuations[rle.tag] = std::pair<CallMessage, const Tag*>(CallMessage(msg, false), tag);
            _pipe_continuations_mutex.unlock();
            // Let tag author know tag is registered
            rle.tag->tag_registered();
        }

}

void RequestPiper::check_pipe_request(void) {
    // Read from and process incoming request on pipe
    char buf_char;
    while (request_pipefd[0] != -1 && 1 != read(request_pipefd[0], &buf_char, 1)) {
        if (errno != EINTR) {
            debug_log("read failed errno %i", errno);
            return;
        }
    }
    process_pipe_request();
}

void RequestPiper::worker_thread(void) {
    //worker thread if not using poll/select
    while (request_pipefd[0] != -1) {
        check_pipe_request();
    }
}

void RequestPiper::dispatcher_pipe_handler(void *buffer, unsigned int nbyte) {
    size_t read_offset = 0;
    Tag* curTag = NULL;

    // The pipe code ensures that the callback will be in same "chunk"
    // size as written, so we just need to confirm that this true.
    if (sizeof(curTag) != nbyte) {
        debug_log("%s expected size %i, received size %i",
                __func__, (int)sizeof(curTag), (int)nbyte);
        return;
    }

    memcpy(&curTag, buffer, nbyte);

    // we have curTag filled out sufficiently, now we can pop the
    // associated values off of the response_queue
    if (curTag) {
        //it's a response message
        response_n_signal_mutex.lock();
        debug_log("About to unpack dbus response call msg %p size %i tag %p", response_queue[0].first,
                  (int)response_queue.size(), curTag);
        const CallMessage msg(response_queue[0].first, false);
        Message res(response_queue[0].second);
        response_queue.erase(response_queue.begin());
        response_n_signal_mutex.unlock();
        debug_log("Sending response dispatcher_pipe_handler %p %i", buffer, nbyte);
        do_send(msg, res, curTag);
    } else {
        //its a signal message if the tag is NULL
        response_n_signal_mutex.lock();
        debug_log("About to unpack dbus signal call msg %i size %i", signal_queue[0].serial(),
                  (int)signal_queue.size());
        SignalMessage msg(signal_queue[0]);
        signal_queue.erase(signal_queue.begin());
        response_n_signal_mutex.unlock();
        debug_log("Sending signal dispatcher_pipe_handler %p %i", buffer, nbyte);
        ObjectAdaptor::_emit_signal(msg);
    }
}

void dispatcher_pipe_handler_wrapper(const void *data, void *buffer, unsigned int nbyte) {
    void* mutable_data = const_cast<void*>(data);
    RequestPiper* server = static_cast<RequestPiper*>(mutable_data);
    server->dispatcher_pipe_handler(buffer, nbyte);
}

void RequestPiper::start_pipe(BusDispatcher& dispatcher) {
    response_n_signal_pipe = dispatcher.add_pipe (dispatcher_pipe_handler_wrapper, this);
}

void RequestPiper::stop_pipe(BusDispatcher& dispatcher) {
  if (response_n_signal_pipe) {
      dispatcher.del_pipe(response_n_signal_pipe);
      response_n_signal_pipe = NULL;
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

void RequestPiper::_emit_signal(SignalMessage &sig)
{
    debug_log("server: _emit_signal()");
    pthread_t this_thread = pthread_self();
    if (pthread_equal(this_thread, _dispatcher_thread)) {
        // Don't use piping to get to dispatcher thread if we are
        // in dispatcher thread.
        debug_log("_emit() Same thread dispatching locally");
        ObjectAdaptor::_emit_signal(sig);
        return;
    }

    response_n_signal_mutex.lock();
    signal_queue.push_back(SignalMessage(sig, false));
    //wake up by sending tag
    Tag* tag = NULL;
    response_n_signal_pipe->write(&tag, sizeof(tag));
    response_n_signal_mutex.unlock();
}

void RequestPiper::return_now(const Tag *tag, Message _return) {
    _pipe_continuations_mutex.lock();
    PipeContinuationMap::iterator pi = _pipe_continuations.find(tag);
    if (pi == _pipe_continuations.end()) {
        // up call
        debug_log("%s Did not find pipe continuation for %p", __FUNCTION__, tag);
        _pipe_continuations_mutex.unlock();
        return ObjectAdaptor::return_now(tag, _return);
    }


    // Found it, so we send it to the pipe like we usually would
    const CallMessage call_msg(pi->second.first, false);
    const Tag* orig_tag  = pi->second.second;
    _pipe_continuations.erase(pi);
    _pipe_continuations_mutex.unlock();
    debug_log("%s Found pipe continuation for tag %p orig_tag %p call_msg %p", __FUNCTION__, tag, orig_tag,
        call_msg);
    do_dispatch(call_msg, _return, orig_tag);
}

const CallMessage* RequestPiper::find_continuation_call_message(const Tag *tag) {
    _pipe_continuations_mutex.lock();
    PipeContinuationMap::iterator pi = _pipe_continuations.find(tag);
    if (pi == _pipe_continuations.end()) {
        // not in request piper, must be a generic ObjectAdaptor tag.
        _pipe_continuations_mutex.unlock();
        // up call
        return ObjectAdaptor::find_continuation_call_message(tag);
    }

    const CallMessage& res(pi->second.first);
    _pipe_continuations_mutex.unlock();
    return &res;
}
