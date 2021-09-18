#include <string_view>

#include "mfsync/misc.h"

std::string_view get_help_message(operation_mode mode)
{
  constexpr std::array<std::string_view, 5> messages{

//SYNC MESSAGE
R"""(mfsync sync - share all files and download all files

Usage: mfsync sync multicast-group [options] path-to-folder

Description:
  'mfsync sync' is basically a combination of 'mfsync share' and 'mfsync get'.
  It announces all given files and also retreives all available files that are not stored locally already.
  Files that where retrieved are then also shared again.

Examples:
  "mfsync sync 239.255.0.1 ./destination"
    - share and get all available files

Options)""",

//SHARE MESSAGE
R"""(mfsync share - share files within the multicast group

Usage: mfsync share multicast-group [options] path-to-folder

Description:
  'mfsync share' basically enables every other computer in the network to download files
  from the sharing host. The sharing host announces all given files to the multicast group.
  Those files can then be downloaded from the host using mfsync on another machine.

Examples:
  "mfsync share 239.255.0.1 ./destination"
    - shares all available files but does not download new ones

Options)""",

//FETCH MESSAGE
R"""(mfsync fetch - fetch available files within the multicast group

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

Options)""",

//GET MESSAGE
R"""(mfsync get - get files that are available within the multicast group

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

Options)""",

//DEFAULT MESSAGE
R"""(mfsync - multicast filesharing from the commandline

Usage: mfsync [share|fetch|get|sync] multicast-group [options]
  Use "mfsync [share|fetch|get|sync] --help" for mode specific help message

Options)"""};

  return messages[static_cast<size_t>(mode)];
}

