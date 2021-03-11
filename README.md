multicast file sync -> sync local files on multiple machines

commandline examples:

mfsync sync 8000 destination
  syncs all awailable files and listens on port 8000
  this mode runs forever

mfsync share 8000 destination
  shares all awailable files but does not download new ones
  this mode runs forever

mfsync add 8000 /path/to/file
  add file to local running "mfsync sync" instance on port 8000
  those will then be multicasted to other mfsync instances on the network

mfsync fetch
  requests names of all available files in the network and prints them to stdout

mfsync get filename destination
  downloads file to given destination if available
