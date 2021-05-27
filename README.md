# mfsync - multicast file synchronisation
##### This project is work in progress and may change in the future

mfsync is a command line utility enabling file lookup/announcement using multicast communication. This always works on the local subnet and can work over wider areas if multicast routing is enabled. 
Files that where announced over multicast can then also be retrieved using mfsync. File transmissions can be interrupted and are continued the next time. File transmissions are not done using multicast, they use direct tcp connections instead.

mfsync can operate in four different modes, each fullfilling a different purpose. The four modes are 'share', 'fetch', 'get' and 'sync'. The modes are described below.

### Motivation

The motivation to write this tool is to have an easy way to backup files on multiple hosts (without the need to know any routing information or ip address) in a volatile network environment like an ad-hoc network. A file that is shared once by a host will distribute through the network even if the original host is gone. This way it is very hard to loose a file, even if lots of hosts will be removed. The mode 'mfsync sync' should serve this purpose and is described below.

## Modes:
### mfsync share
'mfsync share' basicly enables every other computer in the network to download files from the sharing host.
The sharing host announces all given files to the multicast group. Those files can then be downloaded from the host using mfsync on another machine.
```
mfsync share 239.255.0.1 ./destination
```
  * shares all available files but does not download new ones

### mfsync fetch
'mfsync fetch' "passively" listens to all announced files in the multicast group and prints them to stdout. 
This mode is used to get a list of all files that are currently made available within the multicast group.
```
mfsync fetch 239.255.0.1
```
  * requests names of all available files in the network and prints them to stdout

### mfsync get
'mfsync get' retreives all specified files. The files are specified by their sha256sum which can be seen when using 'mfsync fetch'. If no files are specified all announced files will be retreived.

```
mfsync get 239.255.0.1 --request sha256sum1 sha256sum2 -- ./destination
```
  * downloads files with the given sha256sums to given destination if available
  * if no --request flag is set, all files are downloaded

### mfsync sync
'mfsync sync' is basicly a combination of 'mfsync share' and 'mfsync get'. It announces all given files and also retreives all available files that are not stored locally already. Files that where retrieved are then also shared again.
```
mfsync sync 239.255.0.1 ./destination
```
  * share and get all available files

## Build:
mfsync depends on: spdlog, openssl, boost, cmake

In root directoy:
```
mkdir build && cd build
cmake ..
make
```

## Build with Nix:
Requires flake support

You can clone the repo and use ```nix develop``` and ```nix build``` as wanted.

If you just want to run the tool without cloning you can directly build it using:
```
nix build github:k4lipso/mfsync
```

## Firewall
Per default mfsync listens on tcp port 8000 and upd port 30001. Depending on the mode you run mfsync in not all ports need to be opened.
The table below shows which modes listen for tcp or upd packages depending on the mode.

|                 | share           | fetch           | get             | sync            |
| --------------- | --------------- | --------------- | --------------- | --------------- |
| UPD             |                 | X               | X               | X               |
| TCP             | X               |                 |                 | X               |

An X means that the according port has to be openend by the firewall.

##### Todos:
* allow secure file transfer using tls (this requires certificate generation on each host)
* add --timeout to end execution after given period
  * also finish 'mfsync get' if --request was specified and all specified files are downloaded
* eventually switch from self made network protocol to something json like
* many more that iam not aware of right now
