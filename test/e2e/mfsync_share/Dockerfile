FROM debian:bookworm

#update system and install necessary packages
RUN apt-get update
RUN apt-get upgrade -y
RUN apt-get install cmake libspdlog-dev openssl libboost1.81-all-dev cmake g++-12 build-essential libssl-dev git libcrypto++-dev libcrypto++-utils libcrypto++  -y

#prepare file structure
RUN mkdir /mfsync
RUN mkdir /mfsync/cmake
RUN mkdir /mfsync/lib
RUN mkdir /mfsync/src
RUN mkdir /mfsync/build
RUN mkdir /test_data
RUN mkdir /test_data/share
RUN mkdir /test_data/sync
RUN mkdir /test_data/get
RUN mkdir /test_data/combined

ADD CMakeLists.txt /mfsync/
COPY ./include /mfsync/include/
COPY ./cmake /mfsync/cmake/
COPY ./src /mfsync/src/

#actually build mfsync
WORKDIR /mfsync/build
RUN cmake .. -DUSE_SUBMODULES=ON -DBUILD_TYPE=Release
RUN make -j

#create test_data
COPY ./include /test_data/share/
COPY ./src /test_data/sync/

COPY ./include /test_data/combined/
COPY ./src /test_data/combined/

RUN apt-get install tree -y
CMD ["/mfsync/build/mfsync", "--help"]
