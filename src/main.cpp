#include <ifaddrs.h>
#include <sys/types.h>

#include <boost/bind.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "mfsync/crypto.h"
#include "mfsync/file_fetcher.h"
#include "mfsync/file_handler.h"
#include "mfsync/file_receive_handler.h"
#include "mfsync/file_sender.h"
#include "mfsync/help_messages.h"
#include "mfsync/misc.h"
#include "mfsync/protocol.h"
#include "mfsync/server.h"
#include "spdlog/spdlog.h"

namespace po = boost::program_options;

inline std::vector<boost::asio::ip::address> get_ip_addresses_by_interface_name(
    const std::vector<std::string>& interface_names) {
  std::vector<boost::asio::ip::address> result;
  struct ifaddrs* interfaces = nullptr;
  struct ifaddrs* temp_addr = nullptr;

  if (getifaddrs(&interfaces) == 0) {
    temp_addr = interfaces;

    while (temp_addr != nullptr) {
      if (temp_addr->ifa_addr->sa_family == AF_INET) {
        if (std::any_of(interface_names.begin(), interface_names.end(),
                        [&](const auto name) {
                          return name.compare(temp_addr->ifa_name) == 0;
                        })) {
          result.emplace_back(boost::asio::ip::make_address(
              inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr)));
        }
      }
      temp_addr = temp_addr->ifa_next;
    }
  }

  freeifaddrs(interfaces);  // freedom is not given, it is taken
  return result;
}

