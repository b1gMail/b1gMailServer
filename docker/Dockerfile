FROM ubuntu:xenial AS build-stage

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
  apt-get install -y build-essential git cmake autoconf libtool pkg-config \
  zlib1g-dev libncurses5-dev php7.0-cli wget

RUN mkdir -p /buildenv/include

WORKDIR /src/buildenv

RUN wget https://www.openssl.org/source/openssl-1.1.1s.tar.gz
RUN tar xzf openssl-1.1.1s.tar.gz
RUN cd openssl-1.1.1s && ./config --prefix=/buildenv shared && make -j && make install && cd ..

RUN wget https://invisible-mirror.net/archives/dialog/dialog-1.3-20160828.tgz
RUN tar xzf dialog-1.3-20160828.tgz
RUN cd dialog-1.3-20160828 && ./configure --disable-widec --prefix=/buildenv && make -j && make install && cp dlg_config.h dialog.h /buildenv/include && cd ..

RUN wget https://dlm.mariadb.com/2678567/Connectors/c/connector-c-3.3.3/mariadb-connector-c-3.3.3-src.tar.gz
RUN tar xzf mariadb-connector-c-3.3.3-src.tar.gz
RUN cd mariadb-connector-c-3.3.3-src && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/buildenv .. && make -j && make install && cd ..

RUN wget http://ftp.cs.stanford.edu/pub/exim/pcre/pcre-8.45.tar.gz
RUN tar xzf pcre-8.45.tar.gz
RUN cd pcre-8.45 && ./configure --prefix=/buildenv --enable-shared --disable-static && make -j && make install && cd ..

COPY ./src /src/b1gmailserver
WORKDIR /src/b1gmailserver/build
RUN cmake -DCMAKE_INSTALL_PREFIX=/buildenv -DCMAKE_BUILD_TYPE=Release .. && make -j && make dist

FROM scratch AS export-stage
COPY --from=build-stage /src/b1gmailserver/build/dist/*.run /
