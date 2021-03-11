#include <iostream>

#include <boost/program_options.hpp>
#include "spdlog/spdlog.h"


#include <sstream>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include "boost/bind.hpp"
#include "boost/date_time/posix_time/posix_time_types.hpp"

const short multicast_port = 30001;
const int max_message_count = 100;

class receiver
{
public:
  receiver(boost::asio::io_service& io_service,
      const boost::asio::ip::address& listen_address,
      const boost::asio::ip::address& multicast_address)
    : socket_(io_service)
  {
    // Create the socket so that multiple may be bound to the same address.
    boost::asio::ip::udp::endpoint listen_endpoint(
        listen_address, multicast_port);
    socket_.open(listen_endpoint.protocol());
    socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    //socket_.set_option(boost::asio::ip::multicast::enable_loopback(true));
    socket_.bind(listen_endpoint);

    // Join the multicast group.
    socket_.set_option(
        boost::asio::ip::multicast::join_group(multicast_address));

    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), sender_endpoint_,
        boost::bind(&receiver::handle_receive_from, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
  }


  void handle_receive_from(const boost::system::error_code& error,
      size_t bytes_recvd)
  {
    if (!error)
    {
      spdlog::info("Received Message: '{}'", std::string(data_, bytes_recvd));

      socket_.async_receive_from(
          boost::asio::buffer(data_, max_length), sender_endpoint_,
          boost::bind(&receiver::handle_receive_from, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      spdlog::error("Error in handle_receive_from: {}", error.message());
    }
  }

private:
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint sender_endpoint_;
  enum { max_length = 1024 };
  char data_[max_length];
};

class sender
{
public:
  sender(boost::asio::io_service& io_service,
      const boost::asio::ip::address& multicast_address)
    : endpoint_(multicast_address, multicast_port),
      socket_(io_service, endpoint_.protocol()),
      timer_(io_service),
      message_count_(0)
  {
    std::ostringstream os;
    os << "Message " << message_count_++;
    message_ = os.str();

    socket_.async_send_to(
        boost::asio::buffer(message_), endpoint_,
        boost::bind(&sender::handle_send_to, this,
          boost::asio::placeholders::error));
  }

  void handle_send_to(const boost::system::error_code& error)
  {
    if(!error && message_count_ < max_message_count)
    {
      timer_.expires_from_now(boost::posix_time::seconds(1));
      timer_.async_wait(
          boost::bind(&sender::handle_timeout, this,
            boost::asio::placeholders::error));
    }
  }

  void handle_timeout(const boost::system::error_code& error)
  {
    if (!error)
    {
      std::ostringstream os;
      os << "Message " << message_count_++;
      message_ = os.str();

      spdlog::info("Sending Message: '{}'", message_);

      socket_.async_send_to(
          boost::asio::buffer(message_), endpoint_,
          boost::bind(&sender::handle_send_to, this,
            boost::asio::placeholders::error));
    }
  }

private:
  boost::asio::ip::udp::endpoint endpoint_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::deadline_timer timer_;
  int message_count_;
  std::string message_;
};

namespace po = boost::program_options;

int main(int argc, char **argv)
{
	po::options_description description("mdump - multicast filesharing for the commandline");

	const auto print_help = [&description](){ std::cout << description; };

	description.add_options()
		("help,h", "Display help message")
		("send,s", po::value<std::vector<std::string>>()->multitoken()->composing(),
       "Send stuff and have fun")
		("receive,r", po::value<std::vector<std::string>>()->multitoken()->composing(),
       "Receive stuff and have fun");

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(description).run(), vm);
	po::notify(vm);

	if(vm.count("help") || (!vm.count("send") && !vm.count("receive")))
	{
		print_help();
		return 0;
	}

  boost::asio::io_context io_service;
  try
  {

  if(!vm.count("send") && vm.count("receive"))
  {
    const auto vec = vm["receive"].as<std::vector<std::string>>();

    if(vec.size() != 2)
    {
      spdlog::error("receive mode need two arguments: listen address, multicast address");
      return -1;
    }

    spdlog::info("Starting Receiver");
    spdlog::info("Listen address {}, Multicast address {}", vec.at(0), vec.at(1));

    receiver r(io_service,
        boost::asio::ip::address::from_string(vec.at(0)),
        boost::asio::ip::address::from_string(vec.at(1)));
    io_service.run();
  }

  if(vm.count("send") && !vm.count("receive"))
  {
    const auto vec = vm["send"].as<std::vector<std::string>>();

    if(vec.size() != 1)
    {
      spdlog::error("send mode needs one argument: multicast address");
      return -1;
    }

    const auto multicast_address = vec.at(0);

    spdlog::info("Starting Sender");
    spdlog::info("Multicast address: {}", multicast_address);

    sender s(io_service, boost::asio::ip::address::from_string(multicast_address));
    io_service.run();
  }

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

