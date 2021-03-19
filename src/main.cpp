#include <iostream>
#include <memory>
#include <thread>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/file_sender.h"
#include "mfsync/file_fetcher.h"
#include "mfsync/file_receive_handler.h"

namespace po = boost::program_options;

int main(int argc, char **argv)
{
	po::options_description description("mdump - multicast filesharing for the commandline");

	const auto print_help = [&description](){ std::cout << description; };

	description.add_options()
		("help,h", "Display help message")
		("verbose,v", "Show debug logs")
		("send,s", po::value<std::vector<std::string>>()->multitoken()->composing(),
       "Send stuff and have fun")
		("receive,r", po::value<std::vector<std::string>>()->multitoken()->composing(),
			 "Receive stuff and have fun")
			("request,e", po::value<std::vector<std::string>>()->multitoken()->zero_tokens(), "try download the files with the given hash. if no hash is give all available files are downloaded")
		("storage,s", po::value<std::string>(), "Path to storage");

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(description).run(), vm);
	po::notify(vm);

	if(vm.count("help") || (!vm.count("send") && !vm.count("receive")))
	{
		print_help();
		return 0;
	}

  boost::asio::io_context io_service;
  std::unique_ptr<mfsync::multicast::file_fetcher> fetcher = nullptr;
  std::unique_ptr<mfsync::multicast::file_sender> sender = nullptr;
  std::unique_ptr<mfsync::file_receive_handler> receiver = nullptr;
  try
  {


  if(vm.count("verbose"))
  {
    spdlog::set_level(spdlog::level::debug);
  }
  else
  {
    spdlog::set_pattern("%v");
  }

  const short multicast_port = 30001;
  auto file_handler = mfsync::file_handler{};

  if(vm.count("receive"))
  {
    const auto vec = vm["receive"].as<std::vector<std::string>>();

    if(vec.size() != 2)
    {
      spdlog::error("receive mode need two arguments: listen address, multicast address");
      return -1;
    }

    spdlog::debug("Starting Receiver");
    spdlog::debug("Listen address {}, Multicast address {}", vec.at(0), vec.at(1));

    fetcher = std::make_unique<mfsync::multicast::file_fetcher>(io_service,
                                    boost::asio::ip::address::from_string(vec.at(0)),
                                    boost::asio::ip::address::from_string(vec.at(1)),
                                    multicast_port,
                                    &file_handler);
  }

  if(vm.count("storage"))
  {
    file_handler.init_storage(vm["storage"].as<std::string>());
  }

  if(vm.count("send"))
  {
    const auto vec = vm["send"].as<std::vector<std::string>>();

    if(vec.size() != 1)
    {
      spdlog::error("send mode needs one argument: multicast address");
      return -1;
    }

    const auto multicast_address = vec.at(0);

    spdlog::debug("Starting Sender");
    spdlog::debug("Multicast address: {}", multicast_address);

    sender = std::make_unique<mfsync::multicast::file_sender>(io_service,
                                     boost::asio::ip::address::from_string(multicast_address),
                                     multicast_port,
                                     &file_handler);
  }

  if(vm.count("request"))
  {
    auto file_hashes = vm["request"].as<std::vector<std::string>>();

    if(file_hashes.empty())
    {
      receiver = std::make_unique<mfsync::file_receive_handler>(io_service, &file_handler);
    }
    else
    {
      receiver = std::make_unique<mfsync::file_receive_handler>(io_service,
                                                                       &file_handler,
                                                                       std::move(file_hashes));
    }

    receiver->get_files();
  }

  io_service.run();

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
