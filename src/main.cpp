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
#include "mfsync/misc.h"
#include "mfsync/help_messages.h"

#include "indicators/dynamic_progress.hpp"
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
//using namespace indicators;
//
//int main() {
//
//  ProgressBar bar1{option::BarWidth{50}, option::ForegroundColor{Color::red},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"5c90d4a2d1a8: Downloading "}};
//
//  ProgressBar bar2{option::BarWidth{50}, option::ForegroundColor{Color::yellow},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"22337bfd13a9: Downloading "}};
//
//  ProgressBar bar3{option::BarWidth{50}, option::ForegroundColor{Color::green},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"10f26c680a34: Downloading "}};
//
//  ProgressBar bar4{option::BarWidth{50}, option::ForegroundColor{Color::white},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"6364e0d7a283: Downloading "}};
//
//  ProgressBar bar5{option::BarWidth{50}, option::ForegroundColor{Color::blue},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"ff1356ba118b: Downloading "}};
//
//  ProgressBar bar6{option::BarWidth{50}, option::ForegroundColor{Color::cyan},
//                   option::ShowElapsedTime{true}, option::ShowRemainingTime{true},
//                   option::PrefixText{"5a17453338b4: Downloading "}};
//
//  std::cout << termcolor::bold << termcolor::white << "Pulling image foo:bar/baz\n";
//
//  // Construct with 3 progress bars. We'll add 3 more at a later point
//  DynamicProgress<ProgressBar> bars(bar1, bar2, bar3);
//
//  // Do not hide bars when completed
//  bars.set_option(option::HideBarWhenComplete{false});
//
//  std::thread fourth_job, fifth_job, sixth_job;
//
//  auto job4 = [&bars](size_t i) {
//    while (true) {
//      bars[i].tick();
//      if (bars[i].is_completed()) {
//        bars[i].set_option(option::PrefixText{"6364e0d7a283: Pull complete "});
//        bars[i].mark_as_completed();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(50));
//    }
//  };
//
//  auto job5 = [&bars](size_t i) {
//    while (true) {
//      bars[i].tick();
//      if (bars[i].is_completed()) {
//        bars[i].set_option(option::PrefixText{"ff1356ba118b: Pull complete "});
//        bars[i].mark_as_completed();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(100));
//    }
//  };
//
//  auto job6 = [&bars](size_t i) {
//    while (true) {
//      bars[i].tick();
//      if (bars[i].is_completed()) {
//        bars[i].set_option(option::PrefixText{"5a17453338b4: Pull complete "});
//        bars[i].mark_as_completed();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(40));
//    }
//  };
//
//  auto job1 = [&bars, &bar6, &sixth_job, &job6]() {
//    while (true) {
//      bars[0].tick();
//      if (bars[0].is_completed()) {
//        bars[0].set_option(option::PrefixText{"5c90d4a2d1a8: Pull complete "});
//        // bar1 is completed, adding bar6
//        auto i = bars.push_back(bar6);
//        sixth_job = std::thread(job6, i);
//        sixth_job.join();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(140));
//    }
//  };
//
//  auto job2 = [&bars, &bar5, &fifth_job, &job5]() {
//    while (true) {
//      bars[1].tick();
//      if (bars[1].is_completed()) {
//        bars[1].set_option(option::PrefixText{"22337bfd13a9: Pull complete "});
//        // bar2 is completed, adding bar5
//        auto i = bars.push_back(bar5);
//        fifth_job = std::thread(job5, i);
//        fifth_job.join();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(25));
//    }
//  };
//
//  auto job3 = [&bars, &bar4, &fourth_job, &job4]() {
//    while (true) {
//      bars[2].tick();
//      if (bars[2].is_completed()) {
//        bars[2].set_option(option::PrefixText{"10f26c680a34: Pull complete "});
//        // bar3 is completed, adding bar4
//        auto i = bars.push_back(bar4);
//        fourth_job = std::thread(job4, i);
//        fourth_job.join();
//        break;
//      }
//      std::this_thread::sleep_for(std::chrono::milliseconds(50));
//    }
//  };
//
//  std::thread first_job(job1);
//  std::thread second_job(job2);
//  std::thread third_job(job3);
//
//  third_job.join();
//  second_job.join();
//  first_job.join();
//
//  std::cout << termcolor::bold << termcolor::green << "✔ Downloaded image foo/bar:baz" << std::endl;
//  std::cout << termcolor::reset;
//
//  return 0;
//}
namespace po = boost::program_options;

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
    ("multicast-address", po::value<std::string>(), "Manual specify multicast address. If not specified 239.255.0.1 is used as default")
    ("multicast-port,m", po::value<unsigned short>(), "Manual specify multicast port. If not specified using default port 30001")
    ("multicast-listen-address,l", po::value<std::string>(), "Manual specify multicast listen address. If not specified using 0.0.0.0")
    ("server-tls,e", po::value<std::vector<std::string>>()->multitoken(),
       "paths to two files. first containing certificate and private key of the server. second containing dh parameters")
    ("client-tls,e", po::value<std::string>(), "paths to file containing all trusted certificates")
    ("wait-until,w", po::value<int>(), "stop program execution after the given amount of seconds.")
    ("outbound-addresses,a", po::value<std::vector<std::string>>()->multitoken(), "Manual specify multicast outbound interface addresses.")
    ("outbound-interfaces,i", po::value<std::vector<std::string>>()->multitoken(),
                              "Manual specify multicast outbound interface names. Multicast messages will be sent to the given interfaces");

  po::options_description hidden;
  hidden.add_options()
      ("mode", po::value<std::string>(), "operation mode")
      ("destination", po::value<std::string>(), "path to destination")
      ;

  po::options_description all_options;
  all_options.add(description);
  all_options.add(hidden);

  po::positional_options_description p;
  p.add("mode", 1);
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
      usage_message = get_help_message(misc::get_mode(vm["mode"].as<std::string>()));
    }
    print_help();
    return 0;
  }

  if(!vm.count("mode"))
  {
    print_help();
    return 0;
  }


  const auto mode = misc::get_mode(vm["mode"].as<std::string>());

  std::unique_ptr<mfsync::filetransfer::progress_handler> progress_handler
      = std::make_unique<mfsync::filetransfer::progress_handler>();

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
    progress_handler->start();
    spdlog::set_pattern("[mfsync] %v");
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
  auto multicast_listen_address =
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

  auto multicast_address = boost::asio::ip::make_address(mfsync::protocol::MULTICAST_ADDRESS);

  if(vm.count("multicast-address"))
  {
    multicast_address = boost::asio::ip::make_address(vm["multicast-address"].as<std::string>(), ec);
  }

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

  std::string client_tls_path;
  std::optional<std::pair<std::string, std::string>> server_tls_paths;

  if(vm.count("client-tls"))
  {
    client_tls_path = vm["client-tls"].as<std::string>();
  }

  if(vm.count("server-tls"))
  {
    const auto& file_paths = vm["server-tls"].as<std::vector<std::string>>();

    if(file_paths.size() != 2)
    {
      spdlog::info("wrong amount of server-tls files specified. exactly two files need to be specified.");
    }

    server_tls_paths = std::make_pair(file_paths.at(0), file_paths.at(1));
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

    if(server_tls_paths.has_value())
    {
      const auto& paths = server_tls_paths.value();
      file_server->enable_tls(std::get<1>(paths), std::get<0>(paths));
    }

    file_server->set_progress(progress_handler.get());
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

    receiver = std::make_unique<mfsync::file_receive_handler>(io_service, file_handler, progress_handler.get());

    if(!file_hashes.empty())
    {
      receiver->set_files(std::move(file_hashes));
    }

    if(!client_tls_path.empty())
    {
      receiver->enable_tls(client_tls_path);
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
