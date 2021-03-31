#include <iostream>
#include <memory>
#include <thread>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>

#include "spdlog/spdlog.h"

#include <ifaddrs.h>
#include <sys/types.h>

#include "mfsync/file_handler.h"
#include "mfsync/file_sender.h"
#include "mfsync/file_fetcher.h"
#include "mfsync/file_receive_handler.h"
#include "mfsync/protocol.h"
#include "mfsync/server.h"

namespace po = boost::program_options;

enum class operation_mode
{
  SYNC,
  SHARE,
  FETCH,
  GET,
  NONE
};

inline std::vector<boost::asio::ip::address>
get_ip_addresses_by_interface_name(const std::vector<std::string>& interface_names)
{
  std::vector<boost::asio::ip::address> result;
  struct ifaddrs *interfaces = nullptr;
  struct ifaddrs *temp_addr = nullptr;

  if (getifaddrs(&interfaces) == 0) {
    temp_addr = interfaces;

    while(temp_addr != nullptr) {
      if(temp_addr->ifa_addr->sa_family == AF_INET) {

        if(std::any_of(interface_names.begin(), interface_names.end(),
                       [&](const auto name){ return name.compare(temp_addr->ifa_name) == 0; }))
        {
          result.emplace_back(boost::asio::ip::make_address(inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr)));
        }
      }
      temp_addr = temp_addr->ifa_next;
    }
  }

  freeifaddrs(interfaces); //freedom is not given, it is taken
  return result;
}

inline operation_mode get_mode(const std::string& input)
{
  const std::map<std::string, operation_mode> mode_map
  {
    { "sync", operation_mode::SYNC },
    { "share", operation_mode::SHARE },
    { "fetch", operation_mode::FETCH },
    { "get", operation_mode::GET },
  };

  if(!mode_map.contains(input))
  {
    return operation_mode::NONE;
  }

  return mode_map.at(input);
}


