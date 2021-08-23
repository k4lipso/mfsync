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
          result.emplace_back(
                boost::asio::ip::make_address(
                  inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr)));
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

std::string_view get_help_message(operation_mode mode)
{
  constexpr std::string_view help_general{R"""(mfsync - multicast filesharing from the commandline

Usage: mfsync [share|fetch|get|sync] multicast-group [options]
  Use "mfsync [share|fetch|get|sync] --help" for mode specific help message

Options)"""};

  constexpr std::string_view help_share{R"""(mfsync share - share files within the multicast group

Usage: mfsync share multicast-group [options] path-to-folder

Description:
  'mfsync share' basically enables every other computer in the network to download files
  from the sharing host. The sharing host announces all given files to the multicast group.
  Those files can then be downloaded from the host using mfsync on another machine.

Examples:
  "mfsync share 239.255.0.1 ./destination"
    - shares all available files but does not download new ones

Options)"""};

  constexpr std::string_view help_fetch{R"""(mfsync fetch - fetch available files within the multicast group

Usage: mfsync fetch multicast-group [options]

Description:
  'mfsync fetch' "passively" listens to all announced files in the multicast group
  and prints them to stdout. This mode is used to get a list of all files that are currently
  made available within the multicast group.

Examples:
  "mfsync fetch 239.255.0.1"
    - requests names of all available files in the network and prints them to stdout
    - this runs forever

  "mfsync fetch 239.255.0.1 --wait-until 3
    - requests names of all available files in the network and prints them to stdout
    - execution is stopped after 3 seconds

Options)"""};

  constexpr std::string_view help_get{R"""(mfsync get - get files that are available within the multicast group

Usage: mfsync get multicast-group [options] path-to-folder

Description:
  'mfsync get' retreives all specified files.
  The files are specified by their sha256sum which can be seen when using 'mfsync fetch'.
  If no files are specified all announced files will be retreived.

Examples:
  "mfsync get 239.255.0.1 ./destination"
    - download all available files
    - runs forever if no --wait-until is given

  "mfsync get 239.255.0.1 --request sha256sum1 sha256sum2 -- ./destination"
    - downloads files with the given sha256sums to given destination if available
    - waits till all given files are downloaded
    - can abort earlier if --wait-until is specified

Options)"""};

  constexpr std::string_view help_sync{R"""(mfsync sync - share all files and download all files

Usage: mfsync sync multicast-group [options] path-to-folder

Description:
  'mfsync sync' is basically a combination of 'mfsync share' and 'mfsync get'.
  It announces all given files and also retreives all available files that are not stored locally already.
  Files that where retrieved are then also shared again.

Examples:
  "mfsync sync 239.255.0.1 ./destination"
    - share and get all available files

Options)"""};

  switch(mode)
  {
    case operation_mode::SHARE:
      return help_share;
    case operation_mode::FETCH:
      return help_fetch;
    case operation_mode::GET:
      return help_get;
    case operation_mode::SYNC:
      return help_sync;
    case operation_mode::NONE:
  default:
    return help_general;
  }
}


int main(int argc, char **argv)
{
  po::options_description description("");

  std::string_view usage_message = get_help_message(operation_mode::NONE);
  const auto print_help = [&description, &usage_message]()
  {
    std::cout << usage_message;
    std::cout << description;
  };

  description.add_options()
    ("help,h", "Display help message")
    ("verbose,v", "Show debug logs")
    ("trace,t", "Show sent and received multicast messages")
    ("request,r", po::value<std::vector<std::string>>()->multitoken()->zero_tokens(),
       "try download the files with the given hash. if no hash is give all available files are downloaded")
    ("port,p", po::value<unsigned short>(), "Manual specify tcp port to listen on. If not specified using default port 8000")
    ("multicast-port,m", po::value<unsigned short>(), "Manual specify multicast port. If not specified using default port 30001")
    ("multicast-listen-address,l", po::value<std::string>(), "Manual specify multicast listen address. If not specified using 0.0.0.0")
    ("wait-until,w", po::value<int>(), "stop program execution after the given amount of seconds.")
    ("outbound-addresses,a", po::value<std::vector<std::string>>()->multitoken(), "Manual specify multicast outbound interface addresses.")
    ("outbound-interfaces,i", po::value<std::vector<std::string>>()->multitoken(),
                              "Manual specify multicast outbound interface names. Multicast messages will be sent to the given interfaces");

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
    if(vm.count("mode"))
    {
      usage_message = get_help_message(get_mode(vm["mode"].as<std::string>()));
    }
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
  boost::asio::ip::address multicast_listen_address =
      boost::asio::ip::make_address(mfsync::protocol::MULTICAST_LISTEN_ADDRESS);
  std::vector<boost::asio::ip::address> outbound_addresses { boost::asio::ip::address{} };

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
    multicast_listen_address =
        boost::asio::ip::make_address(vm["multicast-listen-address"].as<std::string>(), ec);

    if(ec)
    {
      spdlog::error("the given multicast listen address is not a valid ip address. aborting.");
      return -1;
    }
  }

  if(vm.count("outbound-addresses") && vm.count("outbound-interfaces"))
  {
    spdlog::info("Only one of \"outbound-addresses\" and \"outbound-interfaces\" can be specified simultaniously");
    return -1;
  }

  if(vm.count("outbound-addresses"))
  {
    const auto address_vec = vm["outbound-addresses"].as<std::vector<std::string>>();

    if(address_vec.empty())
    {
      spdlog::error("--outbound-addresses was specified but no addresses where given. aborting.");
      return -1;
    }

    outbound_addresses.clear();

    for(const auto& address : address_vec)
    {
      outbound_addresses.push_back(boost::asio::ip::make_address(address, ec));

      if(ec)
      {
        spdlog::error("the given outbound address ({}) is not a valid ip address. aborting.", address);
        return -1;
      }
    }
  }

  if(vm.count("outbound-interfaces"))
  {
    const auto address_vec = vm["outbound-interfaces"].as<std::vector<std::string>>();

    if(address_vec.empty())
    {
      spdlog::error("--outbound-interfaces was specified but no interface was given. aborting.");
      return -1;
    }

    outbound_addresses.clear();
    outbound_addresses = get_ip_addresses_by_interface_name(address_vec);

    if(outbound_addresses.size() != address_vec.size())
    {
      spdlog::info("Couldnt get addresses for all given outbound-interfaces.");
      spdlog::info("Multicast messages may not be sent to all interfaces.");
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
  std::vector<std::unique_ptr<mfsync::multicast::file_sender>> sender_vec;
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
    for(const auto& outbound_address : outbound_addresses)
    {
      auto sender = std::make_unique<mfsync::multicast::file_sender>(io_service,
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
      sender_vec.push_back(std::move(sender));
    }

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

  std::vector<std::thread> workers;
  for(unsigned i = 0; i < std::thread::hardware_concurrency(); ++i)
  {
    workers.emplace_back([&io_service](){ io_service.run(); });
  }

  auto timeout = std::chrono::system_clock::now();
  if(vm.count("wait-until"))
  {
    timeout += std::chrono::seconds(vm["wait-until"].as<int>());
  }
  else
  {
    timeout += std::chrono::hours(std::numeric_limits<int>::max());
  }

  if(receiver != nullptr)
  {
    auto future = receiver->get_future();
    future.wait_until(timeout);
  }
  else
  {
    std::this_thread::sleep_until(timeout);
  }

  io_service.stop();

  for(auto& worker : workers)
  {
    worker.join();
  }

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
