FROM gcr.io/oss-fuzz-base/base-builder:v1

ENV CLUSTERFUZZLITE=TRUE
RUN apt-get update && apt-get install -y bison libssl-dev libevent-dev
COPY . $SRC/openiked-portable
WORKDIR openiked-portable
COPY .clusterfuzzlite/build.sh $SRC/
