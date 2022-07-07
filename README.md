# mfsync - multicast file synchronisation
##### This project is work in progress and may change in the future

mfsync is a command line utility enabling file lookup/announcement using multicast communication. This always works on the local subnet and can work over wider areas if multicast routing is enabled. 
Files that where announced over multicast can then also be retrieved using mfsync. File transmissions can be interrupted and are continued the next time. File transmissions are not done using multicast, they use direct tcp connections instead.

mfsync can operate in four different modes, each fullfilling a different purpose. The four modes are 'share', 'fetch', 'get' and 'sync'. The modes are described below.

### Motivation

The motivation to write this tool is to have an easy way to backup files on multiple hosts (without the need to know any routing information or ip address) in a volatile network environment like an ad-hoc network. A file that is shared once by a host will distribute through the network even if the original host is gone. This way it is very hard to loose a file, even if lots of hosts will be removed. The mode 'mfsync sync' should serve this purpose and is described below.

## Modes:
### mfsync share
'mfsync share' basically enables every other computer in the network to download files from the sharing host.
The sharing host announces all given files to the multicast group. Those files can then be downloaded from the host using mfsync on another machine.
```
mfsync share ./destination
```
  * shares all available files but does not download new ones

### mfsync fetch
'mfsync fetch' "passively" listens to all announced files in the multicast group and prints them to stdout. 
This mode is used to get a list of all files that are currently made available within the multicast group.
```
mfsync fetch
```
  * requests names of all available files in the network and prints them to stdout

### mfsync get
'mfsync get' retreives all specified files. The files are specified by their sha256sum which can be seen when using 'mfsync fetch'. If no files are specified all announced files will be retreived.

```
mfsync get --request sha256sum1 sha256sum2 -- ./destination
```
  * downloads files with the given sha256sums to given destination if available
  * if no --request flag is set, all files are downloaded

### mfsync sync
'mfsync sync' is basically a combination of 'mfsync share' and 'mfsync get'. It announces all given files and also retreives all available files that are not stored locally already. Files that where retrieved are then also shared again.
```
mfsync sync ./destination
```
  * share and get all available files

## TLS Support:
experimental tls support was added to mfsync. clients (receiving files) need a file containing the certificates of all trusted servers (sending files), while servers need a file containing its private key and certificate and file containing diffie hellman parameters.

The necessary keys and certificates could be generated with the following openssl commands:
```
openssl req -newkey rsa:2048 -new -nodes -x509 -days 3650 -keyout server.pem -out server.pem
openssl dhparam -out dhparams.pem 2048
```

'mfsync share' could then be operated as follows:
```
mfsync share --server-tls ./server.pem ./dhparams.pem -- ./destination
```

A client would need a file containg the servers certificates and then could retreive files using
```
mfsync get --client-tls ./ca.pem ./destination
```
If you want to use 'mfsync sync' both flags, --client-tls and --server-tls, are needed to function properly.

The table below shows which flags are neccessary for which mode:

|                 | share           | fetch           | get             | sync            |
| --------------- | --------------- | --------------- | --------------- | --------------- |
| --client-tls    |                 |                 | X               | X               |
| --server-tls    | X               |                 |                 | X               |


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
Per default mfsync listens on tcp port 8000 and udp port 30001. Depending on the mode you run mfsync in not all ports need to be opened.
The table below shows which modes listen for tcp or udp packages depending on the mode.

|                 | share           | fetch           | get             | sync            |
| --------------- | --------------- | --------------- | --------------- | --------------- |
| UDP             |                 | X               | X               | X               |
| TCP             | X               |                 |                 | X               |

An X means that the according port has to be openend by the firewall.

##### Todos:
* prettify output
* reuse connection if host still has other files to offer (important for tls)
* handle file permissions, check available space
* show some love to CMakeLists.txt
