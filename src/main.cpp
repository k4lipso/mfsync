#include <iostream>

#include <boost/program_options.hpp>
#include "spdlog/spdlog.h"

namespace po = boost::program_options;

int main(int argc, char **argv)
{
	po::options_description description("mdump - multicast filesharing for the commandline");

	const auto print_help = [&description](){ std::cout << description; };

	description.add_options()
		("help,h", "Display help message");

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(description).run(), vm);
	po::notify(vm);

	if(vm.count("help"))
	{
		print_help();
		return 0;
	}

	return 0;
}

