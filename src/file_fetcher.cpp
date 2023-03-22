#include "mfsync/file_fetcher.h"

#include "mfsync/client_session.h"
#include "mfsync/protocol.h"

namespace mfsync::multicast
{

  file_fetcher::file_fetcher(boost::asio::io_service& io_service,
                             const boost::asio::ip::address& listen_address,
                             const boost::asio::ip::address& multicast_address,
                             const short multicast_port,
                             mfsync::file_handler* file_handler,
                             mfsync::crypto::crypto_handler& crypto_handler)
    : io_context_(io_service)
    , socket_(io_service)
    , file_handler_(file_handler)
    , crypto_handler_(crypto_handler)
  {
    boost::asio::ip::udp::endpoint listen_endpoint(
        listen_address, multicast_port);
    socket_.open(listen_endpoint.protocol());
    socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    socket_.bind(listen_endpoint);

    socket_.set_option(
        boost::asio::ip::multicast::join_group(multicast_address));

    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), sender_endpoint_,
        boost::bind(&file_fetcher::handle_receive_from, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
  }


  void file_fetcher::handle_receive_from(const boost::system::error_code& error, size_t bytes_recvd)
  {
    std::scoped_lock lk{mutex_};
    if (error)
    {
      spdlog::error("Error in handle_receive_from: {}", error.message());
      return;
    }

    spdlog::trace("Received Message: '{}'", std::string(data_, bytes_recvd));
    spdlog::trace("From: {}", boost::lexical_cast<std::string>(sender_endpoint_.address()));

    auto host_info = mfsync::protocol::get_host_info_from_message(std::string(data_, bytes_recvd),
                                                                  sender_endpoint_);

    if(!host_info.has_value())
    {
      do_receive();
      return;
    }

    spdlog::debug("received host info, ip: {}, port: {}, pubkey: {}",
                  host_info.value().ip,
                  host_info.value().port,
                  host_info.value().public_key);

    if(list_host_infos_)
    {
      print_host_if_new(host_info.value());
      do_receive();
      return;
    }

    if(crypto_handler_.trust_key(host_info.value().public_key))
    {
      const auto session =
          std::make_shared<mfsync::filetransfer::client_encrypted_file_list>(
            io_context_,
            *file_handler_,
            crypto_handler_,
            host_info.value());
      session->start_request();
    }

    do_receive();
  }
}
