### multicast file sync -> sync local files on multiple machines

#### Planned syntax (this syntax is not implemented yet):

```
mfsync sync 239.255.0.1 8000 ./destination
```
  * share and get all available files
  * listens on port 8000
  * this mode runs forever

```
mfsync share 239.255.0.1 8000 ./destination
```
  * shares all available files but does not download new ones
  * this mode runs forever

```
mfsync fetch 239.255.0.1
```
  * requests names of all available files in the network and prints them to stdout

```
mfsync get 239.255.0.1 sha256sum1 sha256sum2 ./destination
```
  * downloads files with the given sha256sums to given destination if available