int main(int argc, char** argv) {
  po::options_description description("");

  std::string_view usage_message = get_help_message(operation_mode::NONE);
  const auto print_help = [&description, &usage_message]() {
    std::cout << usage_message;
    std::cout << description;
  };

  description.add_options()("help,h", "Display help message")(
      "verbose,v", "Show debug logs")("version", "print version")(
      "list-hosts", "print available hosts and their keys")(
      "public-key", "print public key")(
      "trace,t", "Show sent and received multicast messages")(
      "port,p", po::value<unsigned short>(),
      "Manual specify tcp port to listen on. If not specified using default "
      "port 8000")("concurrent_downloads,c", po::value<size_t>(),
                   "maximum concurrent downloads allowed. default is 3")(
      "key-file", po::value<std::string>(),
      "Manual specify key to use. key.bin is default")(
      "multicast-address", po::value<std::string>(),
      "Manual specify multicast address. If not specified 239.255.0.1 is "
      "used as default")("multicast-port,m", po::value<unsigned short>(),
                         "Manual specify multicast port. If not specified "
                         "using default port 30001")(
      "multicast-listen-address,l", po::value<std::string>(),
      "Manual specify multicast listen address. If not specified using "
      "0.0.0.0")(
      "server-tls,e", po::value<std::vector<std::string>>()->multitoken(),
      "paths to two files. first containing certificate and private key of "
      "the server. second containing dh parameters")(
      "client-tls,e", po::value<std::string>(),
      "paths to file containing all trusted certificates")(
      "wait-until,w", po::value<int>(),
      "stop program execution after the given amount of seconds.")(
      "trusted-keys", po::value<std::vector<std::string>>()->multitoken(),
      "Manual specify trusted keys")(
      "outbound-addresses,a",
      po::value<std::vector<std::string>>()->multitoken(),
      "Manual specify multicast outbound interface addresses.")(
      "outbound-interfaces,i",
      po::value<std::vector<std::string>>()->multitoken(),
      "Manual specify multicast outbound interface names. Multicast messages "
      "will be sent to the given interfaces");

  po::options_description hidden;
  hidden.add_options()("mode", po::value<std::string>(), "operation mode")(
      "destination", po::value<std::vector<std::string>>()->multitoken(),
      "path to destination");

  po::options_description all_options;
  all_options.add(description);
  all_options.add(hidden);

  po::positional_options_description p;
  p.add("mode", 1);
  p.add("destination", -1);

  try {
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
                  .options(all_options)
                  .positional(p)
                  .run(),
              vm);

    po::notify(vm);

    if (vm.count("version")) {
      std::cout << "mfsync v" << mfsync::protocol::VERSION << '\n';
      return 0;
    }

    if (vm.count("help")) {
      if (vm.count("mode")) {
        usage_message =
            get_help_message(misc::get_mode(vm["mode"].as<std::string>()));
      }
      print_help();
      return 0;
    }

    std::string key_file{"key.bin"};

    if (vm.count("key-file")) {
      key_file = vm["key-file"].as<std::string>();
    }

    auto crypto_handler = std::make_unique<mfsync::crypto::crypto_handler>();
    crypto_handler->init(key_file);
    std::string public_key = crypto_handler->get_public_key();

    if (public_key.empty()) {
      spdlog::error("Could not create public key, aborting...");
      return -1;
    }

    if (vm.count("public-key")) {
      spdlog::set_pattern(std::string{mfsync::protocol::MFSYNC_LOG_PREFIX} +
                          "%v");
      spdlog::info("{}", public_key);
      return 0;
    }


    spdlog::debug("{}", public_key);

    if (vm.count("trusted-keys")) {
      const auto& trusted = vm["trusted-keys"].as<std::vector<std::string>>();
      for (const auto& key : trusted) {
        crypto_handler->add_allowed_key(key);
      }
    }

    bool list_hosts = vm.count("list-hosts");

    const auto mode = list_hosts ? operation_mode::FETCH : misc::get_mode(vm["mode"].as<std::string>());

    std::unique_ptr<mfsync::filetransfer::progress_handler> progress_handler =
        std::make_unique<mfsync::filetransfer::progress_handler>();

    if (vm.count("trace")) {
      spdlog::set_level(spdlog::level::trace);
    } else if (vm.count("verbose")) {
      spdlog::set_level(spdlog::level::debug);
    } else {
      progress_handler->start();
      spdlog::set_pattern(std::string{mfsync::protocol::MFSYNC_LOG_PREFIX} +
                          "%v");
    }

    if (mode == operation_mode::NONE) {
      spdlog::info(
          "The given operation mode is not known. Valid values are: "
          "sync, share, fetch, get");
      spdlog::info("Use --help to print all options");
      return 0;
    }

    if (mode != operation_mode::FETCH && !vm.count("destination")) {
      spdlog::info(
          "No destination was given. The only mode that needs no "
          "destination is 'fetch'");
      spdlog::info("Use --help to print all options");
      return 0;
    }

    unsigned short port = mfsync::protocol::TCP_PORT;
    unsigned short multicast_port = mfsync::protocol::MULTICAST_PORT;
    auto multicast_listen_address = boost::asio::ip::make_address(
        mfsync::protocol::MULTICAST_LISTEN_ADDRESS);

    std::vector<boost::asio::ip::address> outbound_addresses{
        boost::asio::ip::address{}};

    if (vm.count("port")) {
      port = vm["port"].as<unsigned short>();
    }

    if (vm.count("multicast-port")) {
      port = vm["multicast-port"].as<unsigned short>();
    }

    boost::system::error_code ec;
    if (vm.count("multicast-listen-address")) {
      multicast_listen_address = boost::asio::ip::make_address(
          vm["multicast-listen-address"].as<std::string>(), ec);

      if (ec) {
        spdlog::error(
            "the given multicast listen address is not a valid ip "
            "address. aborting.");
        return -1;
      }
    }

    if (vm.count("outbound-addresses") && vm.count("outbound-interfaces")) {
      spdlog::info(
          "Only one of \"outbound-addresses\" and "
          "\"outbound-interfaces\" can be specified simultaniously");
      return -1;
    }

    if (vm.count("outbound-addresses")) {
      const auto address_vec =
          vm["outbound-addresses"].as<std::vector<std::string>>();

      if (address_vec.empty()) {
        spdlog::error(
            "--outbound-addresses was specified but no addresses where "
            "given. aborting.");
        return -1;
      }

      outbound_addresses.clear();

      for (const auto& address : address_vec) {
        outbound_addresses.push_back(
            boost::asio::ip::make_address(address, ec));

        if (ec) {
          spdlog::error(
              "the given outbound address ({}) is not a valid ip "
              "address. aborting.",
              address);
          return -1;
        }
      }
    }

    if (vm.count("outbound-interfaces")) {
      const auto address_vec =
          vm["outbound-interfaces"].as<std::vector<std::string>>();

      if (address_vec.empty()) {
        spdlog::error(
            "--outbound-interfaces was specified but no interface was "
            "given. aborting.");
        return -1;
      }

      outbound_addresses.clear();
      outbound_addresses = get_ip_addresses_by_interface_name(address_vec);

      if (outbound_addresses.size() != address_vec.size()) {
        spdlog::info(
            "Couldnt get addresses for all given outbound-interfaces.");
        spdlog::info("Multicast messages may not be sent to all interfaces.");
      }
    }

    auto multicast_address =
        boost::asio::ip::make_address(mfsync::protocol::MULTICAST_ADDRESS);

    if (vm.count("multicast-address")) {
      multicast_address = boost::asio::ip::make_address(
          vm["multicast-address"].as<std::string>(), ec);
    }

    if (ec || !multicast_address.is_multicast()) {
      spdlog::error(
          "the given multicast address is not a valid multicast address. "
          "aborting.");
      return -1;
    }

    std::string destination_path;
    std::vector<std::string> target_files{};
    if (vm.count("destination")) {
      target_files = vm["destination"].as<std::vector<std::string>>();
      destination_path = target_files.back();
      target_files.pop_back();
    }

    std::string client_tls_path;
    std::optional<std::pair<std::string, std::string>> server_tls_paths;

    if (vm.count("client-tls")) {
      client_tls_path = vm["client-tls"].as<std::string>();
    }

    if (vm.count("server-tls")) {
      const auto& file_paths = vm["server-tls"].as<std::vector<std::string>>();

      if (file_paths.size() != 2) {
        spdlog::info(
            "wrong amount of server-tls files specified. exactly two "
            "files need to be specified.");
      }

      server_tls_paths = std::make_pair(file_paths.at(0), file_paths.at(1));
    }

    boost::asio::io_context io_service;
    std::unique_ptr<mfsync::multicast::file_fetcher> fetcher = nullptr;
    std::vector<std::unique_ptr<mfsync::multicast::file_sender>> sender_vec;
    std::unique_ptr<mfsync::file_receive_handler> receiver = nullptr;
    std::unique_ptr<mfsync::filetransfer::server> file_server = nullptr;

    auto file_handler = mfsync::file_handler{};
    file_handler.set_progress(progress_handler.get());

    std::thread storage_initialization_thread;

    if (mode != operation_mode::FETCH) {
      storage_initialization_thread =
          std::thread{[&file_handler, &destination_path]() {
            file_handler.init_storage(destination_path);
          }};
    }

    if (mode != operation_mode::FETCH && mode != operation_mode::GET) {
      for (const auto& outbound_address : outbound_addresses) {
        auto sender = std::make_unique<mfsync::multicast::file_sender>(
            io_service, multicast_address, multicast_port, port, file_handler,
            public_key);

        if (!outbound_address.is_unspecified()) {
          if (outbound_address.is_v4()) {
            spdlog::debug(
                "setting multicast outbound interface address to "
                "{}",
                outbound_address.to_string());
            sender->set_outbound_interface(outbound_address.to_v4());
          } else {
            spdlog::info(
                "setting multicast outbound interface address to "
                "non v4 address has no effect");
          }
        }

        sender->init();
        sender_vec.push_back(std::move(sender));
      }

      file_server = std::make_unique<mfsync::filetransfer::server>(
          io_service, port, file_handler, *crypto_handler.get());

      if (server_tls_paths.has_value()) {
        const auto& paths = server_tls_paths.value();
        file_server->enable_tls(std::get<1>(paths), std::get<0>(paths));
      }

      file_server->set_progress(progress_handler.get());
      file_server->run();
    }

    if (mode != operation_mode::SHARE) {
      fetcher = std::make_unique<mfsync::multicast::file_fetcher>(
          io_service, multicast_listen_address, multicast_address,
          multicast_port, &file_handler, *crypto_handler.get());
    }

    if (mode != operation_mode::SHARE && mode != operation_mode::FETCH) {
      size_t concurrent_downloads = 3;
      if (vm.count("concurrent_downloads")) {
        concurrent_downloads = vm["concurrent_downloads"].as<size_t>();
      }

      receiver = std::make_unique<mfsync::file_receive_handler>(
          io_service, file_handler, concurrent_downloads, *crypto_handler.get(),
          progress_handler.get());

      if (!target_files.empty()) {
        receiver->set_files(std::move(target_files));
      }

      if (!client_tls_path.empty()) {
        receiver->enable_tls(client_tls_path);
      }

      receiver->get_files();
    }

    if (mode == operation_mode::FETCH) {
      if(list_hosts)
      {
        fetcher->list_hosts(true);
      }
      else
      {
        file_handler.print_availables(true);
      }
    }

    std::vector<std::thread> workers;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
      workers.emplace_back([&io_service]() { io_service.run(); });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto timeout = std::chrono::system_clock::now();
    if (vm.count("wait-until")) {
      timeout += std::chrono::seconds(vm["wait-until"].as<int>());
    } else {
      timeout += std::chrono::hours(std::numeric_limits<int>::max());
    }

    if (receiver != nullptr) {
      auto future = receiver->get_future();
      future.wait_until(timeout);
    } else {
      std::this_thread::sleep_until(timeout);
    }

    if (storage_initialization_thread.joinable()) {
      storage_initialization_thread.join();
    }

    progress_handler->stop();

    io_service.stop();

    for (auto& worker : workers) {
      worker.join();
    }

    spdlog::debug("stopped...");

  } catch (std::exception& e) {
    std::cout << e.what() << "\n";
  }

  return 0;
}
