// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include <microfmt/inspector/gdb_decoders.hpp>
#include <microfmt/inspector/gdb_encoders.hpp>
#include <microfmt/inspector/gdb_stream.hpp>
#include <microfmt/scratch_allocator.hpp>

using boost::asio::ip::tcp;

/**
 * @brief A simple synchronous GDB client wrapper over a Boost.Asio socket.
 */
class gdb_client {
public:
  gdb_client(boost::asio::io_context &io_context, const std::string &host, const std::string &port)
      : m_socket(io_context) {
    tcp::resolver resolver(io_context);
    std::cout << "[*] Connecting to GDB server at " << host << ":" << port << "...\n";
    boost::asio::connect(m_socket, resolver.resolve(host, port));
    std::cout << "[+] Connected!\n";
  }

  /**
   * @brief Encodes and sends a packet to the GDB server.
   */
  void send_request(microfmt::gdb::packet_type type, const microfmt::gdb::client_request_view &view = {}) {
    // Stack buffer for packet framing
    char tx_buffer[1024];
    microfmt::gdb::gdb_packet_writer writer(tx_buffer);

    // Encode the request payload directly into the writer's sink
    microfmt::gdb::client_request_encoder::encode(writer.as_sink(), type, view);

    // Finalize framing (adds '$' and '#XX' checksum)
    microfmt::string_view framed_packet = writer.finalize();

    if (framed_packet.empty()) {
      throw std::runtime_error("TX buffer overflow");
    }

    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    std::cout << " -> " << std::string_view(framed_packet.data(), framed_packet.size()) << "\n";

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    // Send over TCP
    boost::asio::write(m_socket, boost::asio::buffer(framed_packet.data(), framed_packet.size()));
  }

  /**
   * @brief Reads byte-by-byte from TCP, decoding the server's response.
   */
  void receive_response(microfmt::scratch_allocator &scratch) {
    scratch.reset();
    const std::size_t payload_capacity = scratch.available();
    char *payload_buffer = scratch.allocate<char>(payload_capacity);
    if (payload_buffer == nullptr) {
      throw std::runtime_error("RX scratch buffer is empty");
    }

    microfmt::gdb::gdb_streaming_decoder decoder(microfmt::span<char>(payload_buffer, payload_capacity));

    char c;
    bool in_packet = false; // Track if we are inside a $...# frame

    while (true) {
      // Read exactly 1 byte from the TCP socket
      boost::asio::read(m_socket, boost::asio::buffer(&c, 1));

      // Only treat +/- as ACK/NAK if we are OUTSIDE a packet
      if (!in_packet && (c == '+' || c == '-')) {
        std::cout << " <- [" << (c == '+' ? "ACK" : "NAK") << "]\n";
        if (c == '-') {
          std::cout << "[!] Server rejected packet (NAK).\n";
        }
        continue;
      }

      // Detect packet start
      if (c == '$') {
        in_packet = true;
      }

      // Feed the decoder
      auto status = decoder.feed(c);

      if (status == microfmt::gdb::gdb_streaming_decoder::status::ready) {
        in_packet = false; // Packet complete

        microfmt::string_view raw_payload = decoder.payload();
        MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

        std::cout << " <- $" << std::string_view(raw_payload.data(), raw_payload.size()) << "#XX\n";

        MICROFMT_END_UNSAFE_BUFFER_USAGE;

        // Parse the response into a structured view
        microfmt::gdb::server_response_view resp_view;
        auto resp_type = microfmt::gdb::server_response_decoder::decode(raw_payload, resp_view);

        print_decoded_response(resp_type, resp_view);
        return;
      } else if (status == microfmt::gdb::gdb_streaming_decoder::status::error_checksum) {
        in_packet = false;
        std::cerr << "[!] Checksum error received. Sending NAK.\n";
        boost::asio::write(m_socket, boost::asio::buffer("-", 1));
        decoder.reset();
      } else if (status == microfmt::gdb::gdb_streaming_decoder::status::error_overflow) {
        throw std::runtime_error("RX Scratch buffer overflow");
      } else if (status == microfmt::gdb::gdb_streaming_decoder::status::error_format) {
        in_packet = false;
        std::cerr << "[!] Format error received. Sending NAK.\n";
        boost::asio::write(m_socket, boost::asio::buffer("-", 1));
        decoder.reset();
      }
    }
  }
  /**
   * @brief Acknowledge a received packet.
   */
  void send_ack() { boost::asio::write(m_socket, boost::asio::buffer("+", 1)); }

private:
  tcp::socket m_socket;

  void print_decoded_response(microfmt::gdb::server_response_type type,
                              const microfmt::gdb::server_response_view &view) {
    std::cout << "    [Decoded] ";
    switch (type) {
    case microfmt::gdb::server_response_type::ok:
      std::cout << "OK\n";
      break;
    case microfmt::gdb::server_response_type::empty:
      std::cout << "Empty (Unsupported Command)\n";
      break;
    case microfmt::gdb::server_response_type::error:
      std::cout << "Error Code: 0x" << std::hex << static_cast<int>(view.status_code) << std::dec << "\n";
      break;
    case microfmt::gdb::server_response_type::stop_signal:
      std::cout << "Target Stopped. Signal: " << static_cast<int>(view.status_code) << "\n";
      if (!view.stop_reason_extra.empty()) {
        MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

        std::cout << "              Extra: "
                  << std::string_view(view.stop_reason_extra.data(), view.stop_reason_extra.size()) << "\n";

        MICROFMT_END_UNSAFE_BUFFER_USAGE;
      }
      break;
    case microfmt::gdb::server_response_type::raw_string:
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

      std::cout << "Data: " << std::string_view(view.text.data(), view.text.size()) << "\n";

      MICROFMT_END_UNSAFE_BUFFER_USAGE;

      break;
    default:
      std::cout << "Other complex response\n";
      break;
    }
  }
};

int main() {
  try {
    boost::asio::io_context io_context;

    char receive_storage[1024];
    microfmt::scratch_allocator receive_scratch(receive_storage);

    // Connect to local GDB server (e.g., standard QEMU port 1234 or gdbserver port 3333)
    gdb_client client(io_context, "127.0.0.1", "1234");

    // --- Step A: Send qSupported ---
    std::cout << "\n--- Querying Supported Features ---\n";
    microfmt::gdb::client_request_view q_view;
    q_view.extra_text = "multiprocess+;qRelocInsn+";

    client.send_request(microfmt::gdb::packet_type::query_supported, q_view);
    client.receive_response(receive_scratch);
    client.send_ack();

    // --- Step B: Ask why the target is halted ---
    std::cout << "\n--- Requesting Halt Reason ---\n";
    client.send_request(microfmt::gdb::packet_type::halt_reason);
    client.receive_response(receive_scratch);
    client.send_ack();

    // --- Step C: Read General Registers ---
    std::cout << "\n--- Reading General Registers ---\n";
    client.send_request(microfmt::gdb::packet_type::read_general_registers);
    client.receive_response(receive_scratch);
    client.send_ack();

  } catch (const std::exception &e) {
    std::cerr << "\n[Fatal Error] " << e.what() << "\n";
    return 1;
  }

  return 0;
}