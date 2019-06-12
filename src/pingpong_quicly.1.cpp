#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/newb.hpp"
#include "caf/logger.hpp"
#include "newb_quicly.hpp"
#include "caf/policy/newb_raw.hpp"
#include "caf/io/broker.hpp"

using namespace caf;
using namespace caf::io;
using namespace caf::io::network;
using namespace caf::policy;

namespace {

using start_atom = atom_constant<atom("start")>;
using send_atom = atom_constant<atom("send")>;
using quit_atom = atom_constant<atom("quit")>;
using responder_atom = atom_constant<atom("responder")>;
using config_atom = atom_constant<atom("config")>;

struct state {
  actor responder;
  caf::io::connection_handle other;
  size_t messages = 0;
  uint32_t received_messages = 0;
  std::unordered_map<uint32_t, std::chrono::milliseconds> timestamps;

};


behavior server(stateful_broker<state>* self) {
  return {
    [=](actor responder) {
      self->state.responder = responder;
    },
    [=](const new_connection_msg& msg) {
      self->configure_read(msg.handle,
                            io::receive_policy::exactly(sizeof(uint32_t)));
      self->state.other = msg.handle;
    },
    [=](new_data_msg& msg) {
      /*
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.buf);
      bd(counter);
      */
      self->write(msg.handle, msg.buf.size(), msg.buf.data());
      self->flush(msg.handle);
    },
    [=](const connection_closed_msg&) {
      self->quit();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

behavior client(stateful_broker<state>* self, connection_handle hdl) {
  self->state.other = hdl;
  return {
    [=](start_atom, size_t messages, actor responder) {
      auto& s = self->state;
      s.responder = responder;
      s.messages = messages;
      self->configure_read(s.other, io::receive_policy::exactly(sizeof(uint32_t)));
      std::vector<char> buf;
      binary_serializer bs(self->system(), buf);
      bs(uint32_t(1));
      self->write(s.other, buf.size(), buf.data());
      self->flush(s.other);
    },
    [=](new_data_msg& msg) {
      auto& s = self->state;
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.buf);
      bd(counter);
      s.received_messages += 1;
      if (s.received_messages % 100 == 0)
        std::cerr << "got " << s.received_messages << std::endl;
      if (s.received_messages >= s.messages) {
        self->send(s.responder, quit_atom::value);
      } else {
        std::vector<char> buf;
        binary_serializer bs(self->system(), buf);
        bs(counter + 1);
        self->write(msg.handle, buf.size(), buf.data());
        self->flush(msg.handle);
      }
    },
    [=](const connection_closed_msg&) {
      self->quit();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}


behavior raw_server(stateful_newb<new_raw_msg, state>* self, actor responder) {
  self->state.responder = responder;
  auto stop = [=](std::string msg) {
    std::cerr << msg << std::endl;
    self->send(self->state.responder, quit_atom::value);
    self->stop();
    self->quit();
  };

  return {
    [=](new_raw_msg& msg) {
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      if (counter == 0xFFFFFFFF) {
        // end of test
        stop("test ended successfully");
      }
      auto whdl = self->wr_buf(nullptr);
      binary_serializer bs(&self->backend(), *whdl.buf);
      bs(counter);
    },
    [=](io_error_msg& msg) {
      stop("server got io error: " + to_string(msg.op));
    }
  };
}

behavior raw_client(stateful_newb<new_raw_msg, state>* self) {
  auto n = actor_clock::clock_type::now();
  auto timeout = std::chrono::milliseconds(25);
  self->clock().set_multi_timeout(n+timeout, self, io::transport_atom::value, 0);
  return {
    [=](start_atom, size_t messages, actor responder) {
      auto& s = self->state;
      s.responder = responder;
      s.messages = messages;
      self->configure_read(io::receive_policy::exactly(sizeof(uint32_t)));
      auto whdl = self->wr_buf(nullptr);
      binary_serializer bs(&self->backend(), *whdl.buf);
      bs(uint32_t(1));
    },
    [=](new_raw_msg& msg) {
      auto& s = self->state;
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      s.received_messages += 1;
      if (s.received_messages % 100 == 0) {
        std::cerr << "got " << s.received_messages << std::endl;
      }
      if (s.received_messages >= s.messages) {
        // tell server that test is over
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(uint32_t(0xFFFFFFFF));

        self->send(s.responder, quit_atom::value);
        self->stop();
        self->quit();
      } else {
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(counter + 1);
        auto ts = std::chrono::system_clock::now().time_since_epoch();
        s.timestamps.insert(s.received_messages, std::chrono::duration_cast<std::chrono::milliseconds>(ts));
      }
    },
    [=](io_error_msg& msg) {
      std::cerr << "client got io error: " << to_string(msg.op) << std::endl;
      self->stop();
      self->quit();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

class config : public actor_system_config {
public:
  uint16_t port = 4433;
  std::string host = "localhost";
  bool is_server = false;
  size_t messages = 10;
  bool traditional = false;

  config() {
    opt_group{custom_options_, "global"}
            .add(port, "port,P", "set port")
            .add(host, "host,H", "set host")
            .add(is_server, "server,s", "set server")
            .add(messages, "messages,m", "set number of exchanged messages")
            .add(traditional, "traditional,t", "use traditional style brokers");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = quicly_protocol<raw>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};

  auto await_done = [&](std::string msg) {
    self->receive([&](quit_atom) {
      std::cerr << msg << std::endl;
    });
  };
  if (cfg.is_server) {
    std::cerr << "creating server on port " << cfg.port << std::endl;
    accept_ptr<policy::new_raw_msg> pol{new accept_quicly<policy::new_raw_msg>};
    auto eserver = spawn_server<proto_t>(sys, raw_server, std::move(pol), port,
                                          nullptr, true, self);
    if (!eserver) {
      std::cerr << "failed to start server on port " << port << std::endl;
      return;
    }
    auto server = std::move(*eserver);
    await_done("done");
    std::cerr << "stopping server" << std::endl;
    self->send_exit(server, caf::exit_reason::user_shutdown);
  } else {
    std::cerr << "creating client" << std::endl;
    transport_ptr pol{new quicly_transport};
    auto eclient = spawn_client<proto_t>(sys, raw_client, std::move(pol),
                                          host, port);
    if (!eclient) {
      std::cerr << "failed to start client for " << host << ":" << port
                << std::endl;
      return;
    }
    auto client = std::move(*eclient);
    auto start = system_clock::now();
    self->send(client, start_atom::value, size_t(cfg.messages),
                actor_cast<actor>(self));
    await_done("done");
    auto end = system_clock::now();
    std::cout << duration_cast<milliseconds>(end - start).count() << "ms"
              << std::endl;
    self->send(client, exit_reason::user_shutdown);
  }
}

} // namespace anonymous

CAF_MAIN(io::middleman);