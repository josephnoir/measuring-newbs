//
// Created by boss on 9/28/18.
//

#include "mozquic_CB.h"

// contains things to receive data
int connectionCB_transport(void* closure, uint32_t event, void* param) {
  auto clo = static_cast<transport_closure*>(closure);

  switch (event) {
    // receive data from stream
    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      if (!clo) {
        std::cout << "closure was null" << std::endl;
        break;
      }
      mozquic_stream_t* stream = param;
      if (mozquic_get_streamid(stream) & 0x3)
        break;
      uint32_t received = 0;
      int fin = 0;
      do {
        int code = mozquic_recv(stream,
                                clo->receive_buffer,
                                static_cast<uint32_t>(clo->len),
                                &received,
                                &fin);
        if (code != MOZQUIC_OK)
          return code;
        clo->amount_read += received; // gather amount that was read
      } while (received > 0 && !fin);
      break;
    }

    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      return mozquic_destroy_connection(param);

    default:
      break;
  }
  return MOZQUIC_OK;
}

// doesnt do anything. needed to interrupt receiving data, while triggering IO!
int connectionCB_send(void*, uint32_t event, void* param) {
  if(event == MOZQUIC_EVENT_ERROR ||
     event == MOZQUIC_EVENT_CLOSE_CONNECTION)
    return mozquic_destroy_connection(param);

  return MOZQUIC_OK;
}

int connectionCB_accept(void* closure, uint32_t event, void* param) {
  auto clo = static_cast<accept_closure*>(closure);
  switch (event) {
    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:
      mozquic_set_event_callback(param, connectionCB_transport);
      clo->new_connection = param;
      break;

    // destroy connection_ip4 in case of error or close event
    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      return mozquic_destroy_connection(param);

    default:
      break;
  }

  return MOZQUIC_OK;
}

int connectionCB_connect(void* closure, uint32_t event, void*) {
  auto clo = static_cast<transport_closure*>(closure);
  switch (event) {
    case MOZQUIC_EVENT_0RTT_POSSIBLE:
      std::cout << "0RTT possible" << std::endl;
    case MOZQUIC_EVENT_CONNECTED:
      std::cout << "client connected" << std::endl;
      clo->connected = true;
      break;

    default:
      break;
  }
  return MOZQUIC_OK;
}