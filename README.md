### multicast file sync -> sync local files on multiple machines

#### examples:

```
mfsync sync 239.255.0.1 ./destination
```
  * share and get all available files
  * this mode runs forever

```
mfsync share 239.255.0.1 ./destination
```
  * shares all available files but does not download new ones
  * this mode runs forever

```
mfsync fetch 239.255.0.1
```
  * requests names of all available files in the network and prints them to stdout

```
mfsync get 239.255.0.1 --request sha256sum1 sha256sum2 -- ./destination
```
  * downloads files with the given sha256sums to given destination if available
  * if no --request flag is set, all files are downloaded
