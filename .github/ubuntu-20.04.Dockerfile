FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

COPY install_common.sh /install_common.sh
RUN chmod +x /install_common.sh && /install_common.sh
