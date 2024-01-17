# mfsync - multicast file synchronisation
##### This project is work in progress and may change in the future

![Build](https://github.com/k4lipso/mfsync/actions/workflows/cmake.yml/badge.svg)

mfsync is a command line utility enabling file lookup/announcement using multicast communication. This always works on the local subnet and can work over wider areas if multicast routing is enabled.

key features:
- **easy to use!** *you dont need to know any ip address*
- **end-to-end encrypted** *using ChaCha20Poly1305 authenticated encryption*
- **stop and go** *File transmissions can be interrupted and continued any time*

https://github.com/k4lipso/mfsync/assets/19481640/a570f897-7f79-4415-ac4b-6241f5da3b21

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
'mfsync get' retreives all specified files. The files are specified by their filename which can be seen when using 'mfsync fetch'. If no files are specified all announced files will be retreived.

```
mfsync get file1.txt directory/file2.txt ./destination
```
  * downloads files with the given filename to given destination if available
  * if no filenames are given all files are downloaded

### mfsync sync
'mfsync sync' is basically a combination of 'mfsync share' and 'mfsync get'. It announces all given files and also retreives all available files that are not stored locally already. Files that where retrieved are then also shared again.
```
mfsync sync ./destination
```
  * share and get all available files

## Encryption  
The communication between each host is fully end to end encrypted. Still, per default files are shared with everyone on the network.  

If you want to share your files only with some specific hosts you need to create config file containing the public keys of those *(and the other hosts needs also to trust your key)*.
To find out the keys you can use the following commands:
```bash 
mfsync --public-key #list your own public key
mfsync --list-hosts #list the keys of every host on the network
```

A config file looks like that (in the given example 3 different hosts would be trusted):
```json
{
  "trustedKeys": [
    "8FA14E51FCD446F1E0B3AEA53751CCF26FF5ADD271FAA3AA4318120B23CAA120",
    "7D477C5FEBC40A655962A262EAE7F7D5FF169F6A767C945F99CD0E9070C50002",
    "CC6B53A585170C2CC29B4BDF064FDEFD79A1CE2DB96186176C9DB7D900CC2B00"
  ]
}
```

If you place that file in ```~/.mfsync/config.json``` mfsync will automatically use it. If you put it somewhere else use the ```--config /path/to/your/config.json```  flag.
Another option is to pass the keys directly on the command line using the ```--trusted-keys``` flag.  

---

*Each hosts generates their own public and private key pair. Using the X25519 key agreement scheme a shared secret between each host is created which is then used for the ChaCha20Poly1305 encrypted communication. For each file transfer a unique key is derived from the shared secret using HKDF and a random salt.*

## Firewall
Per default mfsync listens on tcp port 8000 and udp port 30001. Depending on the mode you run mfsync in not all ports need to be opened.
The table below shows which modes listen for tcp or udp packages depending on the mode.

|                 | share           | fetch           | get             | sync            |
| --------------- | --------------- | --------------- | --------------- | --------------- |
| UDP             |                 | X               | X               | X               |
| TCP             | X               |                 |                 | X               |

An X means that the according port has to be openend by the firewall.

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
nix run github:k4lipso/mfsync
```

## TLS Support (Deprectated):
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