int main(int argc, char **argv)
{
  po::options_description description("mfsync - multicast filesharing for the commandline");

  const auto print_help = [&description](){ std::cout << description; };

  description.add_options()
    ("help,h", "Display help message")
    ("verbose,v", "Show debug logs")
    ("trace,t", "Show sent and received multicast messages")
    ("request,r", po::value<std::vector<std::string>>()->multitoken()->zero_tokens(),
       "try download the files with the given hash. if no hash is give all available files are downloaded")
    ("port,p", po::value<unsigned short>(), "Manual specify tcp port to listen on. If not specified using default port 8000")
    ("multicast-port,m", po::value<unsigned short>(), "Manual specify multicast port. If not specified using default port 30001")
    ("multicast-listen-address,l", po::value<std::string>(), "Manual specify multicast listen address. If not specified using 0.0.0.0")
    ("outbound-address,o", po::value<std::string>(), "Manual specify multicast outbound interface address.");

  po::options_description hidden;
  hidden.add_options()
      ("mode", po::value<std::string>(), "operation mode")
      ("multicast-address", po::value<std::string>(), "multicast address to listen on")
      ("destination", po::value<std::string>(), "path to destination")
      ;

  po::options_description all_options;
  all_options.add(description);
  all_options.add(hidden);

  po::positional_options_description p;
  p.add("mode", 1);
  p.add("multicast-address", 1);
  p.add("destination", 1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).
          options(all_options).
          positional(p).
          run(),
          vm);

  po::notify(vm);

  if(vm.count("help"))
  {
    print_help();
    return 0;
  }

  if(!vm.count("mode") || !vm.count("multicast-address"))
  {
    print_help();
    return 0;
  }

  const auto mode = get_mode(vm["mode"].as<std::string>());
  //todo: check if multicast addr is valid

  if(vm.count("trace"))
  {
    spdlog::set_level(spdlog::level::trace);
  }
  else if(vm.count("verbose"))
  {
    spdlog::set_level(spdlog::level::debug);
  }
  else
  {
    spdlog::set_pattern("%v");
  }

  if(mode == operation_mode::NONE)
  {
    spdlog::info("The given operation mode is not known. Valid values are: sync, share, fetch, get");
    spdlog::info("Use --help to print all options");
    return 0;
  }

  if(mode != operation_mode::FETCH && !vm.count("destination"))
  {
    spdlog::info("No destination was given. The only mode that needs no destination is 'fetch'");
    spdlog::info("Use --help to print all options");
    return 0;
  }

  unsigned short port = mfsync::protocol::TCP_PORT;
  unsigned short multicast_port = mfsync::protocol::MULTICAST_PORT;
  boost::asio::ip::address multicast_listen_address = boost::asio::ip::make_address(mfsync::protocol::MULTICAST_LISTEN_ADDRESS);
  boost::asio::ip::address outbound_address;

  if(vm.count("port"))
  {
    port = vm["port"].as<unsigned short>();
  }

  if(vm.count("multicast-port"))
  {
    port = vm["multicast-port"].as<unsigned short>();
  }

  boost::system::error_code ec;
  if(vm.count("multicast-listen-address"))
  {
    multicast_listen_address = boost::asio::ip::make_address(vm["multicast-listen-address"].as<std::string>(), ec);

    if(ec)
    {
      spdlog::error("the given multicast listen address is not a valid ip address. aborting.");
      return -1;
    }
  }

  if(vm.count("outbound-address"))
  {
    outbound_address = boost::asio::ip::make_address(vm["outbound-address"].as<std::string>(), ec);

    if(ec)
    {
      spdlog::error("the given multicast sender address is not a valid ip address. aborting.");
      return -1;
    }
  }

  const auto multicast_address = boost::asio::ip::make_address(vm["multicast-address"].as<std::string>(), ec);

  if(ec || !multicast_address.is_multicast())
  {
    spdlog::error("the given multicast address is not a valid multicast address. aborting.");
    return -1;
  }

  std::string destination_path;
  if(vm.count("destination"))
  {
    destination_path = vm["destination"].as<std::string>();
  }


  boost::asio::io_context io_service;
  std::unique_ptr<mfsync::multicast::file_fetcher> fetcher = nullptr;
  std::unique_ptr<mfsync::multicast::file_sender> sender = nullptr;
  std::unique_ptr<mfsync::file_receive_handler> receiver = nullptr;
  std::unique_ptr<mfsync::filetransfer::server> file_server = nullptr;

  try
  {

  auto file_handler = mfsync::file_handler{};

  if(mode != operation_mode::FETCH)
  {
    spdlog::info("start initializing storage. depending on filesizes this may take a while");
    file_handler.init_storage(destination_path);
    spdlog::info("done initializing storage");
  }

  if(mode != operation_mode::FETCH && mode != operation_mode::GET)
  {
    sender = std::make_unique<mfsync::multicast::file_sender>(io_service,
                                                              multicast_address,
                                                              multicast_port,
                                                              port,
                                                              file_handler);
    if(!outbound_address.is_unspecified())
    {
      if(outbound_address.is_v4())
      {
        spdlog::debug("setting multicast outbound interface address to {}", outbound_address.to_string());
        sender->set_outbound_interface(outbound_address.to_v4());
      }
      else
      {
        spdlog::info("setting multicast outbound interface address to non v4 address has no effect");
      }
    }

    sender->init();

    file_server = std::make_unique<mfsync::filetransfer::server>(io_service,
                                                                 port,
                                                                 file_handler);
    file_server->run();
  }

  if(mode != operation_mode::SHARE)
  {
    fetcher = std::make_unique<mfsync::multicast::file_fetcher>(io_service,
                                    multicast_listen_address,
                                    multicast_address,
                                    multicast_port,
                                    &file_handler);
  }

  if(mode != operation_mode::SHARE && mode != operation_mode::FETCH)
  {
    std::vector<std::string> file_hashes{};

    if(vm.count("request"))
    {
      file_hashes = vm["request"].as<std::vector<std::string>>();
    }

    receiver = std::make_unique<mfsync::file_receive_handler>(io_service, file_handler);

    if(!file_hashes.empty())
    {
      receiver->set_files(std::move(file_hashes));
    }

    receiver->get_files();
  }

  if(mode == operation_mode::FETCH)
  {
    file_handler.print_availables(true);
  }

  io_service.run();

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
