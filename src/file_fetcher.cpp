#include "mfsync/file_fetcher.h"

#include "mfsync/protocol.h"

namespace mfsync::multicast
{

  file_fetcher::file_fetcher(boost::asio::io_service& io_service,
                             const boost::asio::ip::address& listen_address,
                             const boost::asio::ip::address& multicast_address,
                             const short multicast_port,
                             mfsync::file_handler* file_handler)
    : socket_(io_service)
    , file_handler_(file_handler)
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
    if (!error)
    {
      spdlog::debug("Received Message: '{}'", std::string(data_, bytes_recvd));
      spdlog::debug("From: {}", boost::lexical_cast<std::string>(sender_endpoint_.address()));

      auto foo = mfsync::protocol::get_available_files_from_message(std::string(data_, bytes_recvd),
                                                              sender_endpoint_);

      if(foo.has_value())
      {
        file_handler_->add_available_files(std::move(foo.value()));
      }

      socket_.async_receive_from(boost::asio::buffer(data_, max_length), sender_endpoint_,
                                 boost::bind(&file_fetcher::handle_receive_from, this,
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      spdlog::error("Error in handle_receive_from: {}", error.message());
    }
  }
}
