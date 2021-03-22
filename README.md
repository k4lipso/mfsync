### multicast file sync -> sync local files on multiple machines

###### commandline examples (this syntax is not implemented yet):

```
mfsync sync 8000 destination
```

  * syncs all available files and listens on port 8000
  * this mode runs forever

```
mfsync share 8000 destination
```
  * shares all available files but does not download new ones
  * this mode runs forever

```
mfsync add 8000 /path/to/file
```
  * add file to local running "mfsync sync"/"mfsync share" instance on port 8000
  * those will then be multicasted to other mfsync instances on the network by the according mfsync instance

```
mfsync fetch
```
  * requests names of all available files in the network and prints them to stdout

```
mfsync get filename destination
```
  * downloads file to given destination if available
